package Plugins::HQPlayer::Plugin;

# HQPlayer LMS Plugin — main entry point.
#
# Registers the plugin with Lyrion Music Server, wires the web settings
# page, creates a virtual HQPlayer player client, and listens for
# track-end events via the daemon's /lms/events long-poll endpoint.
# When the daemon signals that HQPlayer has stopped (track ended), the
# plugin advances the LMS queue to the next track.

use strict;
use warnings;

use base qw(Slim::Plugin::Base);

use Digest::MD5 qw(md5_hex);
use Scalar::Util qw(blessed);
use Socket       qw(inet_aton sockaddr_in);

use Slim::Networking::SimpleAsyncHTTP;
use Slim::Utils::Log;
use Slim::Utils::Prefs;
use Slim::Utils::Timers;

my $log = Slim::Utils::Log->addLogCategory(
    {
        'category'     => 'plugin.hqplayer',
        'defaultLevel' => 'WARN',
        'description'  => 'HQPLAYER_NAME',
    }
);

my $prefs = preferences('plugin.hqplayer');

# ---------------------------------------------------------------------------
# Default preference values — kept in sync with Config.cpp defaults.
# ---------------------------------------------------------------------------
$prefs->init(
    {
        config_path          => '/etc/hqplayer/config.yaml',
        lms_host             => '127.0.0.1',
        lms_port             => 18080,
        log_level            => 'info',
        hqplayer_host        => '127.0.0.1',
        hqplayer_port        => 4321,
        hqplayer_timeout_ms  => 3000,
        hqplayer_poll_ms     => 5000,
        player_name          => 'HQPlayer',
    }
);

# ---------------------------------------------------------------------------
# initPlugin — called by LMS at startup.
# ---------------------------------------------------------------------------
sub initPlugin {
    my $class = shift;

    $class->SUPER::initPlugin(@_);

    if ( Slim::Utils::PluginManager->isEnabled('Plugins::HQPlayer::Plugin') ) {
        require Plugins::HQPlayer::Settings;
        Plugins::HQPlayer::Settings->new($class);

        # Register the virtual player after LMS has fully started.
        Slim::Utils::Timers::setTimer( undef, Time::HiRes::time() + 3,
            \&_registerVirtualPlayer );
    }

    $log->info( 'HQPlayer plugin initialised (v' . $class->_version() . ')' );
}

# ---------------------------------------------------------------------------
# getDisplayName — i18n key shown in the LMS plugin list.
# ---------------------------------------------------------------------------
sub getDisplayName { 'HQPLAYER_NAME' }

# ---------------------------------------------------------------------------
# _version — helper returning the version string from install.xml.
# ---------------------------------------------------------------------------
sub _version { '1.3.0' }

# ---------------------------------------------------------------------------
# prefs — accessor used by Settings.pm and tests.
# ---------------------------------------------------------------------------
sub prefs { $prefs }

# ---------------------------------------------------------------------------
# _registerVirtualPlayer — create (or re-use) the virtual HQPlayer client.
# ---------------------------------------------------------------------------
sub _registerVirtualPlayer {
    require Plugins::HQPlayer::Player;

    my $mac  = _generateMac(
        $prefs->get('hqplayer_host') || '127.0.0.1',
        $prefs->get('lms_port')      || 18080,
    );
    my $name = $prefs->get('player_name') || 'HQPlayer';

    # If the player already exists (e.g. after a plugin reload), just make
    # sure event listening is running and bail out.
    my $existing = Slim::Player::Client::getClient($mac);
    if ($existing) {
        $log->info("HQPlayer virtual player already registered ($mac)");
        _startListening($existing);
        return;
    }

    # Build a fake packed sockaddr_in pointing to localhost.
    my $paddr = sockaddr_in( 0, inet_aton('127.0.0.1') );

    # Instantiate the virtual player.
    # Arguments: ($id, $paddr, $revision, $udpsock, $deviceid, $uuid)
    my $client = Plugins::HQPlayer::Player->new( $mac, $paddr, 0, undef, 12, undef );

    Slim::Player::Client::add($client);
    $client->name($name);

    $log->info("HQPlayer virtual player registered as '$name' ($mac)");

    _startListening($client);
}

# ---------------------------------------------------------------------------
# _startListening — begin the /lms/events long-poll loop for $client.
# ---------------------------------------------------------------------------
sub _startListening {
    my ($client) = @_;
    _listenForTrackEnd($client);
}

# ---------------------------------------------------------------------------
# _listenForTrackEnd — send one async GET /lms/events request.
#
# The daemon blocks for up to 30 seconds waiting for a Playing→Stopped
# transition from HQPlayer.  When the response arrives:
#   - track_ended:true  → advance the LMS queue, then immediately reconnect.
#   - track_ended:false → the request timed out; immediately reconnect.
#   - error             → wait 2 s and reconnect to avoid hammering the daemon.
#
# This replaces the former adaptive polling timer: instead of periodically
# checking /lms/status, the plugin is notified the instant HQPlayer signals
# a state change, resulting in gapless-ready queue advancement.
# ---------------------------------------------------------------------------
sub _listenForTrackEnd {
    my ($client) = @_;

    my $host = $prefs->get('lms_host') // '127.0.0.1';
    my $port = $prefs->get('lms_port') // 18080;

    my $http = Slim::Networking::SimpleAsyncHTTP->new(
        sub {
            my ($http_obj) = @_;

            my $body = $http_obj->content // '';

            # {"track_ended":true} → advance the LMS queue.
            if ( $body =~ /"track_ended"\s*:\s*true/ ) {
                _advanceQueue($client);
            }

            # Immediately start the next long-poll.
            _listenForTrackEnd($client);
        },
        sub {
            my ( $http_obj, $error ) = @_;
            $log->warn("HQPlayer /lms/events failed: $error");

            # Brief delay before retrying so we don't hammer a down daemon.
            Slim::Utils::Timers::setTimer(
                $client,
                Time::HiRes::time() + 2,
                sub { _listenForTrackEnd($client); },
            );
        },
        # The daemon's /lms/events blocks for up to 30 s; give it 35 s before
        # we consider the request timed-out at the HTTP layer.
        { timeout => 35 },
    );

    $http->get("http://$host:$port/lms/events");
}

# ---------------------------------------------------------------------------
# _advanceQueue — tell LMS to move to the next track in the queue.
#
# This causes LMS to call load() on the virtual player with the next track,
# which in turn sends it to HQPlayer Embedded via the /lms/track endpoint.
# ---------------------------------------------------------------------------
sub _advanceQueue {
    my ($client) = @_;

    $log->info('HQPlayer: track ended, advancing LMS queue');
    $client->execute( [ 'playlist', 'index', '+1' ] );
}

# ---------------------------------------------------------------------------
# _generateMac — produce a stable, locally-administered MAC-like ID from
#                the HQPlayer host and the daemon port.
#
# Using a deterministic hash means the same virtual player ID survives LMS
# restarts, so the player stays selected in the Material skin.
# ---------------------------------------------------------------------------
sub _generateMac {
    my ( $host, $port ) = @_;

    my $hash = md5_hex("hqplayer:$host:$port");

    # Build MAC as 6 hex pairs.
    my $mac = join( ':', map { substr( $hash, $_ * 2, 2 ) } 0 .. 5 );

    # Set the locally-administered bit (bit 1 of the first octet) and clear
    # the multicast bit so the address is a valid unicast LAA.
    my $first = ( hex( substr( $mac, 0, 2 ) ) | 0x02 ) & 0xFE;
    substr( $mac, 0, 2 ) = sprintf( '%02x', $first );

    return $mac;
}

1;

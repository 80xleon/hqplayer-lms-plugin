package Plugins::HQPlayer::Plugin;

# HQPlayer LMS Plugin — main entry point.
#
# Registers the plugin with Lyrion Music Server, wires the web settings
# page, creates a virtual HQPlayer player client, and starts a polling
# timer that detects track-end events so LMS can advance its queue
# automatically.

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
sub _version { '1.2.0' }

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
    # sure polling is running and bail out.
    my $existing = Slim::Player::Client::getClient($mac);
    if ($existing) {
        $log->info("HQPlayer virtual player already registered ($mac)");
        _startPolling($existing);
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

    _startPolling($client);
}

# ---------------------------------------------------------------------------
# _startPolling — schedule the first status poll for $client.
# ---------------------------------------------------------------------------
sub _startPolling {
    my ($client) = @_;
    _scheduleNextPoll($client);
}

# ---------------------------------------------------------------------------
# _pollDaemon — timer callback: GET /lms/status and advance the queue on
#               track_ended == true.
#
# Adaptive behavior:
#   - while state == "playing" and track duration is unknown to LMS,
#     poll faster (half of configured interval, floored at 500ms)
#   - while state == "playing" and duration is known, use normal interval
#     until 90% progress, then switch to faster interval
#   - while paused/stopped/unknown, poll at configured interval
# ---------------------------------------------------------------------------
sub _pollDaemon {
    my ($client) = @_;

    my $host = $prefs->get('lms_host') // '127.0.0.1';
    my $port = $prefs->get('lms_port') // 18080;

    my $http = Slim::Networking::SimpleAsyncHTTP->new(
        sub {
            my ($http_obj) = @_;

            # Minimal JSON decode — avoid hard dependency on a JSON module.
            my $body = $http_obj->content // '';
            my ( $state, $track_ended ) = _extractStatusFields($body);
            if ($track_ended) {
                _advanceQueue($client);
            }

            # Reschedule for next poll.
            _scheduleNextPoll( $client, $state );
        },
        sub {
            my ( $http_obj, $error ) = @_;
            $log->warn("HQPlayer status poll failed: $error");

            # Retry at normal interval even on error.
            _scheduleNextPoll($client);
        },
        { timeout => 5 },
    );

    $http->get("http://$host:$port/lms/status");
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
# _scheduleNextPoll — schedule next /lms/status poll using adaptive interval.
# ---------------------------------------------------------------------------
sub _scheduleNextPoll {
    my ( $client, $state ) = @_;
    my $interval_ms = _nextPollIntervalMs( $client, $state );
    my $interval_s  = $interval_ms / 1000;
    Slim::Utils::Timers::setTimer( $client,
        Time::HiRes::time() + $interval_s, \&_pollDaemon );
}

# Return configured poll interval (ms), with a safe fallback.
sub _configuredPollMs {
    my $configured = $prefs->get('hqplayer_poll_ms') || 5000;
    return $configured > 0 ? $configured : 5000;
}

# Return the next poll interval (ms):
# - playing + unknown duration: half of configured interval, floored at 500ms
# - playing + known duration:
#     * < 90% progress => normal interval
#     * >= 90% progress => half interval (min 500ms)
# - paused/stopped/unknown: configured interval
sub _nextPollIntervalMs {
    my ( $client, $state ) = @_;
    my $normal_ms = _configuredPollMs();
    return $normal_ms unless defined $state && $state eq 'playing';

    my $fast_ms = int( $normal_ms / 2 );
    $fast_ms = 500 if $fast_ms < 500;

    my $progress_ratio = _trackProgressRatio($client);
    return $fast_ms unless defined $progress_ratio;     # duration unknown
    return $progress_ratio >= 0.9 ? $fast_ms : $normal_ms;
}

# Extract state + track_ended from /lms/status JSON in one scan.
sub _extractStatusFields {
    my ($body) = @_;
    return ( undef, 0 ) unless defined $body;

    my $state;
    my $track_ended = 0;
    while ( $body =~ /"(state|track_ended)"\s*:\s*("(?:playing|paused|stopped)"|true|false)/g ) {
        my ( $key, $value ) = ( $1, $2 );
        if ( $key eq 'state' ) {
            $value =~ s/^"|"$//g;
            $state = $value;
        }
        elsif ( $key eq 'track_ended' ) {
            $track_ended = ( $value eq 'true' ) ? 1 : 0;
        }
    }

    return ( $state, $track_ended );
}

# Return playback progress ratio [0..1], or undef if duration/elapsed unknown.
sub _trackProgressRatio {
    my ($client) = @_;
    return undef unless $client;

    my $elapsed  = _clientElapsedSeconds($client);
    my $duration = _clientDurationSeconds($client);

    return undef unless defined $elapsed && defined $duration;
    return undef unless $duration > 0;

    my $ratio = $elapsed / $duration;
    $ratio = 0 if $ratio < 0;
    $ratio = 1 if $ratio > 1;
    return $ratio;
}

sub _clientElapsedSeconds {
    my ($client) = @_;

    for my $method (qw(songTime currentTime elapsedTime playingSongElapsed)) {
        next unless $client->can($method);
        my $value = eval { $client->$method() };
        my $num   = _toPositiveNumberOrUndef($value);
        return $num if defined $num;
    }

    return undef;
}

sub _clientDurationSeconds {
    my ($client) = @_;

    for my $method (qw(playingSongDuration duration trackDuration)) {
        next unless $client->can($method);
        my $value = eval { $client->$method() };
        my $num   = _toPositiveNumberOrUndef($value);
        return $num if defined $num;
    }

    for my $song_getter (qw(playingSong currentSong song)) {
        next unless $client->can($song_getter);
        my $song = eval { $client->$song_getter() };
        next unless $song && ref $song;
        for my $song_method (qw(duration secs lengthSeconds)) {
            next unless $song->can($song_method);
            my $value = eval { $song->$song_method() };
            my $num   = _toPositiveNumberOrUndef($value);
            return $num if defined $num;
        }
    }

    return undef;
}

sub _toPositiveNumberOrUndef {
    my ($value) = @_;
    return undef unless defined $value;
    return undef unless $value =~ /^-?(?:\d+(?:\.\d*)?|\.\d+)$/;
    return $value + 0;
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

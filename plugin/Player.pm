package Plugins::HQPlayer::Player;

# HQPlayer LMS virtual player.
#
# Appears in Lyrion Music Server as a selectable audio output.  All playback
# commands are forwarded via HTTP to the hqplayer_lms_daemon running locally,
# which in turn relays them to HQPlayer Embedded over its XML/TCP API.
#
# Prerequisites:
#   - hqplayer_lms_daemon must be running (configured via the plugin settings
#     page under Settings → HQPlayer).
#   - For local file playback, the music library path must be accessible at
#     the same absolute path on both the LMS host and the HQPlayer Embedded
#     host (e.g. a shared NAS mount).

use strict;
use warnings;

use base qw(Slim::Player::Squeezebox2);

use Scalar::Util qw(blessed);
use URI::Escape   qw(uri_unescape);

use Slim::Networking::SimpleAsyncHTTP;
use Slim::Utils::Log;
use Slim::Utils::Prefs;

my $log   = Slim::Utils::Log->addLogCategory({ 'category' => 'plugin.hqplayer' });
my $prefs = Slim::Utils::Prefs::preferences('plugin.hqplayer');

# ---------------------------------------------------------------------------
# Identity
# ---------------------------------------------------------------------------

sub model     { 'hqplayer' }
sub modelName { 'HQPlayer' }

# ---------------------------------------------------------------------------
# Capability flags
# ---------------------------------------------------------------------------

sub hasAudioOut     { 1 }
sub hasHeadSubOut   { 0 }
sub hasDigitalOut   { 0 }
sub hasPowerControl { 0 }
sub isLocalPlayer   { 1 }
sub isPlayer        { 1 }

# Always report as connected while the plugin is loaded.
sub connected { 1 }

# ---------------------------------------------------------------------------
# Slim Protocol / hardware stubs
#
# Squeezebox2 tries to communicate with physical hardware via the Slim
# Protocol.  These overrides make the virtual player safe to use without a
# real hardware socket.
# ---------------------------------------------------------------------------

sub sendFrame        { }
sub tcpsock          { undef }
sub slimprotoVersion { 0 }

# ---------------------------------------------------------------------------
# Playback control
#
# LMS calls these methods on the player object when the user (or LMS
# internally) issues transport commands.  Each method makes an asynchronous
# HTTP POST to the local daemon endpoint.
# ---------------------------------------------------------------------------

sub play {
    my ( $self, $params ) = @_;
    $self->_daemonPost('/lms/play');
    return 1;
}

sub stop {
    my ( $self, $params ) = @_;
    $self->_daemonPost('/lms/stop');
    return 1;
}

sub pause {
    my ( $self, $params ) = @_;
    $self->_daemonPost('/lms/pause');
    return 1;
}

sub resume {
    my ( $self, $params ) = @_;
    $self->_daemonPost('/lms/play');
    return 1;
}

# ---------------------------------------------------------------------------
# Queue navigation
#
# LMS calls next() / prev() when the user (or LMS internally) requests a
# skip.  Instead of forwarding raw next/prev commands to HQPlayer Embedded,
# we advance the LMS queue directly.  LMS then calls load() on this player
# with the new track URL, which posts it to /lms/track so HQPlayer receives
# a PlayNextUri command.
# ---------------------------------------------------------------------------

sub next {
    my ( $self, $params ) = @_;
    $self->execute( [ 'playlist', 'index', '+1' ] );
    return 1;
}

sub prev {
    my ( $self, $params ) = @_;
    $self->execute( [ 'playlist', 'index', '-1' ] );
    return 1;
}

# ---------------------------------------------------------------------------
# Track loading
#
# LMS calls load() when it wants the player to start playing a new track.
# $track is either a Slim::Schema::Track object or a URL string.
#
# For file:// URLs, we extract the absolute filesystem path and POST it to
# the daemon's /lms/track endpoint.
#
# For non-file URLs (for example LMS-proxied streaming URLs), we forward the
# URL as-is to /lms/track so HQPlayer can open that URI directly.
#
# When $track is a Slim::Schema::Track object, we also extract title, artist,
# and album and include them in the POST body so the daemon can forward them
# to HQPlayer Embedded as Now Playing metadata.
#
# The daemon forwards the value to HQPlayer Embedded via
# <PlayNextUri uri="..." song="..." artist="..." album="..."/>:
#   - stopped → starts playing immediately
#   - playing → daemon first issues Stop, then starts the newly selected track
# ---------------------------------------------------------------------------

sub load {
    my ( $self, $track, $params, $callback ) = @_;

    my $url = blessed($track) ? $track->url : ( ref $track eq '' ? $track : undef );

    # Extract optional track metadata when a rich track object is available.
    my ( $title, $artist, $album ) = ( '', '', '' );
    if ( blessed($track) ) {
        $title  = $track->title      // '';
        $artist = $track->artistName // '';
        if ( $track->can('album') && $track->album ) {
            $album = $track->album->name // '';
        }
    }

    if ( defined $url && $url =~ m{^file://(.+)$}i ) {
        my $path = uri_unescape($1);

        # Strip the //localhost or //hostname prefix that some systems add.
        $path =~ s{^//[^/]*}{}i;

        if ( length $path ) {
            my $body = _buildTrackJson( $path, $title, $artist, $album );
            $self->_daemonPost( '/lms/track', $body );
        }
        else {
            $log->warn('HQPlayer load(): could not extract path from URL: ' . $url);
        }
    }
    elsif ( defined $url ) {
        my $body = _buildTrackJson( $url, $title, $artist, $album );
        $self->_daemonPost( '/lms/track', $body );
    }

    return 1;
}

# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

# Build the JSON body for a /lms/track POST request.
# $path is required; $title, $artist, $album are optional (empty string = omit).
sub _buildTrackJson {
    my ( $path, $title, $artist, $album ) = @_;
    my $json = '{"path":"' . _escapeJson($path) . '"';
    $json .= ',"title":"'  . _escapeJson($title)  . '"' if length $title;
    $json .= ',"artist":"' . _escapeJson($artist) . '"' if length $artist;
    $json .= ',"album":"'  . _escapeJson($album)  . '"' if length $album;
    $json .= '}';
    return $json;
}

sub _daemonPost {
    my ( $self, $endpoint, $body ) = @_;

    my $host = $prefs->get('lms_host') // '127.0.0.1';
    my $port = $prefs->get('lms_port') // 18080;
    my $url  = "http://$host:$port$endpoint";

    my $http = Slim::Networking::SimpleAsyncHTTP->new(
        sub { },    # success — no-op
        sub {
            my ( $http_obj, $error ) = @_;
            $log->warn("HQPlayer daemon call failed [$endpoint]: $error");
        },
        { timeout => 5 },
    );

    if ( defined $body ) {
        $http->post( $url, 'Content-Type' => 'application/json', $body );
    }
    else {
        $http->post($url);
    }
}

# Minimal JSON string escaping — avoids a hard dependency on a JSON module.
sub _escapeJson {
    my ($s) = @_;
    $s =~ s/\\/\\\\/g;
    $s =~ s/"/\\"/g;
    $s =~ s/\n/\\n/g;
    $s =~ s/\r/\\r/g;
    $s =~ s/\t/\\t/g;
    return $s;
}

1;

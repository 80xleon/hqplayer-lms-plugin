package Plugins::HQPlayer::Settings;

# HQPlayer LMS Plugin — web settings page.
#
# Extends Slim::Web::Settings so the page appears automatically under
# Settings → Plugin → HQPlayer in the LMS web UI.
#
# On GET  : populate the form with current prefs.
# On POST : validate, persist prefs, write config.yaml.

use strict;
use warnings;

use base qw(Slim::Web::Settings);

use IO::Socket::INET;
use Slim::Utils::Log;
use Slim::Utils::Prefs;

my $log   = Slim::Utils::Log->addLogCategory( { 'category' => 'plugin.hqplayer' } );
my $prefs = preferences('plugin.hqplayer');

# ---------------------------------------------------------------------------
# Valid log-level values — must match the enum accepted by Config.cpp.
# ---------------------------------------------------------------------------
my %VALID_LOG_LEVELS = map { $_ => 1 } qw(none trace debug info warn error);

# ---------------------------------------------------------------------------
# page — Template Toolkit template path (relative to HTML root).
# ---------------------------------------------------------------------------
sub page { 'plugins/HQPlayer/settings/basic.html' }

# ---------------------------------------------------------------------------
# prefs — list of preference keys managed by this settings page.
# ---------------------------------------------------------------------------
sub prefs {
    return ( $prefs, qw(config_path lms_host lms_port log_level log_path hqplayer_host hqplayer_port hqplayer_timeout_ms player_name) );
}

# ---------------------------------------------------------------------------
# handler — process GET and POST requests.
# ---------------------------------------------------------------------------
sub handler {
    my ( $class, $client, $params ) = @_;

    if ( $params->{'saveSettings'} ) {
        my @errors = _validate($params);

        if (@errors) {
            $params->{'errors'} = \@errors;
        }
        else {
            _persistPrefs($params);

            my $write_error = _writeConfig( $prefs->get('config_path') );
            if ($write_error) {
                $params->{'errors'} = [ 'HQPLAYER_ERR_WRITE ' . $write_error ];
                $log->error( 'Failed to write config.yaml: ' . $write_error );
            }
            else {
                $params->{'saved'} = 1;
                $log->info('HQPlayer config.yaml written successfully');
            }
        }
    }

    # Populate template params with current pref values for both GET and
    # re-displayed POST (so the form reflects what the user typed).
    $params->{'pref_config_path'}         = $prefs->get('config_path');
    $params->{'pref_lms_host'}            = $prefs->get('lms_host');
    $params->{'pref_lms_port'}            = $prefs->get('lms_port');
    $params->{'pref_log_level'}           = $prefs->get('log_level');
    $params->{'pref_log_path'}            = $prefs->get('log_path');
    $params->{'pref_hqplayer_host'}       = $prefs->get('hqplayer_host');
    $params->{'pref_hqplayer_port'}       = $prefs->get('hqplayer_port');
    $params->{'pref_hqplayer_timeout_ms'} = $prefs->get('hqplayer_timeout_ms');
    $params->{'pref_player_name'}         = $prefs->get('player_name');

    # Probe the LMS adapter daemon and inject reachability into the template.
    my $reachable = _probeDaemon( $prefs->get('lms_host'), $prefs->get('lms_port') );
    $params->{'daemon_reachable'} = $reachable;
    $params->{'daemon_status'}    = $reachable ? 'HQPLAYER_DAEMON_UP' : 'HQPLAYER_DAEMON_DOWN';

    # Probe HQPlayer itself.
    my $hqp_reachable = _probeDaemon( $prefs->get('hqplayer_host'), $prefs->get('hqplayer_port') );
    $params->{'hqplayer_reachable'} = $hqp_reachable;
    $params->{'hqplayer_status'}    = $hqp_reachable ? 'HQPLAYER_HQP_UP' : 'HQPLAYER_HQP_DOWN';

    return $class->SUPER::handler( $client, $params );
}

# ---------------------------------------------------------------------------
# _validate — returns a (possibly empty) list of i18n error keys.
# ---------------------------------------------------------------------------
sub _validate {
    my ($params) = @_;

    my @errors;

    my $config_path = _trim( $params->{'pref_config_path'} // '' );
    push @errors, 'HQPLAYER_ERR_CONFIG_PATH' unless length $config_path;

    my $host = _trim( $params->{'pref_lms_host'} // '' );
    unless ( _isValidIp($host) ) {
        push @errors, 'HQPLAYER_ERR_HOST';
    }

    my $port = $params->{'pref_lms_port'} // '';
    unless ( $port =~ /^\d+$/ && $port >= 1 && $port <= 65535 ) {
        push @errors, 'HQPLAYER_ERR_PORT';
    }

    my $level = lc( _trim( $params->{'pref_log_level'} // '' ) );
    unless ( $VALID_LOG_LEVELS{$level} ) {
        push @errors, 'HQPLAYER_ERR_LOG_LEVEL';
    }

    my $hqp_host = _trim( $params->{'pref_hqplayer_host'} // '' );
    unless ( length $hqp_host ) {
        push @errors, 'HQPLAYER_ERR_HQP_HOST';
    }

    my $hqp_port = $params->{'pref_hqplayer_port'} // '';
    unless ( $hqp_port =~ /^\d+$/ && $hqp_port >= 1 && $hqp_port <= 65535 ) {
        push @errors, 'HQPLAYER_ERR_HQP_PORT';
    }

    my $timeout = $params->{'pref_hqplayer_timeout_ms'} // '';
    unless ( $timeout =~ /^\d+$/ && $timeout >= 1 ) {
        push @errors, 'HQPLAYER_ERR_HQP_TIMEOUT';
    }

    my $player_name = _trim( $params->{'pref_player_name'} // '' );
    push @errors, 'HQPLAYER_ERR_PLAYER_NAME' unless length $player_name;

    return @errors;
}

# ---------------------------------------------------------------------------
# _persistPrefs — write validated values into the prefs store.
# ---------------------------------------------------------------------------
sub _persistPrefs {
    my ($params) = @_;

    $prefs->set( 'config_path',          _trim( $params->{'pref_config_path'} ) );
    $prefs->set( 'lms_host',             _trim( $params->{'pref_lms_host'} ) );
    $prefs->set( 'lms_port',             int( $params->{'pref_lms_port'} ) );
    $prefs->set( 'log_level',            lc( _trim( $params->{'pref_log_level'} ) ) );
    $prefs->set( 'log_path',             _trim( $params->{'pref_log_path'} // '' ) );
    $prefs->set( 'hqplayer_host',        _trim( $params->{'pref_hqplayer_host'} ) );
    $prefs->set( 'hqplayer_port',        int( $params->{'pref_hqplayer_port'} ) );
    $prefs->set( 'hqplayer_timeout_ms',  int( $params->{'pref_hqplayer_timeout_ms'} ) );
    $prefs->set( 'player_name',          _trim( $params->{'pref_player_name'} ) );
}

# ---------------------------------------------------------------------------
# _writeConfig — serialise prefs into the YAML format expected by Config.cpp.
# Returns undef on success, an error string on failure.
# ---------------------------------------------------------------------------
sub _writeConfig {
    my ($path) = @_;

    return 'config path is empty' unless defined $path && length $path;

    my $host        = $prefs->get('lms_host');
    my $port        = $prefs->get('lms_port');
    my $level       = $prefs->get('log_level');
    my $log_path    = $prefs->get('log_path') // '';
    my $hqp_host    = $prefs->get('hqplayer_host');
    my $hqp_port    = $prefs->get('hqplayer_port');
    my $hqp_timeout = $prefs->get('hqplayer_timeout_ms');

    # Build the YAML content.  The Config.cpp parser is a simple line-by-line
    # reader; indentation must use two spaces and section headers must end with
    # a colon (no trailing space).
    my $log_path_line = length($log_path) ? "\n  log_path: $log_path" : '';
    my $yaml = <<"END_YAML";
logging:
  level: $level$log_path_line

lms_adapter:
  host: $host
  port: $port

hqplayer:
  host: $hqp_host
  port: $hqp_port
  timeout_ms: $hqp_timeout
END_YAML

    open( my $fh, '>', $path )
        or return "cannot open '$path': $!";

    print {$fh} $yaml
        or do {
            my $err = $!;
            close $fh;
            return "write failed: $err";
        };

    close($fh) or return "close failed: $!";

    return undef;    ## no critic (ProhibitExplicitReturnUndef)
}

# ---------------------------------------------------------------------------
# _probeDaemon — attempt a non-blocking TCP connection to host:port.
# Returns 1 if the daemon is reachable, 0 otherwise.
# Uses a 2-second timeout so the settings page never hangs.
# ---------------------------------------------------------------------------
sub _probeDaemon {
    my ( $host, $port ) = @_;

    return 0 unless defined $host && length $host;
    return 0 unless defined $port && $port > 0;

    my $sock = IO::Socket::INET->new(
        PeerAddr => $host,
        PeerPort => $port,
        Proto    => 'tcp',
        Timeout  => 2,
    );

    if ($sock) {
        $sock->close();
        return 1;
    }

    return 0;
}

# ---------------------------------------------------------------------------
# _isValidIp — lightweight IPv4 / IPv6 validation without POSIX dependencies.
# ---------------------------------------------------------------------------
sub _isValidIp {
    my ($addr) = @_;

    return 0 unless defined $addr && length $addr;

    # IPv4: four octets 0-255.
    if ( $addr =~ /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/ ) {
        return ( $1 <= 255 && $2 <= 255 && $3 <= 255 && $4 <= 255 ) ? 1 : 0;
    }

    # IPv6: allow compressed forms; validate via character-set check
    # (full RFC-3513 validation is beyond what we need here).
    if ( $addr =~ /^[0-9a-fA-F:]+$/ && $addr =~ /::/ || $addr =~ /^([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}$/ ) {
        return 1;
    }

    # Loopback shorthand — also valid.
    return 1 if $addr eq '::1';

    return 0;
}

# ---------------------------------------------------------------------------
# _trim — remove leading and trailing whitespace from a string.
# ---------------------------------------------------------------------------
sub _trim {
    my ($s) = @_;
    $s =~ s/^\s+|\s+$//g if defined $s;
    return $s // '';
}

1;

package Plugins::HQPlayer::Plugin;

# HQPlayer LMS Plugin — main entry point.
#
# Registers the plugin with Lyrion Music Server and wires the web settings
# page.  Prefs are persisted via Slim::Utils::Prefs so values survive
# server restarts.

use strict;
use warnings;

use base qw(Slim::Plugin::Base);

use Slim::Utils::Log;
use Slim::Utils::Prefs;

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
        config_path => '/etc/hqplayer/config.yaml',
        lms_host    => '127.0.0.1',
        lms_port    => 18080,
        log_level   => 'info',
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
    }

    $log->info('HQPlayer plugin initialised (v' . $class->_version() . ')');
}

# ---------------------------------------------------------------------------
# getDisplayName — i18n key shown in the LMS plugin list.
# ---------------------------------------------------------------------------
sub getDisplayName { 'HQPLAYER_NAME' }

# ---------------------------------------------------------------------------
# _version — helper returning the version string from install.xml.
# ---------------------------------------------------------------------------
sub _version { '0.8.3' }

# ---------------------------------------------------------------------------
# prefs — accessor used by Settings.pm and tests.
# ---------------------------------------------------------------------------
sub prefs { $prefs }

1;

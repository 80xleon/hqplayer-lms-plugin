#pragma once

#include "hqplayer/util/Logger.hpp"

#include <cstdint>
#include <string>

namespace hqplayer::config {

/// Logging configuration loaded from the @c logging: YAML section.
struct LoggingConfig {
    hqplayer::util::LogLevel level{hqplayer::util::LogLevel::Info};

    /// Optional path to a log file.  When non-empty the daemon appends log
    /// messages to this file in addition to @c stdout.  Leave empty to log
    /// to stdout only (default, suitable for journald).
    std::string log_path{};
};

/// Network settings for the daemon's HTTP server, loaded from the
/// @c lms_adapter: YAML section.
struct LmsAdapterConfig {
    /// Bind address for the daemon's HTTP server (IPv4 or IPv6).
    /// Must be reachable from the LMS Perl process (use @c 127.0.0.1 when
    /// both run on the same machine).
    std::string   host{"127.0.0.1"};

    /// TCP port for the daemon's HTTP server (1–65535).
    std::uint16_t port{8080};
};

/// Connection settings for the HQPlayer XML/TCP control API (port 4321),
/// loaded from the @c hqplayer: YAML section.
struct HQPlayerConfig {
    /// Hostname or IP address where HQPlayer Embedded is running.
    std::string host{"127.0.0.1"};

    /// TCP port of the HQPlayer XML control API (default 4321).
    std::uint16_t port{4321};

    /// Socket send/receive timeout in milliseconds.
    std::uint32_t timeout_ms{3000};
};

/// Top-level application configuration aggregating all subsystem configs.
struct AppConfig {
    LoggingConfig    logging{};
    LmsAdapterConfig lms_adapter{};
    HQPlayerConfig   hqplayer{};
};

/// Parse @p path as a YAML config file and return the resulting AppConfig.
/// @throws std::runtime_error   if the file cannot be opened.
/// @throws std::invalid_argument if any required field is missing or invalid.
AppConfig parseConfigFile(const std::string& path);

/// Parse @p text as YAML config content and return the resulting AppConfig.
/// @throws std::invalid_argument if any required field is missing or invalid.
AppConfig parseConfigText(const std::string& text);

} // namespace hqplayer::config

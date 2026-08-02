#pragma once

#include "hqplayer/util/Logger.hpp"

#include <cstdint>
#include <string>

namespace hqplayer::config {

struct LoggingConfig {
    hqplayer::util::LogLevel level{hqplayer::util::LogLevel::Info};
};

struct LmsAdapterConfig {
    std::string   host{"127.0.0.1"};
    std::uint16_t port{8080};
};

/// Connection settings for the HQPlayer XML/TCP control API (port 4321).
struct HQPlayerConfig {
    /// Hostname or IP address where HQPlayer Embedded is running.
    std::string host{"127.0.0.1"};

    /// TCP port of the HQPlayer XML control API (default 4321).
    std::uint16_t port{4321};

    /// Socket send/receive timeout in milliseconds.
    std::uint32_t timeout_ms{3000};
};

struct AppConfig {
    LoggingConfig   logging{};
    LmsAdapterConfig lms_adapter{};
    HQPlayerConfig   hqplayer{};
};

AppConfig parseConfigFile(const std::string& path);
AppConfig parseConfigText(const std::string& text);

} // namespace hqplayer::config

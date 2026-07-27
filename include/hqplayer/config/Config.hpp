#pragma once

#include "hqplayer/util/Logger.hpp"

#include <cstdint>
#include <string>

namespace hqplayer::config {

struct LoggingConfig {
    hqplayer::util::LogLevel level{hqplayer::util::LogLevel::Info};
};

struct LmsAdapterConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
};

struct AppConfig {
    LoggingConfig logging{};
    LmsAdapterConfig lms_adapter{};
};

AppConfig parseConfigFile(const std::string& path);
AppConfig parseConfigText(const std::string& text);

} // namespace hqplayer::config

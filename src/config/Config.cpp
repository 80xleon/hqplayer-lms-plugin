#include "hqplayer/config/Config.hpp"

#include <boost/asio/ip/address.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace hqplayer::config {
namespace {

std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

void validateHost(const std::string& host) {
    boost::system::error_code ec;
    (void)boost::asio::ip::make_address(host, ec);
    if (ec) {
        throw std::invalid_argument(
            "Invalid config value lms_adapter.host='" + host + "': " + ec.message());
    }
}

} // namespace

AppConfig parseConfigText(const std::string& text) {
    AppConfig cfg{};

    std::istringstream input(text);
    std::string line;
    std::string section;

    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line = line.substr(0, hash_pos);
        }

        auto current = trim(line);
        if (current.empty()) {
            continue;
        }

        if (current.back() == ':') {
            section = trim(current.substr(0, current.size() - 1));
            continue;
        }

        const auto colon = current.find(':');
        if (colon == std::string::npos) {
            throw std::invalid_argument("Invalid config line " + std::to_string(line_number) + ": missing ':'");
        }

        const auto key = trim(current.substr(0, colon));
        const auto value = trim(current.substr(colon + 1));

        if (section == "logging" && key == "level") {
            cfg.logging.level = hqplayer::util::Logger::parseLevel(value);
            continue;
        }

        if (section == "lms_adapter" && key == "host") {
            validateHost(value);
            cfg.lms_adapter.host = value;
            continue;
        }

        if (section == "lms_adapter" && key == "port") {
            try {
                const auto parsed = std::stoul(value);
                if (parsed == 0 || parsed > 65535) {
                    throw std::out_of_range("range");
                }
                cfg.lms_adapter.port = static_cast<std::uint16_t>(parsed);
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "Invalid config value lms_adapter.port='" + value + "': expected 1..65535");
            }
            continue;
        }

        if (section == "hqplayer" && key == "host") {
            cfg.hqplayer.host = value;
            continue;
        }

        if (section == "hqplayer" && key == "port") {
            try {
                const auto parsed = std::stoul(value);
                if (parsed == 0 || parsed > 65535) {
                    throw std::out_of_range("range");
                }
                cfg.hqplayer.port = static_cast<std::uint16_t>(parsed);
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "Invalid config value hqplayer.port='" + value + "': expected 1..65535");
            }
            continue;
        }

        if (section == "hqplayer" && key == "timeout_ms") {
            try {
                const auto parsed = std::stoul(value);
                if (parsed == 0) {
                    throw std::out_of_range("range");
                }
                cfg.hqplayer.timeout_ms = static_cast<std::uint32_t>(parsed);
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "Invalid config value hqplayer.timeout_ms='" + value + "': expected > 0");
            }
            continue;
        }

        if (section == "hqplayer" && key == "poll_interval_ms") {
            try {
                const auto parsed = std::stoul(value);
                if (parsed == 0) {
                    throw std::out_of_range("range");
                }
                cfg.hqplayer.poll_interval_ms = static_cast<std::uint32_t>(parsed);
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "Invalid config value hqplayer.poll_interval_ms='" + value + "': expected > 0");
            }
            continue;
        }
    }

    validateHost(cfg.lms_adapter.host);
    return cfg;
}

AppConfig parseConfigFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open config file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseConfigText(buffer.str());
}

} // namespace hqplayer::config

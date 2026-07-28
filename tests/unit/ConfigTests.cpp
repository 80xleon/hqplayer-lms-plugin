#include "hqplayer/config/Config.hpp"
#include "hqplayer/util/Logger.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testValidHostAndPort() {
    const auto cfg = hqplayer::config::parseConfigText(R"(
logging:
  level: debug
lms_adapter:
  host: 127.0.0.1
  port: 18080
)");

    assertTrue(cfg.logging.level == hqplayer::util::LogLevel::Debug, "logging.level should parse");
    assertTrue(cfg.lms_adapter.host == "127.0.0.1", "lms_adapter.host should parse");
    assertTrue(cfg.lms_adapter.port == 18080, "lms_adapter.port should parse");
}

void testHQPlayerDefaults() {
    const auto cfg = hqplayer::config::parseConfigText(R"(
lms_adapter:
  host: 127.0.0.1
  port: 18080
)");

    assertTrue(cfg.hqplayer.host == "127.0.0.1", "hqplayer.host default should be 127.0.0.1");
    assertTrue(cfg.hqplayer.port == 4321, "hqplayer.port default should be 4321");
    assertTrue(cfg.hqplayer.timeout_ms == 3000, "hqplayer.timeout_ms default should be 3000");
    assertTrue(cfg.hqplayer.poll_interval_ms == 5000, "hqplayer.poll_interval_ms default should be 5000");
}

void testHQPlayerSection() {
    const auto cfg = hqplayer::config::parseConfigText(R"(
lms_adapter:
  host: 127.0.0.1
  port: 18080
hqplayer:
  host: 192.168.1.10
  port: 4321
  timeout_ms: 2000
  poll_interval_ms: 3000
)");

    assertTrue(cfg.hqplayer.host == "192.168.1.10", "hqplayer.host should parse");
    assertTrue(cfg.hqplayer.port == 4321, "hqplayer.port should parse");
    assertTrue(cfg.hqplayer.timeout_ms == 2000, "hqplayer.timeout_ms should parse");
    assertTrue(cfg.hqplayer.poll_interval_ms == 3000, "hqplayer.poll_interval_ms should parse");
}

void testInvalidHQPlayerPortThrows() {
    bool thrown = false;
    try {
        (void)hqplayer::config::parseConfigText(R"(
lms_adapter:
  host: 127.0.0.1
  port: 18080
hqplayer:
  port: 99999
)");
    } catch (const std::invalid_argument& e) {
        thrown = true;
        const std::string message = e.what();
        assertTrue(message.find("hqplayer.port") != std::string::npos,
                   "invalid hqplayer port error should mention hqplayer.port");
    }
    assertTrue(thrown, "invalid hqplayer port should throw");
}

void testInvalidHQPlayerTimeoutThrows() {
    bool thrown = false;
    try {
        (void)hqplayer::config::parseConfigText(R"(
lms_adapter:
  host: 127.0.0.1
  port: 18080
hqplayer:
  timeout_ms: 0
)");
    } catch (const std::invalid_argument& e) {
        thrown = true;
        const std::string message = e.what();
        assertTrue(message.find("hqplayer.timeout_ms") != std::string::npos,
                   "invalid timeout error should mention hqplayer.timeout_ms");
    }
    assertTrue(thrown, "zero timeout_ms should throw");
}

void testInvalidHostThrowsClearError() {
    bool thrown = false;
    try {
        (void)hqplayer::config::parseConfigText(R"(
lms_adapter:
  host: 999.999.999.999
  port: 18080
)");
    } catch (const std::invalid_argument& e) {
        thrown = true;
        const std::string message = e.what();
        assertTrue(message.find("lms_adapter.host") != std::string::npos,
                   "invalid host error should mention lms_adapter.host");
    }
    assertTrue(thrown, "invalid host should throw");
}

} // namespace

int main() {
    try {
        testValidHostAndPort();
        testInvalidHostThrowsClearError();
        testHQPlayerDefaults();
        testHQPlayerSection();
        testInvalidHQPlayerPortThrows();
        testInvalidHQPlayerTimeoutThrows();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ConfigTests failed: " << e.what() << std::endl;
        return 1;
    }
}

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
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ConfigTests failed: " << e.what() << std::endl;
        return 1;
    }
}

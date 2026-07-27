#include "hqplayer/config/Config.hpp"
#include "hqplayer/lms/LmsBridge.hpp"
#include "hqplayer/lms/LmsHttpAdapter.hpp"
#include "hqplayer/util/Logger.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string config_path = (argc > 1) ? argv[1] : "config.yaml";

    try {
        const auto config = hqplayer::config::parseConfigFile(config_path);
        hqplayer::util::Logger::instance().setLevel(config.logging.level);

        hqplayer::lms::LmsBridge bridge;
        hqplayer::lms::LmsHttpAdapter adapter(bridge, config.lms_adapter.host, config.lms_adapter.port);
        adapter.start();

        std::cout << "HQPlayer LMS adapter is running on " << config.lms_adapter.host << ":"
                  << adapter.boundPort() << "\nPress ENTER to stop." << std::endl;
        std::string line;
        std::getline(std::cin, line);

        adapter.stop();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}

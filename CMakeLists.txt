cmake_minimum_required(VERSION 3.16)
project(hqplayer_lms_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

add_library(hqplayer_core
    src/config/Config.cpp
    src/lms/LmsBridge.cpp
    src/lms/LmsHttpAdapter.cpp
    src/util/Logger.cpp
)

target_include_directories(hqplayer_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hqplayer_core
    PUBLIC
        Boost::system
        Threads::Threads
)

add_executable(hqplayer_lms_daemon src/main.cpp)
target_link_libraries(hqplayer_lms_daemon PRIVATE hqplayer_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(config_tests tests/unit/ConfigTests.cpp)
    target_link_libraries(config_tests PRIVATE hqplayer_core)
    add_test(NAME config_tests COMMAND config_tests)

    add_executable(lms_http_adapter_integration_tests tests/integration/LmsHttpAdapterIntegrationTests.cpp)
    target_link_libraries(lms_http_adapter_integration_tests PRIVATE hqplayer_core)
    add_test(NAME lms_http_adapter_integration_tests COMMAND lms_http_adapter_integration_tests)
endif()

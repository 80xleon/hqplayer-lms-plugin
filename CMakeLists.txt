cmake_minimum_required(VERSION 3.16)
project(hqplayer_lms_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost QUIET CONFIG)
if(NOT Boost_FOUND)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 OLD)
    endif()
    find_package(Boost REQUIRED)
endif()
find_package(Threads REQUIRED)

add_library(hqplayer_core
    src/config/Config.cpp
    src/hqplayer/HQPlayerClient.cpp
    src/hqplayer/HQPlayerEventListener.cpp
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
        Threads::Threads
)

if(TARGET Boost::headers)
    target_link_libraries(hqplayer_core PUBLIC Boost::headers)
else()
    target_include_directories(hqplayer_core PUBLIC ${Boost_INCLUDE_DIRS})
endif()

target_compile_definitions(hqplayer_core PUBLIC BOOST_ERROR_CODE_HEADER_ONLY)

add_executable(hqplayer_lms_daemon src/main.cpp)
target_link_libraries(hqplayer_lms_daemon PRIVATE hqplayer_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(config_tests tests/unit/ConfigTests.cpp)
    target_link_libraries(config_tests PRIVATE hqplayer_core)
    add_test(NAME config_tests COMMAND config_tests)

    add_executable(hqplayer_client_tests tests/unit/HQPlayerClientTests.cpp)
    target_link_libraries(hqplayer_client_tests PRIVATE hqplayer_core)
    add_test(NAME hqplayer_client_tests COMMAND hqplayer_client_tests)

    add_executable(hqplayer_event_listener_tests tests/unit/HQPlayerEventListenerTests.cpp)
    target_link_libraries(hqplayer_event_listener_tests PRIVATE hqplayer_core)
    add_test(NAME hqplayer_event_listener_tests COMMAND hqplayer_event_listener_tests)

    add_executable(lms_http_adapter_integration_tests tests/integration/LmsHttpAdapterIntegrationTests.cpp)
    target_link_libraries(lms_http_adapter_integration_tests PRIVATE hqplayer_core)
    add_test(NAME lms_http_adapter_integration_tests COMMAND lms_http_adapter_integration_tests)
endif()

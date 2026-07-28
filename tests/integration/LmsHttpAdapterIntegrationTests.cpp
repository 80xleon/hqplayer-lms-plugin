#include "hqplayer/hqplayer/IHQPlayerClient.hpp"
#include "hqplayer/lms/LmsBridge.hpp"
#include "hqplayer/lms/LmsHttpAdapter.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

/// Minimal IHQPlayerClient that does nothing — used to isolate the HTTP
/// adapter layer in integration tests.
struct NullHQPlayerClient final : hqplayer::hqplayer::IHQPlayerClient {
    void play()  override {}
    void pause() override {}
    void stop()  override {}
    hqplayer::hqplayer::HQPlayerStatus getStatus() override { return {}; }
};

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

http::response<http::string_body> sendRequest(
    std::uint16_t port,
    http::verb method,
    const std::string& target,
    const std::string& body = "") {
    boost::asio::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    stream.expires_after(std::chrono::seconds(2));

    const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
    stream.connect(endpoints);

    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::user_agent, "adapter-integration-test");
    request.set(http::field::connection, "close");
    request.body() = body;
    request.prepare_payload();

    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return response;
}

void testEndpoints() {
    NullHQPlayerClient nullClient;
    hqplayer::lms::LmsBridge bridge(nullClient);
    hqplayer::lms::LmsHttpAdapter adapter(bridge, "127.0.0.1", 0);
    adapter.start();

    const auto port = adapter.boundPort();
    assertTrue(port != 0, "adapter should expose actual bound port");

    auto status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.result() == http::status::ok, "GET /lms/status should succeed");
    assertTrue(status.body().find("\"state\":\"stopped\"") != std::string::npos,
               "status response should be JSON with stopped state");

    auto play = sendRequest(port, http::verb::post, "/lms/play");
    assertTrue(play.result() == http::status::ok, "POST /lms/play should succeed");
    assertTrue(play.body() == "{\"ok\":true}", "play response should preserve JSON contract");

    status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.body().find("\"state\":\"playing\"") != std::string::npos,
               "status after play should be playing");

    auto pause = sendRequest(port, http::verb::post, "/lms/pause");
    assertTrue(pause.result() == http::status::ok, "POST /lms/pause should succeed");

    status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.body().find("\"state\":\"paused\"") != std::string::npos,
               "status after pause should be paused");

    auto stop = sendRequest(port, http::verb::post, "/lms/stop");
    assertTrue(stop.result() == http::status::ok, "POST /lms/stop should succeed");

    status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.body().find("\"state\":\"stopped\"") != std::string::npos,
               "status after stop should be stopped");

    auto missing = sendRequest(port, http::verb::get, "/lms/missing");
    assertTrue(missing.result() == http::status::not_found, "unknown endpoint should return 404");

    adapter.stop();
}

void testInvalidHostAtStart() {
    NullHQPlayerClient nullClient;
    hqplayer::lms::LmsBridge bridge(nullClient);
    hqplayer::lms::LmsHttpAdapter adapter(bridge, "bad_host", 18083);

    bool thrown = false;
    try {
        adapter.start();
    } catch (const std::runtime_error& e) {
        thrown = true;
        const std::string message = e.what();
        assertTrue(message.find("lms_adapter.host") != std::string::npos,
                   "invalid host start error should mention lms_adapter.host");
    }

    assertTrue(thrown, "invalid host should fail adapter start");
}

} // namespace

int main() {
    try {
        testEndpoints();
        testInvalidHostAtStart();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "LmsHttpAdapterIntegrationTests failed: " << e.what() << std::endl;
        return 1;
    }
}

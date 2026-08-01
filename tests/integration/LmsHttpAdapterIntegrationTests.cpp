#include "hqplayer/hqplayer/IHQPlayerClient.hpp"
#include "hqplayer/lms/LmsBridge.hpp"
#include "hqplayer/lms/LmsHttpAdapter.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
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
    void play()  override { ++playCalls; }
    void pause() override { ++pauseCalls; }
    void stop()  override { ++stopCalls; }
    void next()  override { ++nextCalls; }
    void prev()  override { ++prevCalls; }
    void playNextUri(const std::string&,
                     const hqplayer::hqplayer::TrackMetadata& meta = {}) override {
        ++playNextUriCalls;
        lastMetadata = meta;
    }
    void loadTrack(const std::string&) override { ++loadTrackCalls; }
    hqplayer::hqplayer::HQPlayerStatus getStatus() override {
        hqplayer::hqplayer::HQPlayerStatus status;
        status.state =
            static_cast<hqplayer::hqplayer::HQPlayerState>(reportedState.load());
        return status;
    }

    std::atomic<int> playCalls{0};
    std::atomic<int> pauseCalls{0};
    std::atomic<int> stopCalls{0};
    std::atomic<int> nextCalls{0};
    std::atomic<int> prevCalls{0};
    std::atomic<int> playNextUriCalls{0};
    std::atomic<int> loadTrackCalls{0};
    std::atomic<int> reportedState{
        static_cast<int>(hqplayer::hqplayer::HQPlayerState::Stopped)};
    hqplayer::hqplayer::TrackMetadata lastMetadata{};
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

    // --- next / prev are handled by Player.pm (LMS queue advance) ---
    // The daemon no longer exposes these endpoints; requests return 404.
    auto next = sendRequest(port, http::verb::post, "/lms/next");
    assertTrue(next.result() == http::status::not_found,
               "POST /lms/next should return 404 — next/prev are handled by LMS directly");

    auto prev = sendRequest(port, http::verb::post, "/lms/prev");
    assertTrue(prev.result() == http::status::not_found,
               "POST /lms/prev should return 404 — next/prev are handled by LMS directly");

    auto missing = sendRequest(port, http::verb::get, "/lms/missing");
    assertTrue(missing.result() == http::status::not_found, "unknown endpoint should return 404");

    adapter.stop();
}

void testAlbumEndpoint() {
    NullHQPlayerClient nullClient;
    hqplayer::lms::LmsBridge bridge(nullClient);
    hqplayer::lms::LmsHttpAdapter adapter(bridge, "127.0.0.1", 0);
    adapter.start();

    const auto port = adapter.boundPort();

    // Valid path — returns 501 because HQPlayer XML API does not support album play yet.
    auto resp = sendRequest(port, http::verb::post, "/lms/album",
                            R"({"path":"/music/MyAlbum"})");
    assertTrue(resp.result() == http::status::not_implemented,
               "POST /lms/album with valid path should return 501 (feature pending HQPlayer API)");
    assertTrue(resp.body().find("\"error\"") != std::string::npos,
               "501 response should include error field");

    // Missing path field — 400.
    auto missingPath = sendRequest(port, http::verb::post, "/lms/album", R"({"other":"x"})");
    assertTrue(missingPath.result() == http::status::bad_request,
               "POST /lms/album without path should return 400");

    // Empty body — 400.
    auto emptyBody = sendRequest(port, http::verb::post, "/lms/album", "");
    assertTrue(emptyBody.result() == http::status::bad_request,
               "POST /lms/album with empty body should return 400");

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

void testTrackEndpoint() {
    NullHQPlayerClient nullClient;
    hqplayer::lms::LmsBridge bridge(nullClient);
    hqplayer::lms::LmsHttpAdapter adapter(bridge, "127.0.0.1", 0);
    adapter.start();

    const auto port = adapter.boundPort();

    // Valid path — returns 200 ok.
    auto resp = sendRequest(port, http::verb::post, "/lms/track",
                            R"({"path":"/music/Artist/Album/01.flac"})");
    assertTrue(resp.result() == http::status::ok,
               "POST /lms/track with valid path should return 200");
    assertTrue(resp.body() == "{\"ok\":true}",
               "POST /lms/track response should be {\"ok\":true}");
    assertTrue(nullClient.stopCalls.load() == 0,
               "stopped state should not send stop before loading track");
    assertTrue(nullClient.playNextUriCalls.load() == 1,
               "first /lms/track should call playNextUri exactly once");

    // After loadTrack, status should be optimistically playing.
    auto status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.body().find("\"state\":\"playing\"") != std::string::npos,
               "status after /lms/track should be playing (optimistic)");

    // If HQPlayer is already playing, /lms/track should stop current playback
    // before loading the newly selected track.
    nullClient.reportedState.store(static_cast<int>(hqplayer::hqplayer::HQPlayerState::Playing));
    resp = sendRequest(port, http::verb::post, "/lms/track",
                       R"({"path":"/music/Artist/Album/02.flac"})");
    assertTrue(resp.result() == http::status::ok,
               "POST /lms/track while playing should still return 200");
    assertTrue(nullClient.stopCalls.load() == 1,
               "playing state should trigger stop before loading new track");
    assertTrue(nullClient.playNextUriCalls.load() == 2,
               "second /lms/track should call playNextUri again");

    // Missing path field — 400.
    auto missingPath = sendRequest(port, http::verb::post, "/lms/track", R"({"other":"x"})");
    assertTrue(missingPath.result() == http::status::bad_request,
               "POST /lms/track without path should return 400");

    // Empty body — 400.
    auto emptyBody = sendRequest(port, http::verb::post, "/lms/track", "");
    assertTrue(emptyBody.result() == http::status::bad_request,
               "POST /lms/track with empty body should return 400");

    adapter.stop();
}

void testTrackMetadataPassthrough() {
    NullHQPlayerClient nullClient;
    hqplayer::lms::LmsBridge bridge(nullClient);
    hqplayer::lms::LmsHttpAdapter adapter(bridge, "127.0.0.1", 0);
    adapter.start();

    const auto port = adapter.boundPort();

    // POST with full metadata (including coverart) — all fields should reach playNextUri.
    auto resp = sendRequest(port, http::verb::post, "/lms/track",
                            R"({"path":"/music/01.flac","title":"My Song","artist":"My Artist","album":"My Album","coverart":"/music/Artist/Album/cover.jpg"})");
    assertTrue(resp.result() == http::status::ok,
               "POST /lms/track with metadata should return 200");
    assertTrue(nullClient.lastMetadata.title    == "My Song",   "title should be forwarded");
    assertTrue(nullClient.lastMetadata.artist   == "My Artist", "artist should be forwarded");
    assertTrue(nullClient.lastMetadata.album    == "My Album",  "album should be forwarded");
    assertTrue(nullClient.lastMetadata.coverart == "/music/Artist/Album/cover.jpg",
               "coverart should be forwarded");

    // POST with path only — metadata fields should be empty (including coverart).
    resp = sendRequest(port, http::verb::post, "/lms/track",
                       R"({"path":"/music/02.flac"})");
    assertTrue(resp.result() == http::status::ok,
               "POST /lms/track without metadata should return 200");
    assertTrue(nullClient.lastMetadata.empty(),
               "metadata should be empty when not supplied");

    adapter.stop();
}

void testTrackEndedFlag() {
    NullHQPlayerClient nullClient;
    hqplayer::lms::LmsBridge bridge(nullClient);
    hqplayer::lms::LmsHttpAdapter adapter(bridge, "127.0.0.1", 0);
    adapter.start();

    const auto port = adapter.boundPort();

    // Seed status: simulate HQPlayer reporting Playing, then Stopped.
    hqplayer::hqplayer::HQPlayerStatus playing;
    playing.state = hqplayer::hqplayer::HQPlayerState::Playing;
    bridge.updateCachedStatus(playing);

    auto status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.body().find("\"track_ended\":false") != std::string::npos,
               "track_ended should be false while playing");

    // Transition: Playing → Stopped.
    hqplayer::hqplayer::HQPlayerStatus stopped;
    stopped.state = hqplayer::hqplayer::HQPlayerState::Stopped;
    bridge.updateCachedStatus(stopped);

    // First GET after the transition should return track_ended:true and reset.
    status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.body().find("\"track_ended\":true") != std::string::npos,
               "track_ended should be true on first GET after Playing→Stopped");

    // Second GET should have reset the flag back to false.
    status = sendRequest(port, http::verb::get, "/lms/status");
    assertTrue(status.body().find("\"track_ended\":false") != std::string::npos,
               "track_ended should reset to false after being consumed");

    adapter.stop();
}

} // namespace

int main() {
    try {
        testEndpoints();
        testAlbumEndpoint();
        testTrackEndpoint();
        testTrackMetadataPassthrough();
        testTrackEndedFlag();
        testInvalidHostAtStart();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "LmsHttpAdapterIntegrationTests failed: " << e.what() << std::endl;
        return 1;
    }
}

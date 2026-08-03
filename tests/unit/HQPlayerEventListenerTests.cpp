#include "hqplayer/hqplayer/HQPlayerEventListener.hpp"
#include "hqplayer/hqplayer/HQPlayerTypes.hpp"
#include "hqplayer/config/Config.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// ---------------------------------------------------------------------------
// Mini mock HQPlayer server that accepts N connections and sends a
// pre-configured XML response on each, then closes.
// ---------------------------------------------------------------------------
class MockHQPlayerServer {
public:
    /// @param responses  One XML string per expected connection.  On connection
    ///                   N the server sends responses[N] then closes.
    explicit MockHQPlayerServer(std::vector<std::string> responses)
        : responses_(std::move(responses))
        , ioc_()
        , acceptor_(ioc_) {

        tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
        boost::system::error_code ec;
        acceptor_.open(tcp::v4(), ec);
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        acceptor_.bind(ep, ec);
        acceptor_.listen(10, ec);
        port_ = acceptor_.local_endpoint().port();

        serverThread_ = std::thread([this] { serve(); });
    }

    ~MockHQPlayerServer() {
        boost::system::error_code ec;
        acceptor_.cancel(ec);
        acceptor_.close(ec);
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    std::uint16_t port() const noexcept { return port_; }
    int connectionsAccepted() const noexcept { return connectionsAccepted_.load(); }

private:
    void serve() {
        boost::system::error_code ec;
        for (std::size_t i = 0; i < responses_.size(); ++i) {
            tcp::socket socket(ioc_);
            acceptor_.accept(socket, ec);
            if (ec) break;

            ++connectionsAccepted_;

            // Read incoming command (discard it — we don't need to validate here).
            std::array<char, 4096> buf{};
            socket.read_some(asio::buffer(buf), ec);

            // Send the configured response and close.
            asio::write(socket, asio::buffer(responses_[i]), ec);
            socket.shutdown(tcp::socket::shutdown_both, ec);
            socket.close(ec);
        }
    }

    std::vector<std::string> responses_;
    asio::io_context         ioc_;
    tcp::acceptor            acceptor_;
    std::uint16_t            port_{0};
    std::thread              serverThread_;
    std::atomic<int>         connectionsAccepted_{0};
};

// ---------------------------------------------------------------------------
// Helper: build a HQPlayerConfig pointing at a local mock server.
// ---------------------------------------------------------------------------
hqplayer::config::HQPlayerConfig makeConfig(std::uint16_t port) {
    hqplayer::config::HQPlayerConfig cfg;
    cfg.host       = "127.0.0.1";
    cfg.port       = port;
    cfg.timeout_ms = 2000;
    return cfg;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testDefaultCachedStatusIsStopped() {
    // Before start() the cached status should be the default (Stopped).
    const auto cfg = makeConfig(19990); // nothing listening — not started
    hqplayer::hqplayer::HQPlayerEventListener listener(cfg);

    const auto cached = listener.cachedStatus();
    assertTrue(cached.state == hqplayer::hqplayer::HQPlayerState::Stopped,
               "default cached state should be Stopped before start()");
}

void testCachedStatusUpdatedAfterFirstMessage() {
    // Server sends a single Playing status then closes.
    const std::string playingXml =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="1" active_rate="44100" active_bits="16" volume="-10"/>)";

    MockHQPlayerServer server({playingXml});
    const auto cfg = makeConfig(server.port());

    hqplayer::hqplayer::HQPlayerEventListener listener(cfg);
    listener.start();

    // Wait up to 2 s for the first status message to arrive.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (listener.cachedStatus().state == hqplayer::hqplayer::HQPlayerState::Playing) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    listener.stop();

    const auto cached = listener.cachedStatus();
    assertTrue(cached.state == hqplayer::hqplayer::HQPlayerState::Playing,
               "cached state should be Playing after server sent state=1");
    assertTrue(cached.samplerate_hz == 44100, "cached samplerate_hz should be 44100");
}

void testObserverCalledOnStatusMessage() {
    const std::string pausedXml =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="2" active_rate="0" active_bits="0" volume="0"/>)";

    MockHQPlayerServer server({pausedXml});
    const auto cfg = makeConfig(server.port());

    std::atomic<int> observerCalls{0};
    hqplayer::hqplayer::HQPlayerState lastState =
        hqplayer::hqplayer::HQPlayerState::Stopped;

    hqplayer::hqplayer::HQPlayerEventListener listener(
        cfg,
        [&](const hqplayer::hqplayer::HQPlayerStatus& s) {
            lastState = s.state;
            ++observerCalls;
        });

    listener.start();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (observerCalls.load() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    listener.stop();

    assertTrue(observerCalls.load() >= 1,
               "observer should have been called at least once");
    assertTrue(lastState == hqplayer::hqplayer::HQPlayerState::Paused,
               "observer should receive the Paused state");
}

void testReconnectsAfterConnectionClose() {
    // Server closes after the first response; listener should reconnect and
    // receive the second status message.
    const std::string stoppedXml =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="0" active_rate="0" active_bits="0" volume="0"/>)";
    const std::string playingXml =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="1" active_rate="96000" active_bits="24" volume="-6"/>)";

    // Two responses — the listener must reconnect after the first connection closes.
    MockHQPlayerServer server({stoppedXml, playingXml});
    const auto cfg = makeConfig(server.port());

    hqplayer::hqplayer::HQPlayerEventListener listener(cfg);
    listener.start();

    // Wait up to 4 s for two connections (1 s initial backoff between reconnects).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (server.connectionsAccepted() < 2 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    listener.stop();

    assertTrue(server.connectionsAccepted() >= 2,
               "listener should have reconnected after the first connection closed");
    assertTrue(listener.cachedStatus().state == hqplayer::hqplayer::HQPlayerState::Playing,
               "cached state should reflect the second (Playing) message");
}

void testStopIsClean() {
    // Server sends nothing — listener will fail to connect (port not used).
    // stop() must return promptly regardless.
    const auto cfg = makeConfig(19991); // nothing listening
    hqplayer::hqplayer::HQPlayerEventListener listener(cfg);
    listener.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    listener.stop();  // should not block
    listener.stop();  // second call should be idempotent
}

void testConnectErrorTriggersBackoff() {
    // Port 19992 has nothing listening — every connection attempt fails.
    const auto cfg = makeConfig(19992);

    std::atomic<int> observerCalls{0};
    hqplayer::hqplayer::HQPlayerEventListener listener(
        cfg,
        [&](const hqplayer::hqplayer::HQPlayerStatus&) { ++observerCalls; });

    listener.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    listener.stop();

    // Observer must not have been called since no connection succeeded.
    assertTrue(observerCalls.load() == 0,
               "observer should not be called when all connections fail");
    // Cached status should remain Stopped.
    assertTrue(listener.cachedStatus().state == hqplayer::hqplayer::HQPlayerState::Stopped,
               "cached state should remain Stopped when connection always fails");
}

void testMultipleFramesOnOneConnection() {
    // Two XML frames on the same connection without the server closing between them.
    // We simulate a short-lived persistent connection: send both frames then close.
    const std::string twoFrames =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="1" active_rate="44100" active_bits="16" volume="-10"/>)"
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="0" active_rate="0" active_bits="0" volume="0"/>)";

    MockHQPlayerServer server({twoFrames});
    const auto cfg = makeConfig(server.port());

    std::vector<hqplayer::hqplayer::HQPlayerState> observed;
    std::mutex observedMutex;

    hqplayer::hqplayer::HQPlayerEventListener listener(
        cfg,
        [&](const hqplayer::hqplayer::HQPlayerStatus& s) {
            std::lock_guard<std::mutex> lock(observedMutex);
            observed.push_back(s.state);
        });

    listener.start();

    // Wait up to 2 s for both frames to be processed.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            if (observed.size() >= 2) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    listener.stop();

    std::lock_guard<std::mutex> lock(observedMutex);
    assertTrue(observed.size() >= 2,
               "both XML frames on the same connection should be parsed");
    assertTrue(observed[0] == hqplayer::hqplayer::HQPlayerState::Playing,
               "first frame should be Playing");
    assertTrue(observed[1] == hqplayer::hqplayer::HQPlayerState::Stopped,
               "second frame should be Stopped");
}

} // namespace

int main() {
    try {
        testDefaultCachedStatusIsStopped();
        testCachedStatusUpdatedAfterFirstMessage();
        testObserverCalledOnStatusMessage();
        testReconnectsAfterConnectionClose();
        testStopIsClean();
        testConnectErrorTriggersBackoff();
        testMultipleFramesOnOneConnection();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HQPlayerEventListenerTests failed: " << e.what() << std::endl;
        return 1;
    }
}

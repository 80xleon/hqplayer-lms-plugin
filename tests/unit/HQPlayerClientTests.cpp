#include "hqplayer/config/Config.hpp"
#include "hqplayer/hqplayer/HQPlayerClient.hpp"
#include "hqplayer/hqplayer/HQPlayerTypes.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// ---------------------------------------------------------------------------
// Minimal TCP server that accepts one connection, reads the command, and
// sends a pre-configured XML response.
// ---------------------------------------------------------------------------
class MockHQPlayerServer {
public:
    explicit MockHQPlayerServer(const std::string& responseXml)
        : responseXml_(responseXml)
        , ioc_()
        , acceptor_(ioc_) {

        tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
        boost::system::error_code ec;
        acceptor_.open(tcp::v4(), ec);
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        acceptor_.bind(ep, ec);
        acceptor_.listen(1, ec);
        port_ = acceptor_.local_endpoint().port();

        serverThread_ = std::thread([this] { serveOne(); });
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

    const std::string& lastReceivedCommand() const noexcept { return lastCommand_; }

private:
    void serveOne() {
        boost::system::error_code ec;
        tcp::socket socket(ioc_);
        acceptor_.accept(socket, ec);
        if (ec) {
            return;
        }

        // Read the incoming command.
        std::array<char, 4096> buf{};
        const std::size_t n = socket.read_some(asio::buffer(buf), ec);
        if (n > 0) {
            lastCommand_.assign(buf.data(), n);
        }

        // Send configured response then close.
        asio::write(socket, asio::buffer(responseXml_), ec);
        socket.shutdown(tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }

    std::string       responseXml_;
    asio::io_context  ioc_;
    tcp::acceptor     acceptor_;
    std::uint16_t     port_{0};
    std::thread       serverThread_;
    std::string       lastCommand_;
};

// ---------------------------------------------------------------------------
// Build a HQPlayerConfig pointing at a local mock server.
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
void testGetStatusPlaying() {
    const std::string response =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="1" active_rate="44100" active_bits="16" volume="-10"/>)";

    MockHQPlayerServer server(response);
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(server.port()));

    const auto status = client.getStatus();

    assertTrue(status.state == hqplayer::hqplayer::HQPlayerState::Playing,
               "state should be Playing when server reports state=1");
    assertTrue(status.samplerate_hz == 44100, "samplerate_hz should be 44100");
    assertTrue(status.bitdepth == 16, "bitdepth should be 16");
    assertTrue(status.volume_db == -10.0f, "volume_db should be -10.0");
}

void testGetStatusStopped() {
    const std::string response =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="0" active_rate="0" active_bits="0" volume="0"/>)";

    MockHQPlayerServer server(response);
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(server.port()));

    const auto status = client.getStatus();

    assertTrue(status.state == hqplayer::hqplayer::HQPlayerState::Stopped,
               "state should be Stopped when server reports state=0");
}

void testGetStatusWithMetadata() {
    const std::string response =
        R"(<?xml version="1.0" encoding="UTF-8"?><Status state="1" active_rate="96000" active_bits="24"><metadata song="My Song" artist="My Artist" album="My Album"/></Status>)";

    MockHQPlayerServer server(response);
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(server.port()));

    const auto status = client.getStatus();

    assertTrue(status.state == hqplayer::hqplayer::HQPlayerState::Playing,
               "state should be Playing");
    assertTrue(status.track_title == "My Song", "track_title should match song attribute");
    assertTrue(status.artist == "My Artist", "artist should match");
    assertTrue(status.album == "My Album", "album should match");
    assertTrue(status.samplerate_hz == 96000, "samplerate_hz should be 96000");
    assertTrue(status.bitdepth == 24, "bitdepth should be 24");
}

void testPlayCommandSendsCorrectXml() {
    const std::string response =
        R"(<?xml version="1.0" encoding="UTF-8"?><Play result="OK"/>)";

    MockHQPlayerServer server(response);
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(server.port()));

    client.play();

    const auto& cmd = server.lastReceivedCommand();
    assertTrue(cmd.find("<Play/>") != std::string::npos,
               "play() should send <Play/> XML command");
}

void testStopCommandSendsCorrectXml() {
    const std::string response =
        R"(<?xml version="1.0" encoding="UTF-8"?><Stop result="OK"/>)";

    MockHQPlayerServer server(response);
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(server.port()));

    client.stop();

    const auto& cmd = server.lastReceivedCommand();
    assertTrue(cmd.find("<Stop/>") != std::string::npos,
               "stop() should send <Stop/> XML command");
}

void testNextCommandSendsCorrectXml() {
    const std::string response =
        R"(<?xml version="1.0" encoding="UTF-8"?><Next result="OK"/>)";

    MockHQPlayerServer server(response);
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(server.port()));

    client.next();

    const auto& cmd = server.lastReceivedCommand();
    assertTrue(cmd.find("<Next/>") != std::string::npos,
               "next() should send <Next/> XML command");
}

void testPrevCommandSendsCorrectXml() {
    const std::string response =
        R"(<?xml version="1.0" encoding="UTF-8"?><Prev result="OK"/>)";

    MockHQPlayerServer server(response);
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(server.port()));

    client.prev();

    const auto& cmd = server.lastReceivedCommand();
    assertTrue(cmd.find("<Prev/>") != std::string::npos,
               "prev() should send <Prev/> XML command");
}

void testConnectFailureThrows() {
    hqplayer::hqplayer::HQPlayerClient client(makeConfig(19999)); // nothing listening
    bool thrown = false;
    try {
        client.getStatus();
    } catch (const hqplayer::hqplayer::HQPlayerError& e) {
        thrown = true;
        const std::string msg = e.what();
        assertTrue(!msg.empty(), "HQPlayerError should have a message");
    }
    assertTrue(thrown, "getStatus() on unreachable host should throw HQPlayerError");
}

} // namespace

int main() {
    try {
        testGetStatusPlaying();
        testGetStatusStopped();
        testGetStatusWithMetadata();
        testPlayCommandSendsCorrectXml();
        testStopCommandSendsCorrectXml();
        testNextCommandSendsCorrectXml();
        testPrevCommandSendsCorrectXml();
        testConnectFailureThrows();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HQPlayerClientTests failed: " << e.what() << std::endl;
        return 1;
    }
}

#include "hqplayer/hqplayer/HQPlayerEventListener.hpp"
#include "hqplayer/hqplayer/HQPlayerClient.hpp"
#include "hqplayer/util/Logger.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>

namespace hqplayer::hqplayer {

namespace {

using ::hqplayer::util::LogLevel;
using ::hqplayer::util::Logger;

constexpr const char* kXmlDecl         = R"(<?xml version="1.0" encoding="UTF-8"?>)";
constexpr std::uint32_t kMaxBackoffMs   = 30'000;
constexpr std::uint32_t kInitialBackoffMs = 1'000;

/// Socket-level read timeout for the persistent listener connection (seconds).
/// Short so the read loop wakes up periodically to check the stop flag.
constexpr int kReadTimeoutSec = 1;

/// Try to extract one complete XML element from @p buffer.
///
/// Skips a leading <?xml … ?> declaration if present, then looks for the end
/// of the first root element (either self-closing @c /> or an explicit closing
/// tag @c </name>).  On success, @p frame receives the extracted text and that
/// text is removed from @p buffer.  Returns false when @p buffer does not yet
/// contain a complete element.
bool extractXmlFrame(std::string& buffer, std::string& frame) {
    // Need at least a '<'.
    const auto start = buffer.find('<');
    if (start == std::string::npos) {
        return false;
    }

    // Skip an XML declaration (<?xml … ?>).
    std::size_t elemStart = start;
    if (buffer.size() > start + 1 && buffer[start + 1] == '?') {
        const auto declEnd = buffer.find("?>", start + 2);
        if (declEnd == std::string::npos) {
            return false; // declaration not yet complete
        }
        const auto next = buffer.find_first_not_of(" \t\r\n", declEnd + 2);
        if (next == std::string::npos) {
            return false;
        }
        elemStart = next;
    }

    if (elemStart >= buffer.size() || buffer[elemStart] != '<') {
        return false;
    }

    // Extract the root element name.
    const auto nameStart = elemStart + 1;
    const auto nameEnd   = buffer.find_first_of(" />\t\r\n", nameStart);
    if (nameEnd == std::string::npos) {
        return false;
    }
    const std::string elemName = buffer.substr(nameStart, nameEnd - nameStart);
    if (elemName.empty()) {
        return false;
    }

    // Locate the end of the element: self-closing (/>) or explicit close (</name>).
    const auto selfClose = buffer.find("/>",          elemStart);
    const auto closeTag  = buffer.find("</" + elemName + ">", elemStart);

    std::size_t frameEnd = std::string::npos;
    if (selfClose != std::string::npos &&
        (closeTag == std::string::npos || selfClose < closeTag)) {
        frameEnd = selfClose + 2;                       // include the "/>"
    } else if (closeTag != std::string::npos) {
        frameEnd = closeTag + 3 + elemName.size();      // include "</" + name + ">"
    } else {
        return false; // element not yet complete
    }

    frame  = buffer.substr(0, frameEnd);
    buffer = buffer.substr(frameEnd);

    // Trim leading whitespace so the next call starts cleanly.
    const auto next = buffer.find_first_not_of(" \t\r\n");
    buffer = (next != std::string::npos) ? buffer.substr(next) : std::string{};

    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

HQPlayerEventListener::HQPlayerEventListener(const config::HQPlayerConfig& config,
                                              Observer observer)
    : config_(config), observer_(std::move(observer)) {}

HQPlayerEventListener::~HQPlayerEventListener() {
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void HQPlayerEventListener::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }
    {
        std::lock_guard<std::mutex> lock(cvMutex_);
        stopFlag_ = false;
    }
    worker_ = std::thread(&HQPlayerEventListener::listenLoop, this);
}

void HQPlayerEventListener::stop() noexcept {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // already stopped
    }
    {
        std::lock_guard<std::mutex> lock(cvMutex_);
        stopFlag_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

HQPlayerStatus HQPlayerEventListener::cachedStatus() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return cached_;
}

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------

void HQPlayerEventListener::updateCache(const HQPlayerStatus& status) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cached_ = status;
    }
    if (observer_) {
        try {
            observer_(status);
        } catch (const std::exception& e) {
            Logger::instance().log(LogLevel::Warn,
                "HQPlayerEventListener: observer threw: " + std::string(e.what()));
        }
    }
}

void HQPlayerEventListener::listenLoop() {
    Logger::instance().log(LogLevel::Info, "HQPlayerEventListener: starting");

    std::uint32_t backoffMs = kInitialBackoffMs;

    while (running_.load()) {
        try {
            runOneConnection();
            // Connection closed cleanly by HQPlayer — reset the backoff so we
            // reconnect promptly and avoid treating a normal EOF as an error.
            backoffMs = kInitialBackoffMs;
        } catch (const std::exception& e) {
            Logger::instance().log(LogLevel::Warn,
                "HQPlayerEventListener: " + std::string(e.what()) +
                " (retry in " + std::to_string(backoffMs) + "ms)");
        }

        if (!running_.load()) {
            break;
        }

        // Wait for the back-off period before reconnecting.
        {
            std::unique_lock<std::mutex> lock(cvMutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(backoffMs),
                         [this] { return stopFlag_; });
        }

        backoffMs = std::min(backoffMs * 2, kMaxBackoffMs);
    }

    Logger::instance().log(LogLevel::Info, "HQPlayerEventListener: stopped");
}

void HQPlayerEventListener::runOneConnection() {
    namespace asio = boost::asio;
    using tcp = asio::ip::tcp;

    asio::io_context ioc;
    tcp::socket socket(ioc);
    boost::system::error_code ec;

    // Resolve.
    tcp::resolver resolver(ioc);
    const auto endpoints =
        resolver.resolve(config_.host, std::to_string(config_.port), ec);
    if (ec) {
        throw HQPlayerError("Cannot resolve HQPlayer host '" + config_.host +
                            "': " + ec.message());
    }

    // Connect.
    asio::connect(socket, endpoints, ec);
    if (ec) {
        throw HQPlayerError("Cannot connect to HQPlayer " + config_.host +
                            ":" + std::to_string(config_.port) + ": " + ec.message());
    }

    // Short SO_RCVTIMEO so read_some wakes up periodically to check running_.
    struct timeval tvRecv{};
    tvRecv.tv_sec  = static_cast<time_t>(kReadTimeoutSec);
    tvRecv.tv_usec = 0;
    ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tvRecv, sizeof(tvRecv));

    // Longer SO_SNDTIMEO matching the configured command timeout.
    struct timeval tvSend{};
    tvSend.tv_sec  = static_cast<time_t>(config_.timeout_ms / 1000);
    tvSend.tv_usec = static_cast<suseconds_t>((config_.timeout_ms % 1000) * 1000);
    ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tvSend, sizeof(tvSend));

    // Send initial <Status/> to request the current state.
    const std::string cmd = std::string(kXmlDecl) + "<Status/>";
    asio::write(socket, asio::buffer(cmd), ec);
    if (ec) {
        throw HQPlayerError("Send <Status/> failed: " + ec.message());
    }

    Logger::instance().log(LogLevel::Debug,
        "HQPlayerEventListener: connected to " + config_.host +
        ":" + std::to_string(config_.port));

    // Read loop: accumulate bytes and process each complete XML frame.
    std::string readBuf;
    std::array<char, 4096> ioBuf{};

    while (running_.load()) {
        const std::size_t n = socket.read_some(asio::buffer(ioBuf), ec);
        if (n > 0) {
            readBuf.append(ioBuf.data(), n);
        }

        if (ec == asio::error::eof || ec == asio::error::connection_reset) {
            // HQPlayer closed the connection (expected after responding to
            // <Status/> if it does not support persistent push).
            Logger::instance().log(LogLevel::Debug,
                "HQPlayerEventListener: connection closed by HQPlayer");
            break; // triggers reconnect in listenLoop()
        }

        if (ec) {
            if (ec.value() == EAGAIN || ec.value() == EWOULDBLOCK) {
                // Read timeout — no data within kReadTimeoutSec.  Loop back to
                // check running_ and try again.
                continue;
            }
            Logger::instance().log(LogLevel::Debug,
                "HQPlayerEventListener: read error: " + ec.message());
            break;
        }

        // Parse any complete XML frames from the accumulated buffer.
        std::string frame;
        while (extractXmlFrame(readBuf, frame)) {
            Logger::instance().log(LogLevel::Debug,
                "HQPlayerEventListener << " +
                frame.substr(0, std::min(frame.size(), std::size_t{200})));
            try {
                const auto status = HQPlayerClient::parseStatusXml(frame);
                updateCache(status);
            } catch (const std::exception& e) {
                Logger::instance().log(LogLevel::Debug,
                    "HQPlayerEventListener: XML parse skipped: " +
                    std::string(e.what()));
            }
        }
    }

    boost::system::error_code ecClose;
    socket.shutdown(tcp::socket::shutdown_both, ecClose);
    socket.close(ecClose);
}

} // namespace hqplayer::hqplayer

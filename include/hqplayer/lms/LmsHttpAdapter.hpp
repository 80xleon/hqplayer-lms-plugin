#pragma once

#include "hqplayer/lms/LmsBridge.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace hqplayer::lms {

/// HTTP server that exposes the LMS bridge over a local TCP socket.
///
/// Listens on @p host : @p port and dispatches incoming HTTP requests to
/// LmsBridge.  Supported endpoints:
///
/// | Method | Path            | Description                                   |
/// |--------|-----------------|-----------------------------------------------|
/// | GET    | /lms/status     | Returns playback state as JSON.               |
/// | GET    | /lms/events     | Long-poll: blocks until track-ended or 30 s.  |
/// | POST   | /lms/play       | Start / resume playback.                      |
/// | POST   | /lms/pause      | Pause playback.                               |
/// | POST   | /lms/stop       | Stop playback.                                |
/// | POST   | /lms/track      | Load a track URI via PlayNextUri.             |
/// | POST   | /lms/album      | Reserved — returns 501 Not Implemented.       |
///
/// The @c /lms/events handler spawns a dedicated @c std::async thread per
/// request so the accept loop is never blocked while a long-poll is pending.
/// Outstanding event threads are joined cleanly in stop().
///
/// Thread-safe: start() and stop() may be called from any thread.
class LmsHttpAdapter final {
public:
    /// Construct the adapter.
    ///
    /// @param bridge  LmsBridge instance to dispatch commands to (must outlive
    ///                this adapter).
    /// @param host    Bind address for the HTTP server (IPv4 or IPv6 string).
    /// @param port    TCP port to listen on (0 = OS-assigned ephemeral port,
    ///                useful for tests).
    LmsHttpAdapter(LmsBridge& bridge, std::string host, std::uint16_t port);

    /// Stops the adapter if running, then destroys the object.
    ~LmsHttpAdapter();

    // Non-copyable, non-movable.
    LmsHttpAdapter(const LmsHttpAdapter&)            = delete;
    LmsHttpAdapter& operator=(const LmsHttpAdapter&) = delete;

    /// Bind the socket, start the accept loop, and block until the server is
    /// ready to accept connections.
    ///
    /// @throws std::runtime_error if the socket cannot be bound or opened.
    /// Safe to call only once without an intervening stop().
    void start();

    /// Gracefully shut down the accept loop and all outstanding /lms/events
    /// long-poll threads, then block until everything has joined.
    ///
    /// Safe to call multiple times and from any thread.
    void stop();

    /// @return The actual TCP port the server is listening on.
    ///
    /// Useful when @p port was 0 (OS-assigned) to discover the bound port.
    /// Returns 0 before start() completes successfully.
    [[nodiscard]] std::uint16_t boundPort() const;

private:
    void run();
    std::string statusJson() const;

    LmsBridge& bridge_;
    std::string host_;
    std::uint16_t port_;

    mutable std::mutex state_mutex_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::uint16_t bound_port_{0};

    std::condition_variable start_cv_;
    bool start_done_{false};
    std::string start_error_;

    /// Futures for outstanding /lms/events long-poll threads.
    std::mutex                     event_futures_mutex_;
    std::vector<std::future<void>> event_futures_;
};

} // namespace hqplayer::lms

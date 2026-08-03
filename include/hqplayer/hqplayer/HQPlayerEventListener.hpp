#pragma once

#include "hqplayer/config/Config.hpp"
#include "hqplayer/hqplayer/HQPlayerTypes.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace hqplayer::hqplayer {

/// Persistent-connection event listener that keeps a live HQPlayerStatus cache.
///
/// Opens a single TCP connection to HQPlayer Embedded (port 4321).  After
/// connecting it sends an initial <Status/> query and then reads XML messages
/// from the socket continuously.  Each complete XML frame is parsed and the
/// caller-supplied observer is invoked.
///
/// This replaces the periodic-polling approach of HQPlayerSync:
///  - If HQPlayer sends unsolicited state-change notifications on the same
///    connection, they are processed immediately (true push model).
///  - If HQPlayer closes the connection after the first response (current
///    behaviour), the listener reconnects automatically and re-queries,
///    achieving the same result as polling but without a fixed timer.
///
/// Reconnection uses exponential back-off (initial 1 s, max 30 s) so that
/// HQPlayer restarts are handled gracefully.
///
/// Lifecycle: call start() once, stop() to terminate cleanly.  The
/// destructor calls stop() automatically.
class HQPlayerEventListener final {
public:
    /// Callback type invoked on each received HQPlayerStatus.
    using Observer = std::function<void(const HQPlayerStatus&)>;

    /// @param config    HQPlayer connection settings (host, port, timeout_ms).
    /// @param observer  Callback invoked on each status update.  May be empty.
    explicit HQPlayerEventListener(const config::HQPlayerConfig& config,
                                   Observer observer = {});

    ~HQPlayerEventListener();

    // Non-copyable, non-movable.
    HQPlayerEventListener(const HQPlayerEventListener&)            = delete;
    HQPlayerEventListener& operator=(const HQPlayerEventListener&) = delete;

    /// Start the background listener thread.
    /// Must not be called more than once without an intervening stop().
    void start();

    /// Stop the background listener thread and block until it exits.
    /// Safe to call multiple times.
    void stop() noexcept;

    /// @return The most recently received HQPlayerStatus.
    ///         Returns a default-constructed (Stopped) status before the
    ///         first successful message.
    [[nodiscard]] HQPlayerStatus cachedStatus() const noexcept;

private:
    void listenLoop();
    void runOneConnection();
    void updateCache(const HQPlayerStatus& status);

    config::HQPlayerConfig config_;
    Observer               observer_;

    mutable std::mutex mutex_;
    HQPlayerStatus     cached_{};

    std::mutex              cvMutex_;
    std::condition_variable cv_;
    bool                    stopFlag_{false};

    std::atomic<bool> running_{false};
    std::thread       worker_;
};

} // namespace hqplayer::hqplayer

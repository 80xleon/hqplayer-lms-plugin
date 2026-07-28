#pragma once

#include "hqplayer/hqplayer/HQPlayerTypes.hpp"
#include "hqplayer/hqplayer/IHQPlayerClient.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace hqplayer::hqplayer {

/// Background poller that keeps a fresh HQPlayerStatus cache.
///
/// Periodically calls IHQPlayerClient::getStatus() and:
///  - stores the result in an in-process cache,
///  - invokes a caller-supplied observer callback on every update.
///
/// On network errors the poller backs off exponentially (doubling each
/// failure up to a configured cap), then resumes normal polling.
///
/// Lifecycle: call start() once, stop() to terminate cleanly.  The
/// destructor calls stop() automatically.
class HQPlayerSync final {
public:
    /// Callback type invoked with the fresh status after each successful poll.
    using Observer = std::function<void(const HQPlayerStatus&)>;

    /// @param client           Client used for status queries.
    /// @param pollIntervalMs   Normal interval between polls (milliseconds).
    /// @param observer         Callback invoked after each successful poll.
    ///                         May be empty (no-op).
    HQPlayerSync(IHQPlayerClient& client,
                 std::uint32_t    pollIntervalMs,
                 Observer         observer = {});

    ~HQPlayerSync();

    // Non-copyable, non-movable.
    HQPlayerSync(const HQPlayerSync&)            = delete;
    HQPlayerSync& operator=(const HQPlayerSync&) = delete;

    /// Start the background polling thread.
    /// Must not be called more than once without an intervening stop().
    void start();

    /// Stop the background polling thread and block until it exits.
    /// Safe to call multiple times.
    void stop() noexcept;

    /// @return The most recently polled HQPlayerStatus.
    ///         Returns a default-constructed (Stopped) status before the
    ///         first successful poll.
    [[nodiscard]] HQPlayerStatus cachedStatus() const noexcept;

private:
    void pollLoop();
    void updateCache(const HQPlayerStatus& status);

    IHQPlayerClient&  client_;
    std::uint32_t     pollIntervalMs_;
    Observer          observer_;

    mutable std::mutex      mutex_;
    HQPlayerStatus          cached_{};

    std::mutex              cvMutex_;
    std::condition_variable cv_;
    bool                    stopFlag_{false};

    std::atomic<bool>       running_{false};
    std::thread             worker_;
};

} // namespace hqplayer::hqplayer

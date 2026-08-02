#pragma once

#include "hqplayer/hqplayer/HQPlayerTypes.hpp"
#include "hqplayer/hqplayer/IHQPlayerClient.hpp"
#include "hqplayer/lms/ILmsBridge.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace hqplayer::lms {

/// Bridge between the LMS HTTP adapter and HQPlayer.
///
/// Commands arriving from the LMS HTTP adapter are forwarded to the
/// IHQPlayerClient.  The most-recent status is kept in an internal
/// cache that is updated either:
///  - optimistically on each handled command (immediate feedback), or
///  - externally via updateCachedStatus() (called by HQPlayerEventListener on
///    each received notification).
///
/// When HQPlayerEventListener detects a Playing→Stopped transition, it calls
/// updateCachedStatus() which sets an internal flag and notifies any thread
/// waiting in waitForTrackEnded().  The LMS HTTP adapter's /lms/events
/// long-poll endpoint blocks in waitForTrackEnded() until the flag fires,
/// then returns a JSON response to the Perl plugin.
///
/// Thread-safe.
class LmsBridge final : public ILmsBridge {
public:
    /// @param client  HQPlayer control client (must outlive LmsBridge).
    explicit LmsBridge(::hqplayer::hqplayer::IHQPlayerClient& client);

    /// Handle a command from the LMS HTTP adapter.
    ///
    /// Play/Pause/Stop are forwarded to the HQPlayer client.
    /// Next/prev navigation is handled at the LMS layer (Player.pm advances
    /// the LMS queue, which triggers a load() call with the new track URI).
    /// Errors are logged but not propagated so the HTTP contract is always stable.
    void handleCommand(LmsCommand command) override;

    /// @return The most recently cached LMS status.
    LmsStatus currentStatus() const override;

    /// Load a single track by filesystem path (or URL) and start playback.
    ///
    /// @p meta carries optional display metadata (title, artist, album) that
    /// is forwarded to HQPlayer Embedded via PlayNextUri attributes.
    ///
    /// @throws std::invalid_argument if @p filePath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    void handleTrackLoad(const std::string& filePath,
                         const ::hqplayer::hqplayer::TrackMetadata& meta = {}) override;

    /// Attempt to play an album by filesystem path.
    ///
    /// Validates @p albumPath (non-empty) then forwards to the HQPlayer client.
    /// @throws std::invalid_argument if @p albumPath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    void handleAlbumPlay(const std::string& albumPath) override;

    /// Update the cached status from an external source (HQPlayerEventListener).
    ///
    /// Detects Playing→Stopped transitions and sets an internal flag, then
    /// notifies any threads waiting in waitForTrackEnded().
    void updateCachedStatus(const ::hqplayer::hqplayer::HQPlayerStatus& status);

    /// Atomically read and reset the track-ended flag.
    ///
    /// Returns true once when a Playing→Stopped transition was detected since
    /// the last call.  Subsequent calls return false until the next transition.
    /// Used by LmsHttpAdapter when building the /lms/status JSON response.
    bool consumeTrackEnded();

    /// Block until a track-ended event is detected or @p timeout elapses.
    ///
    /// Returns true when a Playing→Stopped transition is detected (and
    /// consumes the flag so it is not reported a second time).  Returns false
    /// on timeout.  Also returns false immediately when abortWaits() has been
    /// called.
    ///
    /// Used by the /lms/events long-poll endpoint so the Perl plugin is
    /// notified the instant a track ends rather than on the next poll cycle.
    bool waitForTrackEnded(std::chrono::milliseconds timeout);

    /// Wake up all threads currently blocked in waitForTrackEnded().
    ///
    /// Called by LmsHttpAdapter::stop() to unblock any outstanding long-poll
    /// connections before the adapter tears down.
    void abortWaits();

private:
    void setOptimisticState(const std::string& state);

    ::hqplayer::hqplayer::IHQPlayerClient& client_;
    mutable std::mutex                     mutex_;
    LmsStatus                              cached_{};
    bool                                   track_ended_flag_{false};
    bool                                   abort_waits_{false};
    std::condition_variable                track_ended_cv_;
};

} // namespace hqplayer::lms

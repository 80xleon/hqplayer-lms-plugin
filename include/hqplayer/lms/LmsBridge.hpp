#pragma once

#include "hqplayer/hqplayer/HQPlayerTypes.hpp"
#include "hqplayer/hqplayer/IHQPlayerClient.hpp"
#include "hqplayer/lms/ILmsBridge.hpp"

#include <mutex>

namespace hqplayer::lms {

/// Bridge between the LMS HTTP adapter and HQPlayer.
///
/// Commands arriving from the LMS HTTP adapter are forwarded to the
/// IHQPlayerClient.  The most-recent status is kept in an internal
/// cache that is updated either:
///  - optimistically on each handled command (immediate feedback), or
///  - externally via updateCachedStatus() (called by HQPlayerSync on
///    each successful poll).
///
/// Thread-safe.
class LmsBridge final : public ILmsBridge {
public:
    /// @param client  HQPlayer control client (must outlive LmsBridge).
    explicit LmsBridge(::hqplayer::hqplayer::IHQPlayerClient& client);

    /// Handle a command from the LMS HTTP adapter.
    ///
    /// Play/Pause/Stop/NextTrack/PrevTrack are forwarded to the HQPlayer client.
    /// Errors are logged but not propagated so the HTTP contract is always stable.
    void handleCommand(LmsCommand command) override;

    /// @return The most recently cached LMS status.
    LmsStatus currentStatus() const override;

    /// Load a single track by filesystem path and start playback.
    ///
    /// @throws std::invalid_argument if @p filePath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    void handleTrackLoad(const std::string& filePath) override;

    /// Attempt to play an album by filesystem path.
    ///
    /// Validates @p albumPath (non-empty) then forwards to the HQPlayer client.
    /// @throws std::invalid_argument if @p albumPath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    void handleAlbumPlay(const std::string& albumPath) override;

    /// Update the cached status from an external source (e.g. HQPlayerSync).
    ///
    /// Detects Playing→Stopped transitions and sets an internal flag that
    /// consumeTrackEnded() can read.
    void updateCachedStatus(const ::hqplayer::hqplayer::HQPlayerStatus& status);

    /// Atomically read and reset the track-ended flag.
    ///
    /// Returns true once when a Playing→Stopped transition was detected since
    /// the last call.  Subsequent calls return false until the next transition.
    /// Called by LmsHttpAdapter when building the /lms/status JSON response.
    bool consumeTrackEnded();

private:
    void setOptimisticState(const std::string& state);

    ::hqplayer::hqplayer::IHQPlayerClient& client_;
    mutable std::mutex                     mutex_;
    LmsStatus                              cached_{};
    bool                                   track_ended_flag_{false};
};

} // namespace hqplayer::lms

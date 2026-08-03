#pragma once

#include "hqplayer/hqplayer/HQPlayerTypes.hpp"
#include "hqplayer/lms/LmsTypes.hpp"

#include <string>

namespace hqplayer::lms {

/// Pure interface between the LMS HTTP adapter and a HQPlayer back-end.
///
/// Implementations translate LMS transport commands and track-load requests
/// into the appropriate HQPlayer API calls and maintain a cached status that
/// the HTTP adapter can return without blocking on a live query.
class ILmsBridge {
public:
    virtual ~ILmsBridge() = default;

    /// Dispatch a transport command (Play, Pause, Stop) to HQPlayer.
    virtual void handleCommand(LmsCommand command) = 0;

    /// @return A snapshot of the most recently cached playback status.
    virtual LmsStatus currentStatus() const = 0;

    /// Load a single track by filesystem path (or URL) and start playback.
    ///
    /// @p meta carries optional display metadata (title, artist, album) that
    /// is forwarded to HQPlayer Embedded so it can show Now Playing info.
    ///
    /// @throws std::invalid_argument if @p filePath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    virtual void handleTrackLoad(const std::string& filePath,
                                 const ::hqplayer::hqplayer::TrackMetadata& meta = {}) = 0;

    /// Attempt to play an album by path.
    ///
    /// Validates @p albumPath and forwards to the HQPlayer client.
    /// @throws std::invalid_argument if @p albumPath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    virtual void handleAlbumPlay(const std::string& albumPath) = 0;
};

} // namespace hqplayer::lms

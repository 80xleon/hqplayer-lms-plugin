#pragma once

#include "hqplayer/lms/LmsTypes.hpp"

#include <string>

namespace hqplayer::lms {

class ILmsBridge {
public:
    virtual ~ILmsBridge() = default;

    virtual void handleCommand(LmsCommand command) = 0;
    virtual LmsStatus currentStatus() const = 0;

    /// Load a single track by filesystem path and start playback.
    ///
    /// Validates @p filePath (non-empty) then forwards to the HQPlayer client.
    /// @throws std::invalid_argument if @p filePath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    virtual void handleTrackLoad(const std::string& filePath) = 0;

    /// Attempt to play an album by path.
    ///
    /// Validates @p albumPath and forwards to the HQPlayer client.
    /// @throws std::invalid_argument if @p albumPath is empty.
    /// @throws std::runtime_error   if the HQPlayer backend cannot fulfil the request.
    virtual void handleAlbumPlay(const std::string& albumPath) = 0;
};

} // namespace hqplayer::lms

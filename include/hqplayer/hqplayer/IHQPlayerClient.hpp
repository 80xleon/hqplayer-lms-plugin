#pragma once

#include "hqplayer/hqplayer/HQPlayerTypes.hpp"

namespace hqplayer::hqplayer {

/// Pure interface for controlling HQPlayer.
///
/// All implementations must be thread-safe: play(), pause(), stop() and
/// getStatus() may be called concurrently from different threads.
class IHQPlayerClient {
public:
    virtual ~IHQPlayerClient() = default;

    /// Start HQPlayer playback.
    /// @throws HQPlayerError on network or protocol error.
    virtual void play() = 0;

    /// Pause HQPlayer playback.
    /// @throws HQPlayerError on network or protocol error.
    virtual void pause() = 0;

    /// Stop HQPlayer playback.
    /// @throws HQPlayerError on network or protocol error.
    virtual void stop() = 0;

    /// Skip to the next track in the current HQPlayer playlist.
    /// @throws HQPlayerError on network or protocol error.
    virtual void next() = 0;

    /// Skip to the previous track in the current HQPlayer playlist.
    /// @throws HQPlayerError on network or protocol error.
    virtual void prev() = 0;

    /// Query the current HQPlayer status.
    /// @return Current HQPlayerStatus.
    /// @throws HQPlayerError on network or protocol error.
    virtual HQPlayerStatus getStatus() = 0;
};

} // namespace hqplayer::hqplayer

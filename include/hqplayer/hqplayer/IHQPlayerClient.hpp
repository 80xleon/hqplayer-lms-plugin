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

    /// Load a single audio file and begin playback.
    ///
    /// Sends the HQPlayer XML API load command with @p filePath as the source,
    /// then issues a Play command.  The exact XML command is:
    ///   <Load src="<filePath>"/>
    /// followed by a <Play/>.
    ///
    /// @note The @p filePath must be an absolute path accessible by the
    ///       HQPlayer Embedded process (e.g. a shared NAS mount that appears
    ///       at the same path on both the LMS host and the HQPlayer host).
    /// @throws HQPlayerError on network or protocol error.
    virtual void loadTrack(const std::string& filePath) = 0;

    /// Query the current HQPlayer status.
    /// @return Current HQPlayerStatus.
    /// @throws HQPlayerError on network or protocol error.
    virtual HQPlayerStatus getStatus() = 0;
};

} // namespace hqplayer::hqplayer

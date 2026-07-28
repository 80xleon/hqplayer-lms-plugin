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

    /// Queue @p uri as the next file to play (HQPe --play-next-uri semantics).
    ///
    /// Sends the HQPlayer XML API command:
    ///   <PlayNextUri uri="<uri>"/>
    ///
    /// Behaviour mirrors HQPlayer Embedded's --play-next-uri option:
    ///  - When HQPlayer is stopped:  starts playing @p uri immediately.
    ///  - When HQPlayer is playing:  queues @p uri for gapless transition after
    ///    the current track ends.
    ///
    /// This is the primary method used by the LMS bridge for all track loads,
    /// replacing the older loadTrack() / <Load> + <Play> two-command sequence.
    ///
    /// @note @p uri must be an absolute filesystem path accessible by the
    ///       HQPlayer Embedded process (e.g. a shared NAS mount that appears
    ///       at the same path on both the LMS host and the HQPlayer host).
    /// @throws HQPlayerError on network or protocol error.
    virtual void playNextUri(const std::string& uri) = 0;

    /// Load a single audio file and begin playback immediately.
    ///
    /// Sends <Load src="<filePath>"/> then <Play/> to HQPlayer Embedded.
    /// Superseded by playNextUri() for LMS-driven playback; retained for
    /// backward compatibility and direct use cases.
    /// @throws HQPlayerError on network or protocol error.
    virtual void loadTrack(const std::string& filePath) = 0;

    /// Query the current HQPlayer status.
    /// @return Current HQPlayerStatus.
    /// @throws HQPlayerError on network or protocol error.
    virtual HQPlayerStatus getStatus() = 0;
};

} // namespace hqplayer::hqplayer

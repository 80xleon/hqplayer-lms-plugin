#pragma once

#include <cstdint>
#include <string>

namespace hqplayer::lms {

/// Commands that the LMS HTTP adapter can dispatch to LmsBridge.
enum class LmsCommand {
    Play,
    Pause,
    Stop,
    Status
};

/// Snapshot of the current playback state returned by LmsBridge.
///
/// Consumed by LmsHttpAdapter when building the /lms/status JSON response.
struct LmsStatus {
    /// Current playback state: "playing", "paused", or "stopped".
    std::string state{"stopped"};

    /// Title of the currently loaded track (may be empty).
    std::string track_title{};

    /// Output sample rate in Hz (0 when stopped).
    std::uint32_t samplerate_hz{0};

    /// Output bit depth (0 when stopped; 1 indicates DSD).
    std::uint32_t bitdepth{0};

    /// True when a Playing→Stopped transition was detected since the last call
    /// to LmsBridge::consumeTrackEnded().  The Perl plugin is notified of this
    /// event via the /lms/events long-poll endpoint (see LmsBridge::waitForTrackEnded()).
    bool track_ended{false};
};

} // namespace hqplayer::lms

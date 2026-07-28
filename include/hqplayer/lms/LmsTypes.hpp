#pragma once

#include <cstdint>
#include <string>

namespace hqplayer::lms {

enum class LmsCommand {
    Play,
    Pause,
    Stop,
    Status
};

struct LmsStatus {
    std::string state{"stopped"};
    std::string track_title{};
    std::uint32_t samplerate_hz{0};
    std::uint32_t bitdepth{0};
    /// True when a Playing→Stopped transition was detected since the last
    /// call to LmsBridge::consumeTrackEnded().  The Perl plugin polls this
    /// flag to advance the LMS queue when HQPlayer finishes a track.
    bool track_ended{false};
};

} // namespace hqplayer::lms

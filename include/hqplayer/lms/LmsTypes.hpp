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
};

} // namespace hqplayer::lms

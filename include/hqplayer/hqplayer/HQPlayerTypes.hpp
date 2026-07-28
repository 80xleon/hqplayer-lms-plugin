#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace hqplayer::hqplayer {

/// Playback state as reported by HQPlayer.
///
/// Values match the HQPlayer XML TCP API (port 4321):
/// 0 = stopped, 1 = playing, 2 = paused.
enum class HQPlayerState : int {
    Stopped = 0,
    Playing = 1,
    Paused  = 2,
};

/// Full playback status returned by HQPlayer.
struct HQPlayerStatus {
    HQPlayerState state{HQPlayerState::Stopped};

    /// Current track title (may be empty when stopped).
    std::string track_title{};

    /// Current artist (may be empty).
    std::string artist{};

    /// Current album (may be empty).
    std::string album{};

    /// Output sample rate in Hz (0 when stopped).
    std::uint32_t samplerate_hz{0};

    /// Output bit depth (0 when stopped; 1 indicates DSD).
    std::uint32_t bitdepth{0};

    /// Current volume in dB (typically –40 to 0, or 0 when unknown).
    float volume_db{0.0f};

    /// True when HQPlayer outputs a DSD (1-bit) stream.
    bool is_dsd{false};
};

/// Exception thrown when HQPlayer is unreachable or returns an error.
class HQPlayerError : public std::runtime_error {
public:
    explicit HQPlayerError(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace hqplayer::hqplayer

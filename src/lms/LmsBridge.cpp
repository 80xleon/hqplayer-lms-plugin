#include "hqplayer/lms/LmsBridge.hpp"

#include "hqplayer/util/Logger.hpp"

#include <stdexcept>

namespace hqplayer::lms {

using ::hqplayer::util::LogLevel;
using ::hqplayer::util::Logger;

namespace {

/// Map a HQPlayerState to the LMS status state string.
std::string stateToString(::hqplayer::hqplayer::HQPlayerState s) noexcept {
    switch (s) {
    case ::hqplayer::hqplayer::HQPlayerState::Playing: return "playing";
    case ::hqplayer::hqplayer::HQPlayerState::Paused:  return "paused";
    default:                                            return "stopped";
    }
}

} // namespace

LmsBridge::LmsBridge(::hqplayer::hqplayer::IHQPlayerClient& client)
    : client_(client) {}

void LmsBridge::handleCommand(LmsCommand command) {
    switch (command) {
    case LmsCommand::Play:
        Logger::instance().log(LogLevel::Info, "LmsBridge: Play");
        try {
            client_.play();
            setOptimisticState("playing");
        } catch (const std::exception& e) {
            Logger::instance().log(LogLevel::Warn,
                "LmsBridge: Play failed — " + std::string(e.what()));
        }
        break;

    case LmsCommand::Pause:
        Logger::instance().log(LogLevel::Info, "LmsBridge: Pause");
        try {
            client_.pause();
            setOptimisticState("paused");
        } catch (const std::exception& e) {
            Logger::instance().log(LogLevel::Warn,
                "LmsBridge: Pause failed — " + std::string(e.what()));
        }
        break;

    case LmsCommand::Stop:
        Logger::instance().log(LogLevel::Info, "LmsBridge: Stop");
        try {
            client_.stop();
            setOptimisticState("stopped");
        } catch (const std::exception& e) {
            Logger::instance().log(LogLevel::Warn,
                "LmsBridge: Stop failed — " + std::string(e.what()));
        }
        break;

    case LmsCommand::Status:
        // Status is maintained by HQPlayerEventListener; nothing to do here.
        break;
    }
}

LmsStatus LmsBridge::currentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cached_;
}

void LmsBridge::updateCachedStatus(const ::hqplayer::hqplayer::HQPlayerStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string newState = stateToString(status.state);

    // Detect Playing→Stopped transition and notify waiting long-poll threads.
    if (cached_.state == "playing" && newState == "stopped") {
        track_ended_flag_ = true;
        Logger::instance().log(LogLevel::Info,
            "LmsBridge: Playing→Stopped transition detected — track ended");
        track_ended_cv_.notify_all();
    }

    cached_.state         = newState;
    cached_.track_title   = status.track_title;
    cached_.samplerate_hz = status.samplerate_hz;
    cached_.bitdepth      = status.bitdepth;
}

void LmsBridge::setOptimisticState(const std::string& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    cached_.state = state;
}

void LmsBridge::handleAlbumPlay(const std::string& albumPath) {
    if (albumPath.empty()) {
        throw std::invalid_argument("Album path must not be empty");
    }

    Logger::instance().log(LogLevel::Info,
        "LmsBridge: PlayAlbum '" + albumPath + "'");

    // TODO: HQPlayer Embedded XML API does not expose a native album-play command.
    // When the HQPlayer playlist API is confirmed and documented, replace this
    // with a direct client_.playAlbum(albumPath) call.
    throw std::runtime_error(
        "Album playback is not yet supported: HQPlayer Embedded does not expose "
        "a native album-play XML command. Load a playlist via HQPlayer's own interface.");
}

void LmsBridge::handleTrackLoad(const std::string& filePath,
                                const ::hqplayer::hqplayer::TrackMetadata& meta) {
    if (filePath.empty()) {
        throw std::invalid_argument("Track path must not be empty");
    }

    Logger::instance().log(LogLevel::Info,
        "LmsBridge: PlayNextUri '" + filePath + "'");

    // Reset the track-ended flag: a new track is starting so any previous
    // "track ended" signal is no longer relevant.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        track_ended_flag_ = false;
    }

    try {
        // If HQPlayer is currently playing, stop first so the newly selected
        // track starts immediately instead of being queued behind the current one.
        bool isPlaying = false;
        try {
            const auto status = client_.getStatus();
            isPlaying = (status.state == ::hqplayer::hqplayer::HQPlayerState::Playing);
        } catch (const std::exception& e) {
            Logger::instance().log(
                LogLevel::Warn,
                "LmsBridge: status probe before track load failed — " + std::string(e.what()));
        }

        if (isPlaying) {
            Logger::instance().log(
                LogLevel::Info,
                "LmsBridge: stopping current playback before loading new track");
            client_.stop();
        }

        // Use playNextUri to start playback for stopped state.
        // Metadata (title, artist, album) is forwarded so HQPlayer can display
        // Now Playing information.
        client_.playNextUri(filePath, meta);
        setOptimisticState("playing");
    } catch (const std::exception& e) {
        Logger::instance().log(LogLevel::Warn,
            "LmsBridge: PlayNextUri failed — " + std::string(e.what()));
        throw;
    }
}

bool LmsBridge::consumeTrackEnded() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool v = track_ended_flag_;
    track_ended_flag_ = false;
    return v;
}

bool LmsBridge::waitForTrackEnded(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool signaled = track_ended_cv_.wait_for(lock, timeout, [this] {
        return track_ended_flag_ || abort_waits_;
    });
    if (signaled && track_ended_flag_ && !abort_waits_) {
        track_ended_flag_ = false;
        return true;
    }
    return false;
}

void LmsBridge::abortWaits() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_waits_ = true;
    }
    track_ended_cv_.notify_all();
}

} // namespace hqplayer::lms

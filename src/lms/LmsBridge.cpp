#include "hqplayer/lms/LmsBridge.hpp"

#include "hqplayer/util/Logger.hpp"

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
        // Status is maintained by HQPlayerSync; nothing to do here.
        break;
    }
}

LmsStatus LmsBridge::currentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cached_;
}

void LmsBridge::updateCachedStatus(const ::hqplayer::hqplayer::HQPlayerStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    cached_.state         = stateToString(status.state);
    cached_.track_title   = status.track_title;
    cached_.samplerate_hz = status.samplerate_hz;
    cached_.bitdepth      = status.bitdepth;
}

void LmsBridge::setOptimisticState(const std::string& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    cached_.state = state;
}

} // namespace hqplayer::lms

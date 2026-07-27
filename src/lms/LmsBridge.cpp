#include "hqplayer/lms/LmsBridge.hpp"

namespace hqplayer::lms {

void LmsBridge::handleCommand(LmsCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);

    switch (command) {
    case LmsCommand::Play:
        status_.state = "playing";
        break;
    case LmsCommand::Pause:
        status_.state = "paused";
        break;
    case LmsCommand::Stop:
        status_.state = "stopped";
        break;
    case LmsCommand::Status:
        break;
    }
}

LmsStatus LmsBridge::currentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

} // namespace hqplayer::lms

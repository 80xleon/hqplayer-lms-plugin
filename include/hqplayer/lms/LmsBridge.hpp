#pragma once

#include "hqplayer/lms/ILmsBridge.hpp"

#include <mutex>

namespace hqplayer::lms {

class LmsBridge final : public ILmsBridge {
public:
    void handleCommand(LmsCommand command) override;
    LmsStatus currentStatus() const override;

private:
    mutable std::mutex mutex_;
    LmsStatus status_{};
};

} // namespace hqplayer::lms

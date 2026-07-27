#pragma once

#include "hqplayer/lms/LmsTypes.hpp"

namespace hqplayer::lms {

class ILmsBridge {
public:
    virtual ~ILmsBridge() = default;

    virtual void handleCommand(LmsCommand command) = 0;
    virtual LmsStatus currentStatus() const = 0;
};

} // namespace hqplayer::lms

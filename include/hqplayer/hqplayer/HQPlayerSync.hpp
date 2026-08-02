#pragma once

// HQPlayerSync has been superseded by HQPlayerEventListener.
// This header is kept as a compatibility shim so any code that included it
// continues to compile.  New code should use HQPlayerEventListener directly.

#include "hqplayer/hqplayer/HQPlayerEventListener.hpp"

namespace hqplayer::hqplayer {

/// @deprecated Use HQPlayerEventListener instead.
using HQPlayerSync = HQPlayerEventListener;

} // namespace hqplayer::hqplayer

#pragma once

#include <mutex>
#include <string>

namespace hqplayer::util {

/// Daemon-wide log level enumeration (least to most severe).
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

/// Thread-safe singleton logger.
///
/// Writes timestamped, level-filtered messages to @c stdout.  The active log
/// level is configured at startup from the YAML config (@c logging.level).
///
/// Usage:
/// @code
///     Logger::instance().log(LogLevel::Info, "Service started");
/// @endcode
class Logger {
public:
    /// @return The process-wide Logger singleton.
    static Logger& instance();

    /// Set the minimum log level; messages below this level are discarded.
    void setLevel(LogLevel level);

    /// @return The currently active minimum log level.
    [[nodiscard]] LogLevel level() const;

    /// Write @p message at @p level if it is at or above the active level.
    void log(LogLevel level, const std::string& message);

    /// Parse a level name string (case-insensitive: "trace", "debug", "info",
    /// "warn", "error") into a LogLevel value.
    /// @throws std::invalid_argument if @p level is not a recognised name.
    static LogLevel parseLevel(const std::string& level);

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel level_{LogLevel::Info};
};

} // namespace hqplayer::util

#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace hqplayer::util {

/// Daemon-wide log level enumeration (least to most severe).
/// @c None disables all log output.
enum class LogLevel {
    None,
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

/// Thread-safe singleton logger.
///
/// Writes timestamped, level-filtered messages to @c stderr.  When a log
/// path is configured (@c setLogPath), messages are also appended to that
/// file.  The active log level is configured at startup from the YAML config
/// (@c logging.level).
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
    /// LogLevel::None suppresses all output.
    void setLevel(LogLevel level);

    /// @return The currently active minimum log level.
    [[nodiscard]] LogLevel level() const;

    /// Open (or re-open) the log file at @p path in append mode.
    /// Pass an empty string to close any open log file and log to stderr only.
    /// @throws std::runtime_error if @p path is non-empty but cannot be opened.
    void setLogPath(const std::string& path);

    /// Write @p message at @p level if it is at or above the active level.
    void log(LogLevel level, const std::string& message);

    /// Parse a level name string (case-insensitive: "none", "trace", "debug",
    /// "info", "warn", "error") into a LogLevel value.
    /// @throws std::invalid_argument if @p level is not a recognised name.
    static LogLevel parseLevel(const std::string& level);

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel           level_{LogLevel::Info};
    std::ofstream      log_file_;
};

} // namespace hqplayer::util

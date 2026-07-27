#pragma once

#include <mutex>
#include <string>

namespace hqplayer::util {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level);
    [[nodiscard]] LogLevel level() const;

    void log(LogLevel level, const std::string& message);

    static LogLevel parseLevel(const std::string& level);

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel level_{LogLevel::Info};
};

} // namespace hqplayer::util

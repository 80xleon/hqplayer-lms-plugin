#include "hqplayer/util/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

namespace hqplayer::util {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::setLogPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
    }
    if (!path.empty()) {
        log_file_.open(path, std::ios::app);
        if (!log_file_) {
            throw std::runtime_error("Cannot open log file: " + path);
        }
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (level_ == LogLevel::None) {
        return;
    }
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }
    std::clog << message << std::endl;
    if (log_file_.is_open()) {
        log_file_ << message << '\n';
        log_file_.flush();
    }
}

LogLevel Logger::parseLevel(const std::string& level) {
    std::string normalized = level;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "none") {
        return LogLevel::None;
    }
    if (normalized == "trace") {
        return LogLevel::Trace;
    }
    if (normalized == "debug") {
        return LogLevel::Debug;
    }
    if (normalized == "info") {
        return LogLevel::Info;
    }
    if (normalized == "warn" || normalized == "warning") {
        return LogLevel::Warn;
    }
    if (normalized == "error") {
        return LogLevel::Error;
    }

    throw std::invalid_argument("Invalid config value logging.level='" + level + "'");
}

} // namespace hqplayer::util

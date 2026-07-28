#include "hqplayer/hqplayer/HQPlayerSync.hpp"

#include "hqplayer/util/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace hqplayer::hqplayer {
namespace {

using ::hqplayer::util::LogLevel;
using ::hqplayer::util::Logger;

/// Maximum backoff delay between retries (milliseconds).
constexpr std::uint32_t kMaxBackoffMs = 30'000;

/// Initial backoff delay on the first error (milliseconds).
constexpr std::uint32_t kInitialBackoffMs = 1'000;

} // namespace

HQPlayerSync::HQPlayerSync(IHQPlayerClient& client,
                           std::uint32_t    pollIntervalMs,
                           Observer         observer)
    : client_(client)
    , pollIntervalMs_(pollIntervalMs)
    , observer_(std::move(observer)) {}

HQPlayerSync::~HQPlayerSync() {
    stop();
}

void HQPlayerSync::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }

    {
        std::lock_guard<std::mutex> lock(cvMutex_);
        stopFlag_ = false;
    }

    worker_ = std::thread(&HQPlayerSync::pollLoop, this);
}

void HQPlayerSync::stop() noexcept {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // already stopped
    }

    {
        std::lock_guard<std::mutex> lock(cvMutex_);
        stopFlag_ = true;
    }
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
}

HQPlayerStatus HQPlayerSync::cachedStatus() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return cached_;
}

void HQPlayerSync::updateCache(const HQPlayerStatus& status) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cached_ = status;
    }
    if (observer_) {
        try {
            observer_(status);
        } catch (const std::exception& e) {
            Logger::instance().log(LogLevel::Warn,
                "HQPlayerSync observer threw: " + std::string(e.what()));
        }
    }
}

void HQPlayerSync::pollLoop() {
    Logger::instance().log(LogLevel::Info,
        "HQPlayerSync: starting poll loop (interval=" +
        std::to_string(pollIntervalMs_) + "ms)");

    std::uint32_t backoffMs = kInitialBackoffMs;

    while (running_.load()) {
        // --- Poll ---
        try {
            const HQPlayerStatus status = client_.getStatus();
            backoffMs = kInitialBackoffMs; // reset on success
            updateCache(status);
        } catch (const std::exception& e) {
            Logger::instance().log(LogLevel::Warn,
                "HQPlayerSync: poll failed — " + std::string(e.what()) +
                " (retry in " + std::to_string(backoffMs) + "ms)");

            // Wait for backoff duration (wake early on stop).
            {
                std::unique_lock<std::mutex> lock(cvMutex_);
                cv_.wait_for(lock,
                    std::chrono::milliseconds(backoffMs),
                    [this] { return stopFlag_; });
            }
            backoffMs = std::min(backoffMs * 2, kMaxBackoffMs);
            continue;
        }

        // --- Wait for next poll interval (wake early on stop) ---
        {
            std::unique_lock<std::mutex> lock(cvMutex_);
            cv_.wait_for(lock,
                std::chrono::milliseconds(pollIntervalMs_),
                [this] { return stopFlag_; });
        }
    }

    Logger::instance().log(LogLevel::Info, "HQPlayerSync: poll loop stopped");
}

} // namespace hqplayer::hqplayer

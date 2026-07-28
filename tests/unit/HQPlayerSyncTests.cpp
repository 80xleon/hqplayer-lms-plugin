#include "hqplayer/hqplayer/HQPlayerSync.hpp"
#include "hqplayer/hqplayer/HQPlayerTypes.hpp"
#include "hqplayer/hqplayer/IHQPlayerClient.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// ---------------------------------------------------------------------------
// Mock client that returns a configurable status and counts calls.
// ---------------------------------------------------------------------------
struct MockHQPlayerClient final : hqplayer::hqplayer::IHQPlayerClient {
    explicit MockHQPlayerClient(
        hqplayer::hqplayer::HQPlayerState initialState =
            hqplayer::hqplayer::HQPlayerState::Stopped)
    {
        status_.state = initialState;
    }

    void play()  override { ++playCalls; }
    void pause() override { ++pauseCalls; }
    void stop()  override { ++stopCalls; }

    hqplayer::hqplayer::HQPlayerStatus getStatus() override {
        ++statusCalls;
        if (shouldThrow) {
            throw hqplayer::hqplayer::HQPlayerError("mock error");
        }
        return status_;
    }

    hqplayer::hqplayer::HQPlayerStatus status_{};
    std::atomic<int> statusCalls{0};
    std::atomic<int> playCalls{0};
    std::atomic<int> pauseCalls{0};
    std::atomic<int> stopCalls{0};
    bool shouldThrow{false};
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testCachedStatusUpdatedAfterPoll() {
    MockHQPlayerClient client;
    client.status_.state = hqplayer::hqplayer::HQPlayerState::Playing;
    client.status_.samplerate_hz = 44100;

    hqplayer::hqplayer::HQPlayerSync sync(client, 20 /* poll every 20ms */);
    sync.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    sync.stop();

    const auto cached = sync.cachedStatus();
    assertTrue(cached.state == hqplayer::hqplayer::HQPlayerState::Playing,
               "cached state should match mock client status");
    assertTrue(cached.samplerate_hz == 44100, "cached samplerate_hz should match");
    assertTrue(client.statusCalls.load() >= 1, "getStatus() should have been called at least once");
}

void testObserverCalledOnPoll() {
    MockHQPlayerClient client;
    client.status_.state = hqplayer::hqplayer::HQPlayerState::Paused;

    std::atomic<int> observerCallCount{0};
    hqplayer::hqplayer::HQPlayerState lastObservedState =
        hqplayer::hqplayer::HQPlayerState::Stopped;

    hqplayer::hqplayer::HQPlayerSync sync(
        client, 20,
        [&](const hqplayer::hqplayer::HQPlayerStatus& s) {
            lastObservedState = s.state;
            ++observerCallCount;
        });

    sync.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    sync.stop();

    assertTrue(observerCallCount.load() >= 1,
               "observer should have been called at least once");
    assertTrue(lastObservedState == hqplayer::hqplayer::HQPlayerState::Paused,
               "observer should receive the mock client state");
}

void testStopIsClean() {
    MockHQPlayerClient client;

    hqplayer::hqplayer::HQPlayerSync sync(client, 20);
    sync.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sync.stop();     // must return promptly
    sync.stop();     // second stop should be idempotent
}

void testDefaultCachedStatusIsStopped() {
    MockHQPlayerClient client;
    // Do NOT start the sync — verify the default value.
    hqplayer::hqplayer::HQPlayerSync sync(client, 5000);

    const auto cached = sync.cachedStatus();
    assertTrue(cached.state == hqplayer::hqplayer::HQPlayerState::Stopped,
               "default cached state should be Stopped");
}

void testErrorBackoffDoesNotCrash() {
    MockHQPlayerClient client;
    client.shouldThrow = true;

    std::atomic<int> observerCalls{0};
    hqplayer::hqplayer::HQPlayerSync sync(
        client, 20,
        [&](const hqplayer::hqplayer::HQPlayerStatus&) { ++observerCalls; });

    sync.start();
    // Let it attempt and fail a couple of times.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sync.stop();

    // Observer should not have been called on errors.
    assertTrue(observerCalls.load() == 0,
               "observer should not be called on poll errors");
}

} // namespace

int main() {
    try {
        testCachedStatusUpdatedAfterPoll();
        testObserverCalledOnPoll();
        testStopIsClean();
        testDefaultCachedStatusIsStopped();
        testErrorBackoffDoesNotCrash();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HQPlayerSyncTests failed: " << e.what() << std::endl;
        return 1;
    }
}

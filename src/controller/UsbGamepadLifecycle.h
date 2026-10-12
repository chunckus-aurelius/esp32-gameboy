#pragma once

#include <atomic>
#include <stdint.h>

namespace gameboy { namespace controller_detail {

enum class UsbGamepadStatus {
    Ok,
    InvalidState,
    SpawnFailed,
    StartupFailed,
    StartupTimeout,
    StopTimeout,
};

// start() and stop() must not overlap. The worker calls finished() as its last act.
class UsbGamepadLifecycle {
public:
    std::atomic<bool> stopRequested{false};
    std::atomic<int> startup{0};

    bool running() const { return runningFlag.load(); }

    template<class Spawn, class Sleep, class Clock>
    UsbGamepadStatus start(Spawn spawn, Sleep sleepMs, Clock clock,
                           uint32_t startupTimeoutMs = 1500,
                           uint32_t stopTimeoutMs = 3000) {
        if (runningFlag.load())
            return stopping.load() ? UsbGamepadStatus::InvalidState
                                   : UsbGamepadStatus::Ok;
        stopRequested.store(false);
        stopping.store(false);
        startup.store(0);
        runningFlag.store(true);
        if (!spawn()) {
            runningFlag.store(false);
            return UsbGamepadStatus::SpawnFailed;
        }
        const uint32_t started = clock();
        while (startup.load() == 0 && uint32_t(clock() - started) < startupTimeoutMs)
            sleepMs(10);
        if (startup.load() == 1) return UsbGamepadStatus::Ok;
        const UsbGamepadStatus cause = startup.load() == -1
            ? UsbGamepadStatus::StartupFailed : UsbGamepadStatus::StartupTimeout;
        const UsbGamepadStatus stopped = stop(sleepMs, clock, stopTimeoutMs);
        return stopped == UsbGamepadStatus::Ok ? cause : stopped;
    }

    template<class Sleep, class Clock>
    UsbGamepadStatus stop(Sleep sleepMs, Clock clock, uint32_t timeoutMs = 3000) {
        if (!runningFlag.load()) return UsbGamepadStatus::Ok;
        stopRequested.store(true);
        stopping.store(true);
        const uint32_t started = clock();
        while (runningFlag.load() && uint32_t(clock() - started) < timeoutMs)
            sleepMs(10);
        return runningFlag.load() ? UsbGamepadStatus::StopTimeout
                                  : UsbGamepadStatus::Ok;
    }

    void finished() {
        stopping.store(false);
        runningFlag.store(false);
    }

private:
    std::atomic<bool> runningFlag{false};
    std::atomic<bool> stopping{false};
};

} }

#include "../src/controller/UsbGamepadLifecycle.h"
#include <assert.h>
#include <chrono>
#include <thread>

using gameboy::controller_detail::UsbGamepadLifecycle;
using gameboy::controller_detail::UsbGamepadStatus;

static uint32_t nowMs() {
    using namespace std::chrono;
    return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
static void sleepMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct FakeHost {
    UsbGamepadLifecycle lifecycle;
    std::thread worker;
    int startupResult = 1;
    std::atomic<bool> ignoreStop{false};
    int spawns = 0;
    bool spawnSucceeds = true;

    bool spawn() {
        if (!spawnSucceeds) return false;
        ++spawns;
        if (worker.joinable()) worker.join();
        worker = std::thread([this] {
            if (startupResult != 0) lifecycle.startup.store(startupResult);
            while (startupResult == 1 &&
                   (ignoreStop.load() || !lifecycle.stopRequested.load()))
                sleepMs(1);
            while (startupResult == 0 && !lifecycle.stopRequested.load()) sleepMs(1);
            lifecycle.finished();
        });
        return true;
    }
    UsbGamepadStatus start(uint32_t startupMs = 1500, uint32_t stopMs = 3000) {
        return lifecycle.start([this] { return spawn(); }, sleepMs, nowMs, startupMs, stopMs);
    }
    UsbGamepadStatus stop(uint32_t timeoutMs = 3000) {
        return lifecycle.stop(sleepMs, nowMs, timeoutMs);
    }
    ~FakeHost() {
        ignoreStop.store(false);
        lifecycle.stopRequested.store(true);
        if (worker.joinable()) worker.join();
    }
};

int main() {
    {
        FakeHost host;
        assert(!host.lifecycle.running());
        assert(host.stop() == UsbGamepadStatus::Ok); // stop before start is a no-op
        assert(host.start() == UsbGamepadStatus::Ok);
        assert(host.lifecycle.running());
        assert(host.start() == UsbGamepadStatus::Ok && host.spawns == 1); // no second task
        assert(host.stop() == UsbGamepadStatus::Ok);
        assert(!host.lifecycle.running());
        assert(host.stop() == UsbGamepadStatus::Ok);
        assert(host.start() == UsbGamepadStatus::Ok && host.spawns == 2); // restartable
        assert(host.lifecycle.startup.load() == 1 && !host.lifecycle.stopRequested.load());
        assert(host.stop() == UsbGamepadStatus::Ok);
    }
    {
        FakeHost host;
        host.startupResult = -1; // usb_host_install failure; the worker exits by itself
        assert(host.start() == UsbGamepadStatus::StartupFailed);
        assert(!host.lifecycle.running());
        host.startupResult = 1;
        assert(host.start() == UsbGamepadStatus::Ok); // state fully reset
        assert(host.stop() == UsbGamepadStatus::Ok);
    }
    {
        FakeHost host;
        host.startupResult = 0; // never reports startup
        assert(host.start(50) == UsbGamepadStatus::StartupTimeout);
        assert(!host.lifecycle.running()); // timed-out start stops the worker
    }
    {
        FakeHost host;
        host.spawnSucceeds = false;
        assert(host.start() == UsbGamepadStatus::SpawnFailed);
        assert(!host.lifecycle.running());
    }
    {
        FakeHost host;
        assert(host.start() == UsbGamepadStatus::Ok);
        host.ignoreStop.store(true); // worker still draining the USB stack
        assert(host.stop(50) == UsbGamepadStatus::StopTimeout);
        assert(host.lifecycle.running());
        assert(host.start() == UsbGamepadStatus::InvalidState); // stop in progress
        host.ignoreStop.store(false);
        assert(host.stop() == UsbGamepadStatus::Ok); // a retry keeps waiting
        assert(!host.lifecycle.running());
    }
    return 0;
}

#pragma once

#include <atomic>
#include <stdint.h>

namespace gameboy { namespace controller_detail {

// Null members fall back to Serial.end()/begin(115200) under Arduino and to
// no-ops elsewhere, where the app owns the native USB console.
struct UsbConsoleHooks {
    void (*release)(void *context) = nullptr;
    void (*restore)(void *context) = nullptr;
    void *context = nullptr;
};

// Blocks in the controller worker until requested to stop. Owns USB host and
// the native USB console throughout that interval.
void runUsbSerial(std::atomic<bool> &stop, std::atomic<int> &startup,
                  void (*report)(uint8_t),
                  void (*disconnect)(),
                  const UsbConsoleHooks &console = {});

} }

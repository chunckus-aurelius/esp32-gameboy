#pragma once

#include <stdint.h>

namespace gameboy {

// All methods except start, stop and setMapping are safe to poll from the game task.
class Controller {
public:
    enum class Transport : uint8_t { Bluetooth = 0, UsbSerial = 1 };
    // Bluetooth owns NimBLE: stop any other BLE service first.
    // UsbSerial takes the native USB port until stop().
    static bool start(Transport transport = Transport::Bluetooth);
    static Transport transport();
    static bool supportsTransport(Transport transport);
    // Requests teardown and waits at most timeoutMs (clamped to 3000 ms).
    // False means teardown is still in progress; retry until active() is false.
    static bool stop(uint32_t timeoutMs = 3000);
    static bool active();
    static bool stopping();
    // Link and recent report are both required; stale input releases at 2 seconds.
    static bool connected();
    // Game Boy bit order: A, B, Select, Start, Right, Left, Up, Down.
    static uint8_t buttons();
    static uint8_t rawButtons();
    static uint32_t reportCount();
    static uint32_t lastReportAgeMs();
    // Eight distinct single-bit HID masks in Game Boy bit order; persist stores them in NVS.
    static bool setMapping(const uint8_t mapping[8], bool persist = true);
};

}

#include "../src/controller/UsbSerialInput.h"
#include "../src/controller/UsbConsoleLifecycle.h"
#include "../src/controller/UsbTransferDrain.h"
#include <assert.h>
#include <string.h>
#include <vector>
#include <string>

using gameboy::controller_detail::UsbSerialInput;
using gameboy::controller_detail::UsbCdcEndpoint;
using gameboy::controller_detail::c3CdcEndpoint;

static void send(UsbSerialInput &input, uint16_t sequence, uint8_t mask,
                 std::vector<uint8_t> &reports) {
    char frame[GamepadSerialProtocol::kFrameSize];
    assert(GamepadSerialProtocol::encode(sequence, mask, frame, sizeof frame) ==
           sizeof frame);
    input.feed(reinterpret_cast<const uint8_t *>(frame), 5,
               [&](uint8_t value) { reports.push_back(value); });
    input.feed(reinterpret_cast<const uint8_t *>(frame + 5), sizeof frame - 5,
               [&](uint8_t value) { reports.push_back(value); });
}

int main() {
    UsbSerialInput input;
    std::vector<uint8_t> reports;
    send(input, 0xffff, 0x80, reports);
    send(input, 0xffff, 0x00, reports); // duplicate cannot refresh held input
    send(input, 0x0000, 0x01, reports); // wrapped sequence is newer
    send(input, 0xfffe, 0x00, reports); // older sequence is rejected
    assert((reports == std::vector<uint8_t>{0x80, 0x01}));
    char corrupt[GamepadSerialProtocol::kFrameSize];
    GamepadSerialProtocol::encode(1, 0xff, corrupt, sizeof corrupt);
    corrupt[13] = corrupt[13] == 'A' ? 'B' : 'A';
    input.feed(reinterpret_cast<const uint8_t *>(corrupt), sizeof corrupt,
               [&](uint8_t value) { reports.push_back(value); });
    assert(reports.size() == 2);
    input.reset(); // disconnect starts a fresh sequence space
    send(input, 1, 0x22, reports);
    assert(reports.back() == 0x22);

    // ESP32-C3 USB Serial/JTAG layout: CDC control interface 0, data interface 1 with a bulk IN.
    // The extra JTAG endpoint must not be selected.
    const uint8_t descriptor[] = {
        9, 2, 48, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 2, 2, 0, 0,
        7, 5, 0x83, 3, 16, 0, 16,
        9, 4, 1, 0, 2, 10, 0, 0, 0,
        7, 5, 0x81, 2, 64, 0, 0,
        7, 5, 0x01, 2, 64, 0, 0,
    };
    UsbCdcEndpoint endpoint{};
    assert(c3CdcEndpoint(descriptor, sizeof descriptor, endpoint));
    assert(endpoint.address == 0x81 && endpoint.packetSize == 64);
    uint8_t unrelated[sizeof descriptor];
    memcpy(unrelated, descriptor, sizeof unrelated);
    unrelated[14] = 3; // control interface no longer CDC
    assert(!c3CdcEndpoint(unrelated, sizeof unrelated, endpoint));
    unrelated[14] = 2;
    unrelated[37] = 3; // bulk IN no longer bulk
    assert(!c3CdcEndpoint(unrelated, sizeof unrelated, endpoint));
    assert(!c3CdcEndpoint(descriptor, sizeof descriptor - 1, endpoint));
    memcpy(unrelated, descriptor, sizeof unrelated);
    unrelated[2] = 47; // declared total length does not match buffer
    assert(!c3CdcEndpoint(unrelated, sizeof unrelated, endpoint));
    memcpy(unrelated, descriptor, sizeof unrelated);
    unrelated[28] = 1; // data endpoint is on alternate setting one
    assert(!c3CdcEndpoint(unrelated, sizeof unrelated, endpoint));
    memcpy(unrelated, descriptor, sizeof unrelated);
    unrelated[36] = 0x80; // endpoint zero is reserved
    assert(!c3CdcEndpoint(unrelated, sizeof unrelated, endpoint));

    // The fake host completes its EP0 callback only after power-off, like IDF's device-gone path.
    // The drain must power off before pumping and never report completion early.
    bool controlPending = true, portOff = false;
    uint32_t time = 0;
    int pumps = 0;
    assert(gameboy::controller_detail::drainUsbTransfers(
        [&] { return controlPending; },
        [&] { portOff = true; },
        [&] { assert(portOff); ++pumps; controlPending = false; ++time; },
        [&] { return time; }, 500));
    assert(pumps == 1 && !controlPending);
    controlPending = true;
    time = 0;
    pumps = 0;
    assert(!gameboy::controller_detail::drainUsbTransfers(
        [&] { return controlPending; },
        [&] { portOff = true; },
        [&] { ++pumps; ++time; },
        [&] { return time; }, 3));
    assert(controlPending && pumps == 3); // caller must retain ownership
    assert(!gameboy::controller_detail::usbControlSetupExpired(true, 999, 0));
    assert(gameboy::controller_detail::usbControlSetupExpired(true, 1000, 0));
    assert(gameboy::controller_detail::usbControlSetupExpired(
        true, 990, UINT32_MAX - 9)); // deadline survives millis wrap
    assert(!gameboy::controller_detail::usbControlSetupExpired(false, 2000, 0));
    assert(!gameboy::controller_detail::shouldRepowerUsbRoot(true, true, false));
    assert(!gameboy::controller_detail::shouldRepowerUsbRoot(true, false, true));
    assert(gameboy::controller_detail::shouldRepowerUsbRoot(true, false, false));

    // Both a normal exit and failure during host setup must put the PHY back
    // before the native CDC console is started again.
    for (bool hostInstalled : {false, true}) {
        std::string steps;
        gameboy::controller_detail::withUsbConsoleReleased(
            [&] { steps += "end/"; },
            [&] { steps += hostInstalled ? "host-stop/" : "host-fail/"; },
            [&] { steps += "phy-usj/"; },
            [&] { steps += "begin"; });
        assert(steps == (hostInstalled ? "end/host-stop/phy-usj/begin" :
                                         "end/host-fail/phy-usj/begin"));
    }
    return 0;
}

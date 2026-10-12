#include "../src/controller/Report.h"
#include <assert.h>
#include <stdint.h>

int main() {
    uint8_t report[19] = {};
    uint8_t raw = 0xff;
    report[0] = 0x29;
    assert(gameboy::controller_detail::report3Buttons(report, sizeof report, &raw));
    assert(raw == 0x29);
    assert(gameboy::controller_detail::report3Buttons(report, 2, &raw));
    assert(gameboy::controller_detail::report3Buttons(report, 18, &raw));
    assert(!gameboy::controller_detail::report3Buttons(report, 1, &raw));
    uint8_t extended[33] = {};
    assert(!gameboy::controller_detail::report3Buttons(extended, sizeof extended, &raw));
    assert(!gameboy::controller_detail::report3Buttons(nullptr, 19, &raw));

    const uint8_t verified[8] = {0x40, 0x20, 0x10, 0x80, 0x08, 0x02, 0x04, 0x01};
    assert(gameboy::controller_detail::validMapping(verified));
    // Physical HID buttons 6, 4, 1 -> Game Boy B, Right, Down.
    assert(gameboy::controller_detail::mapButtons(0x29, verified) == 0x92);
    const uint8_t duplicate[8] = {1, 1, 4, 8, 16, 32, 64, 128};
    assert(!gameboy::controller_detail::validMapping(duplicate));
    return 0;
}

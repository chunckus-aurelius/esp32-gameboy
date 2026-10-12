#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gameboy { namespace controller_detail {

// Notification bytes exclude the report ID; the GATT Report Reference
// descriptor must first identify this characteristic as input report 3.
inline bool report3Buttons(const uint8_t *data, size_t length, uint8_t *buttons) {
    // Only byte 0 (buttons) is used; trailing axis bytes vary, so the length is just bounded.
    if (!data || !buttons || length < 2 || length > 32) return false;
    *buttons = data[0];
    return true;
}

inline bool validMapping(const uint8_t *mapping) {
    if (!mapping) return false;
    uint8_t used = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t bit = mapping[i];
        if (!bit || (bit & (bit - 1)) || (used & bit)) return false;
        used |= bit;
    }
    return used == 0xff;
}

inline uint8_t mapButtons(uint8_t raw, const uint8_t *mapping) {
    uint8_t result = 0;
    for (unsigned i = 0; i < 8; ++i) if (raw & mapping[i]) result |= 1u << i;
    return result;
}

} }

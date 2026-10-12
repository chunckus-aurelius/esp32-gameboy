#pragma once

#include <stdint.h>

namespace gameboy { namespace controller_detail {

inline bool usbControlSetupExpired(bool pending, uint32_t now,
                                   uint32_t started, uint32_t limitMs = 1000) {
    return pending && uint32_t(now - started) >= limitMs;
}

inline bool shouldRepowerUsbRoot(bool rootPortOff, bool deviceOpen,
                                 bool stopping) {
    return rootPortOff && !deviceOpen && !stopping;
}

// No public EP0 cancel: powering off the root port makes IDF complete the queued control URB.
// Never free a transfer before its completion callback has run.
template<class Pending, class Disconnect, class Pump, class Clock>
bool drainUsbTransfers(Pending pending, Disconnect disconnect, Pump pump,
                       Clock clock, uint32_t limitMs) {
    if (!pending()) return true;
    disconnect();
    const uint32_t started = clock();
    while (pending() && uint32_t(clock() - started) < limitMs) pump();
    return !pending();
}

} }

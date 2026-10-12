#pragma once

namespace gameboy { namespace controller_detail {

// Normal stop and host-install failure share this order; HostWork returns only
// after releasing the USB host stack and its PHY.
template<class EndConsole, class HostWork, class RestorePhy, class BeginConsole>
void withUsbConsoleReleased(EndConsole endConsole, HostWork hostWork,
                            RestorePhy restorePhy, BeginConsole beginConsole) {
    endConsole();
    hostWork();
    restorePhy();
    beginConsole();
}

} }

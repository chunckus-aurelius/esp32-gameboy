#pragma once

#include "GamepadSerialProtocol.h"
#include <stddef.h>
#include <stdint.h>

namespace gameboy { namespace controller_detail {

// One instance per USB connection. Repeated/older sequence numbers cannot
// keep a stuck button alive by refreshing the report timestamp.
class UsbSerialInput {
public:
    UsbSerialInput() : received_(false), sequence_(0) {}

    void reset() { parser_.reset(); received_ = false; sequence_ = 0; }

    template<class Sink>
    void feed(const uint8_t *bytes, size_t length, Sink sink) {
        if (!bytes) return;
        for (size_t i = 0; i < length; ++i) {
            GamepadSerialProtocol::Frame frame;
            if (!parser_.feed(static_cast<char>(bytes[i]), frame)) continue;
            if (received_) {
                const uint16_t delta = uint16_t(frame.sequence - sequence_);
                if (!delta || delta >= 0x8000) continue;
            }
            received_ = true;
            sequence_ = frame.sequence;
            sink(frame.mask);
        }
    }

private:
    GamepadSerialProtocol::Parser parser_;
    bool received_;
    uint16_t sequence_;
};

struct UsbCdcEndpoint {
    uint8_t address;
    uint16_t packetSize;
};

// Accepts only the native Serial/JTAG layout: CDC control interface 0, bulk-IN data interface 1.
inline bool c3CdcEndpoint(const uint8_t *data, size_t length, UsbCdcEndpoint &out) {
    if (!data || length < 9 || data[0] != 9 || data[1] != 2 ||
        size_t(data[2] | (data[3] << 8)) != length || data[4] < 2)
        return false;
    bool control = false, serialData = false, endpoint = false;
    uint8_t current = 0xff;
    bool currentData = false;
    uint8_t address = 0;
    uint16_t packetSize = 0;
    for (size_t offset = 0; offset < length;) {
        if (length - offset < 2 || data[offset] < 2 || data[offset] > length - offset)
            return false;
        const uint8_t size = data[offset], type = data[offset + 1];
        if (type == 4) {
            if (size < 9) return false;
            current = data[offset + 2];
            const uint8_t alt = data[offset + 3], cls = data[offset + 5];
            currentData = current == 1 && alt == 0 && cls == 10;
            if (current == 0 && alt == 0 && cls == 2 && data[offset + 6] == 2)
                control = true;
            if (currentData) serialData = true;
        } else if (type == 5) {
            if (size < 7) return false;
            if (currentData && (data[offset + 2] & 0xf0) == 0x80 &&
                (data[offset + 2] & 0x0f) != 0 &&
                (data[offset + 3] & 3) == 2) {
                if (endpoint) return false;
                address = data[offset + 2];
                packetSize = uint16_t(data[offset + 4] | (data[offset + 5] << 8)) & 0x7ff;
                endpoint = packetSize > 0 && packetSize <= 64;
            }
        }
        offset += size;
    }
    if (!control || !serialData || !endpoint) return false;
    out = {address, packetSize};
    return true;
}

} }

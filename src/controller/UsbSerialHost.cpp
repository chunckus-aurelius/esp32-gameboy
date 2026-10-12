#include "UsbSerialHost.h"
#include "UsbSerialInput.h"
#include "UsbConsoleLifecycle.h"
#include "UsbTransferDrain.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/usb_serial_jtag_ll.h>
#include <string.h>
#include <usb/usb_host.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace gameboy { namespace controller_detail {
namespace {

constexpr uint16_t kEspressifVid = 0x303a;
constexpr uint16_t kSerialJtagPid = 0x1001;

uint32_t nowMs() { return uint32_t(esp_timer_get_time() / 1000); }

// HWCDC::end releases the native D+/D- pin ownership and ISR. Its write
// method immediately returns while ended, including from other app tasks.
void releaseDefaultConsole(void *) {
#ifdef ARDUINO
    Serial.end();
#endif
}

void restoreDefaultConsole(void *) {
#ifdef ARDUINO
    Serial.begin(115200);
#endif
}

struct Host {
    usb_host_client_handle_t client = nullptr;
    usb_device_handle_t device = nullptr;
    usb_transfer_t *control = nullptr;
    usb_transfer_t *receive = nullptr;
    UsbSerialInput input;
    std::atomic<bool> *stop = nullptr;
    std::atomic<int> *startup = nullptr;
    void (*report)(uint8_t) = nullptr;
    void (*disconnect)() = nullptr;
    uint8_t pendingAddress = 0;
    uint8_t inAddress = 0;
    uint16_t packetSize = 0;
    uint32_t controlStartedAt = 0;
    bool claimed = false;
    bool gone = false;
    bool rootPortOff = false;
    bool recovering = false;
    bool controlPending = false;
    bool controlDone = false;
    bool controlOk = false;
    bool receivePending = false;
    bool receiveBroken = false;
};

void deviceEvent(const usb_host_client_event_msg_t *message, void *arg) {
    Host &host = *static_cast<Host *>(arg);
    if (message->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (!host.device) host.pendingAddress = message->new_dev.address;
    } else if (message->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
               message->dev_gone.dev_hdl == host.device) {
        host.gone = true;
        host.disconnect();
    }
}

void controlComplete(usb_transfer_t *transfer) {
    Host &host = *static_cast<Host *>(transfer->context);
    host.controlPending = false;
    host.controlDone = true;
    host.controlOk = transfer->status == USB_TRANSFER_STATUS_COMPLETED;
}

void receiveComplete(usb_transfer_t *transfer) {
    Host &host = *static_cast<Host *>(transfer->context);
    host.receivePending = false;
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED || host.gone) {
        host.receiveBroken = true;
        host.disconnect();
        return;
    }
    if (!host.stop->load() && transfer->actual_num_bytes > 0)
        host.input.feed(transfer->data_buffer, size_t(transfer->actual_num_bytes),
                        host.report);
}

// Client callbacks and transfers execute on this same worker. The daemon is
// pumped between each bounded client event wait so enumeration also advances.
void pump(Host &host, TickType_t ticks = pdMS_TO_TICKS(10)) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(0, &flags);
    if (host.client) usb_host_client_handle_events(host.client, ticks);
}

bool powerOffRoot(Host &host) {
    if (host.rootPortOff) return true;
    if (usb_host_lib_set_root_port_power(false) != ESP_OK) return false;
    host.rootPortOff = true;
    return true;
}

bool closeDevice(Host &host) {
    host.disconnect();
    if (host.receivePending && !host.gone && host.device && host.claimed) {
        usb_host_endpoint_halt(host.device, host.inAddress);
        usb_host_endpoint_flush(host.device, host.inAddress);
    }
    // usb_host_endpoint_halt() rejects EP0; powering off the root port forces the pending
    // control transfer to complete. Bounded: on timeout keep resources owned and retry.
    if (!drainUsbTransfers(
            [&] { return host.controlPending || host.receivePending; },
            [&] { powerOffRoot(host); },
            [&] { pump(host); }, nowMs, 500)) return false;
    usb_host_transfer_free(host.receive);
    usb_host_transfer_free(host.control);
    host.receive = nullptr;
    host.control = nullptr;
    if (host.claimed) usb_host_interface_release(host.client, host.device, 1);
    if (host.device) usb_host_device_close(host.client, host.device);
    host.device = nullptr;
    host.pendingAddress = 0;
    host.claimed = false;
    host.gone = false;
    host.recovering = false;
    host.controlStartedAt = 0;
    host.controlDone = false;
    host.controlOk = false;
    host.receiveBroken = false;
    host.input.reset();
    return true;
}

bool openDevice(Host &host, uint8_t address) {
    if (usb_host_device_open(host.client, address, &host.device) != ESP_OK) return false;
    const usb_device_desc_t *descriptor = nullptr;
    const usb_config_desc_t *configuration = nullptr;
    if (usb_host_get_device_descriptor(host.device, &descriptor) != ESP_OK ||
        !descriptor || descriptor->idVendor != kEspressifVid ||
        descriptor->idProduct != kSerialJtagPid ||
        usb_host_get_active_config_descriptor(host.device, &configuration) != ESP_OK ||
        !configuration) return false;

    UsbCdcEndpoint endpoint{};
    if (!c3CdcEndpoint(reinterpret_cast<const uint8_t *>(configuration),
                       configuration->wTotalLength, endpoint)) return false;
    host.inAddress = endpoint.address;
    host.packetSize = endpoint.packetSize;
    if (usb_host_interface_claim(host.client, host.device, 1, 0) != ESP_OK)
        return false;
    host.claimed = true;
    if (usb_host_transfer_alloc(8, 0, &host.control) != ESP_OK ||
        usb_host_transfer_alloc(host.packetSize, 0, &host.receive) != ESP_OK)
        return false;

    // CDC SET_CONTROL_LINE_STATE: DTR=1 permits C3 Serial output; RTS=0
    // avoids the native serial/JTAG bootloader reset combination.
    const uint8_t setup[8] = {0x21, 0x22, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(host.control->data_buffer, setup, sizeof setup);
    host.control->device_handle = host.device;
    host.control->bEndpointAddress = 0;
    host.control->num_bytes = sizeof setup;
    host.control->callback = controlComplete;
    host.control->context = &host;
    if (usb_host_transfer_submit_control(host.client, host.control) != ESP_OK)
        return false;
    host.controlPending = true;
    host.controlStartedAt = nowMs();
    return true;
}

void run(Host &host) {
    usb_host_config_t config{};
    config.skip_phy_setup = false;
    if (usb_host_install(&config) != ESP_OK) {
        host.startup->store(-1);
        return;
    }

    usb_host_client_config_t clientConfig{};
    clientConfig.is_synchronous = false;
    clientConfig.max_num_event_msg = 4;
    clientConfig.async.client_event_callback = deviceEvent;
    clientConfig.async.callback_arg = &host;
    if (usb_host_client_register(&clientConfig, &host.client) == ESP_OK) {
        host.startup->store(1);
        while (!host.stop->load()) {
            pump(host);
            if (shouldRepowerUsbRoot(host.rootPortOff, host.device != nullptr,
                                     host.stop->load())) {
                if (usb_host_lib_set_root_port_power(true) == ESP_OK)
                    host.rootPortOff = false;
                continue;
            }
            if (usbControlSetupExpired(host.controlPending, nowMs(),
                                       host.controlStartedAt))
                host.recovering = true;
            if (host.recovering || host.gone || host.receiveBroken ||
                (host.controlDone && !host.controlOk)) {
                // A failed CDC request or read must cause re-enumeration;
                // merely closing the handle leaves a connected device idle.
                const bool portOff = powerOffRoot(host);
                const bool closed = closeDevice(host);
                if (closed && !portOff) host.recovering = true;
                continue;
            }
            if (!host.device && host.pendingAddress) {
                const uint8_t address = host.pendingAddress;
                host.pendingAddress = 0;
                if (!openDevice(host, address)) closeDevice(host);
            }
            if (host.device && host.controlDone && host.controlOk &&
                !host.receivePending) {
                host.receive->device_handle = host.device;
                host.receive->bEndpointAddress = host.inAddress;
                host.receive->num_bytes = host.packetSize;
                host.receive->callback = receiveComplete;
                host.receive->context = &host;
                if (usb_host_transfer_submit(host.receive) == ESP_OK)
                    host.receivePending = true;
                else
                    host.receiveBroken = true;
            }
        }
        while (!closeDevice(host)) pump(host);
        usb_host_client_deregister(host.client);
        host.client = nullptr;
    } else {
        host.startup->store(-1);
    }
    // Device freeing is asynchronous even after all clients close. Only
    // release the PHY and restore Serial once the library is fully drained.
    usb_host_device_free_all();
    while (usb_host_uninstall() != ESP_OK) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
        usb_host_device_free_all();
    }
}

}

void runUsbSerial(std::atomic<bool> &stop, std::atomic<int> &startup,
                  void (*report)(uint8_t),
                  void (*disconnect)(),
                  const UsbConsoleHooks &console) {
    Host host;
    host.stop = &stop;
    host.startup = &startup;
    host.report = report;
    host.disconnect = disconnect;
    auto release = console.release ? console.release : releaseDefaultConsole;
    auto restore = console.restore ? console.restore : restoreDefaultConsole;
    withUsbConsoleReleased(
        [&] { release(console.context); },
        [&] { run(host); },
        [] {
            // usb_host_uninstall frees the OTG PHY but does not route the
            // internal PHY back to USB Serial/JTAG. HWCDC::begin alone cannot.
            usb_serial_jtag_ll_phy_enable_external(false);
        },
        [&] { restore(console.context); });
}

} }

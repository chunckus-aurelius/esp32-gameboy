#include "sdkconfig.h"

// PlatformIO compiles every source and has no GAMEBOY_USB_SERIAL_HOST Kconfig;
// CMakeLists.txt applies that option for IDF.
#if CONFIG_SOC_USB_OTG_SUPPORTED && (defined(ARDUINO) || CONFIG_GAMEBOY_USB_SERIAL_HOST)

#include "gameboy/usb_gamepad.h"
#include "UsbGamepadLifecycle.h"
#include "UsbSerialHost.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

using gameboy::controller_detail::UsbConsoleHooks;
using gameboy::controller_detail::UsbGamepadLifecycle;
using gameboy::controller_detail::UsbGamepadStatus;

UsbGamepadLifecycle lifecycle;
usb_gamepad_config_t active;

uint32_t nowMs() { return uint32_t(esp_timer_get_time() / 1000); }
void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void notifyDisconnected() {
    if (active.disconnected) active.disconnected(active.ctx);
}

void forwardReport(uint8_t mask) {
    if (lifecycle.stopRequested.load()) return;
    active.report(mask, active.ctx);
}

void worker(void *) {
    UsbConsoleHooks console;
    console.release = active.release_console;
    console.restore = active.restore_console;
    console.context = active.ctx;
    gameboy::controller_detail::runUsbSerial(lifecycle.stopRequested, lifecycle.startup,
                                             forwardReport, notifyDisconnected, console);
    notifyDisconnected();
    lifecycle.finished();
    vTaskDelete(nullptr);
}

esp_err_t toEsp(UsbGamepadStatus status) {
    switch (status) {
    case UsbGamepadStatus::Ok: return ESP_OK;
    case UsbGamepadStatus::InvalidState: return ESP_ERR_INVALID_STATE;
    case UsbGamepadStatus::SpawnFailed: return ESP_ERR_NO_MEM;
    case UsbGamepadStatus::StartupFailed: return ESP_FAIL;
    case UsbGamepadStatus::StartupTimeout:
    case UsbGamepadStatus::StopTimeout: return ESP_ERR_TIMEOUT;
    }
    return ESP_FAIL;
}

}

extern "C" {

esp_err_t usb_gamepad_start(const usb_gamepad_config_t *config) {
    if (!config || !config->report) return ESP_ERR_INVALID_ARG;
    return toEsp(lifecycle.start(
        [config] {
            active = *config;
            return xTaskCreatePinnedToCore(worker, "gb_usb_pad", 8192, nullptr, 2,
                                           nullptr, 0) == pdPASS;
        },
        sleepMs, nowMs));
}

esp_err_t usb_gamepad_stop(void) { return toEsp(lifecycle.stop(sleepMs, nowMs)); }

bool usb_gamepad_running(void) { return lifecycle.running(); }

}

#endif

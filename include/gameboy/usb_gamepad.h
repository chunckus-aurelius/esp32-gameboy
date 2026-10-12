#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mask bit n is controller button n+1; bit 0 is the C3's GPIO0 button. */
typedef void (*usb_gamepad_report_fn)(uint8_t mask, void *ctx);
typedef void (*usb_gamepad_event_fn)(void *ctx);

typedef struct {
    /* Required. Runs on the host task for every valid report; must not block. */
    usb_gamepad_report_fn report;
    /* Optional. May run repeatedly, and once more as the task exits.
     * Means "release every button". */
    usb_gamepad_event_fn disconnected;
    /* Optional pair around the interval that owns the native USB port.
     * NULL: nothing to move, or Serial.end()/begin(115200) under Arduino. */
    usb_gamepad_event_fn release_console;
    usb_gamepad_event_fn restore_console;
    void *ctx;
} usb_gamepad_config_t;

/* Returns once USB host startup succeeds or fails. The config is copied.
 * ESP_OK also when already running (new config ignored); an error leaves the host stopped. */
esp_err_t usb_gamepad_start(const usb_gamepad_config_t *config);

/* Blocks until USB and the console are restored. ESP_ERR_TIMEOUT after 3 s means
 * still draining: call again. Never call from a callback. */
esp_err_t usb_gamepad_stop(void);

bool usb_gamepad_running(void);

#ifdef __cplusplus
}
#endif

#include "gameboy/Controller.h"
#include "Report.h"
#include "UsbSerialHost.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEClient.h>
#include <NimBLEScan.h>
#include <NimBLERemoteService.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteDescriptor.h>
#include <NimBLEAttValue.h>
#include <esp_bt.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <atomic>
#include <string.h>

namespace gameboy {
namespace {

constexpr uint32_t kStaleMs = 2000;
constexpr uint32_t kScanMs = 800;
constexpr uint32_t kRetryMs = 400;
constexpr uint8_t kKnownMapping[8] = {0x40, 0x20, 0x10, 0x80, 0x08, 0x02, 0x04, 0x01};

std::atomic<bool> running{false}, stoppingRequested{false}, stopRequested{false};
std::atomic<bool> linkUp{false}, disconnected{true}, hasReport{false};
std::atomic<uint8_t> raw{0};
std::atomic<uint32_t> reports{0}, reportAt{0};
std::atomic<uint32_t> lastNotifyLength{UINT32_MAX};
std::atomic<Controller::Transport> selectedTransport{Controller::Transport::Bluetooth};
std::atomic<int> usbStartup{0};
portMUX_TYPE mappingMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t mapping[8] = {0x40, 0x20, 0x10, 0x80, 0x08, 0x02, 0x04, 0x01};

void releaseInput() {
    linkUp.store(false);
    hasReport.store(false);
    raw.store(0);
}

void unwindInitFailure() {
    // NimBLEDevice::deinit() cannot stop a controller left initialized by a
    // failed init because its m_initialized flag is still false.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        esp_bt_controller_disable();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
        esp_bt_controller_deinit();
    NimBLEDevice::deinit(true);
}

bool loadMapping() {
    uint8_t loaded[8];
    memcpy(loaded, kKnownMapping, sizeof loaded);
    // NimBLEDevice::init erases NVS on NO_FREE_PAGES / NEW_VERSION_FOUND.
    // Check first so controller startup cannot destroy the original mapping.
    if (nvs_flash_init() != ESP_OK) return false;
    nvs_handle_t handle;
    if (nvs_open("gamepad", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t saved[8];
        size_t size = sizeof saved;
        if (nvs_get_blob(handle, "buttons", saved, &size) == ESP_OK &&
            size == sizeof saved && controller_detail::validMapping(saved)) {
            memcpy(loaded, saved, sizeof loaded);
        }
        nvs_close(handle);
    }
    portENTER_CRITICAL(&mappingMux);
    memcpy(mapping, loaded, sizeof mapping);
    portEXIT_CRITICAL(&mappingMux);
    return true;
}

class ClientCallbacks final : public NimBLEClientCallbacks {
    void onConnectFail(NimBLEClient *, int reason) override {
        Serial.printf("GB_PAD connect_fail reason=%d\n", reason);
    }
    void onDisconnect(NimBLEClient *, int reason) override {
        Serial.printf("GB_PAD disconnected reason=%d\n", reason);
        releaseInput();
        disconnected.store(true);
    }
};
ClientCallbacks clientCallbacks;

void onReport(NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
    uint8_t buttons;
    const bool valid = controller_detail::report3Buttons(data, length, &buttons);
    if (lastNotifyLength.exchange(uint32_t(length)) != length) {
        Serial.printf("GB_PAD notify bytes=%u valid=%u\n", unsigned(length), unsigned(valid));
#if defined(ANKI_TEST_IO) && ANKI_TEST_IO
        Serial.printf("GB_PAD host_stack_min_bytes=%u task=%s\n",
                      unsigned(uxTaskGetStackHighWaterMark(nullptr)), pcTaskGetName(nullptr));
#endif
    }
    if (stopRequested.load() || !valid) return;
    raw.store(buttons);
    reportAt.store(millis());
    reports.fetch_add(1);
    hasReport.store(true);
}

void onUsbReport(uint8_t buttons) {
    if (stopRequested.load()) return;
    raw.store(buttons);
    reportAt.store(millis());
    reports.fetch_add(1);
    hasReport.store(true);
    linkUp.store(true);
}

void usbWorker(void *) {
    controller_detail::runUsbSerial(stopRequested, usbStartup, onUsbReport, releaseInput);
    releaseInput();
    running.store(false);
    stoppingRequested.store(false);
    vTaskDelete(nullptr);
}

// Runs only on the worker. Delete a client only after its disconnect callback;
// NimBLEDevice::deleteClient otherwise schedules asynchronous deletion.
void finishClient(NimBLEClient *client) {
    if (!client) return;
    releaseInput();
    if (client->isConnected()) {
        disconnected.store(false);
        while (!disconnected.load()) {
            client->disconnect();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        // onDisconnect fires before NimBLE finishes its own event handler.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    NimBLEDevice::deleteClient(client);
    while (NimBLEDevice::getCreatedClientCount() != 0) vTaskDelay(pdMS_TO_TICKS(20));
}

bool subscribeReport3(NimBLEClient *client) {
    NimBLERemoteService *hid = client->getService(NimBLEUUID(uint16_t(0x1812)));
    if (!hid || stopRequested.load()) {
        Serial.printf("GB_PAD hid_service=%u error=%d\n", unsigned(hid != nullptr), client->getLastError());
        return false;
    }
    // getService() only discovers the service. The default false here returns
    // NimBLE's empty characteristic cache, so explicitly perform GATT discovery.
    const auto &characteristics = hid->getCharacteristics(true);
    Serial.printf("GB_PAD hid_chars=%u\n", unsigned(characteristics.size()));
    for (NimBLERemoteCharacteristic *characteristic : characteristics) {
        if (stopRequested.load() || !client->isConnected()) return false;
        if (characteristic->getUUID() != NimBLEUUID(uint16_t(0x2a4d)) ||
            !characteristic->canNotify()) continue;
        NimBLERemoteDescriptor *reference = characteristic->getDescriptor(NimBLEUUID(uint16_t(0x2908)));
        if (!reference || stopRequested.load()) {
            Serial.printf("GB_PAD report_ref=%u\n", unsigned(reference != nullptr));
            continue;
        }
        NimBLEAttValue value = reference->readValue();
        Serial.printf("GB_PAD report_ref bytes=%u id=%u type=%u\n", unsigned(value.size()),
                      value.size() ? unsigned(value.data()[0]) : 0u,
                      value.size() > 1 ? unsigned(value.data()[1]) : 0u);
        // 0x01 is an input report; reject output/feature report 3.
        if (value.size() != 2 || value.data()[0] != 3 || value.data()[1] != 1) continue;
        if (stopRequested.load()) return false;
        const bool subscribed = characteristic->subscribe(true, onReport);
        Serial.printf("GB_PAD subscribe=%u error=%d\n", unsigned(subscribed), client->getLastError());
        return subscribed;
    }
    Serial.println("GB_PAD report3_not_found");
    return false;
}

void worker(void *) {
#if defined(ANKI_TEST_IO) && ANKI_TEST_IO
    Serial.printf("GB_PAD worker_stack_start_min_bytes=%u\n",
                  unsigned(uxTaskGetStackHighWaterMark(nullptr)));
#endif
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan) {
        scan->setActiveScan(true); // name may be present only in scan response
        scan->setMaxResults(64);
        scan->setInterval(80);
        scan->setWindow(60);
    }

    uint32_t scans = 0;
#if defined(ANKI_TEST_IO) && ANKI_TEST_IO
    uint32_t connections = 0;
#endif
    if (!scan) Serial.println("GB_PAD scan_unavailable");
    while (scan && !stopRequested.load()) {
        NimBLEAddress address;
        NimBLEScanResults results = scan->getResults(kScanMs);
        unsigned named = 0, connectable = 0;
        for (int i = 0; i < results.getCount() && !stopRequested.load(); ++i) {
            const NimBLEAdvertisedDevice *device = results.getDevice(i);
            if (device && device->getName() == "ESP32 Gamepad") {
                ++named;
                if (device->isConnectable()) ++connectable;
                address = device->getAddress();
                break;
            }
        }
        if (named || (++scans % 4) == 0)
            Serial.printf("GB_PAD scan results=%d named=%u connectable=%u\n",
                          results.getCount(), named, connectable);
        scan->clearResults();
        if (stopRequested.load()) break;
        if (address.isNull()) {
            vTaskDelay(pdMS_TO_TICKS(kRetryMs));
            continue;
        }

        NimBLEClient *client = NimBLEDevice::createClient();
        if (!client) {
            vTaskDelay(pdMS_TO_TICKS(kRetryMs));
            continue;
        }
        client->setClientCallbacks(&clientCallbacks, false);
        client->setConnectRetries(0);
        client->setConnectTimeout(1200);
        disconnected.store(false);
        lastNotifyLength.store(UINT32_MAX);
        const bool opened = client->connect(address, true, false, false);
        Serial.printf("GB_PAD connect=%u error=%d\n", unsigned(opened), client->getLastError());
#if defined(ANKI_TEST_IO) && ANKI_TEST_IO
        if (opened && (++connections == 1 || connections % 4 == 0))
            Serial.printf("GB_PAD worker_stack_connected_min_bytes=%u\n",
                          unsigned(uxTaskGetStackHighWaterMark(nullptr)));
#endif
        if (opened && !stopRequested.load() && subscribeReport3(client)) {
            linkUp.store(true);
            while (!stopRequested.load() && client->isConnected()) {
                if (hasReport.load() && uint32_t(millis() - reportAt.load()) >= kStaleMs) break;
                vTaskDelay(pdMS_TO_TICKS(25));
            }
        }
        finishClient(client);
        if (!stopRequested.load()) vTaskDelay(pdMS_TO_TICKS(kRetryMs));
    }

    releaseInput();
    if (scan) {
        scan->stop();
        scan->clearResults();
    }
    // This worker owns every central operation. Only it may deinitialize the
    // stack, after GATT work and client callbacks have completed.
    while (!NimBLEDevice::deinit(true)) vTaskDelay(pdMS_TO_TICKS(100));
    // NimBLE-Arduino 2.5.1 discards nimble_port_deinit's return code. Confirm
    // its controller cleanup before advertising that setup BLE may start.
    while (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
            esp_bt_controller_disable();
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
            esp_bt_controller_deinit();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    running.store(false);
    stoppingRequested.store(false);
    vTaskDelete(nullptr);
}

}

bool Controller::start(Transport chosen) {
    if (!supportsTransport(chosen)) return false;
    if (running.load()) return selectedTransport.load() == chosen &&
                               !stoppingRequested.load();
    if (!loadMapping()) return false;
    raw.store(0);
    reports.store(0);
    hasReport.store(false);
    linkUp.store(false);
    stopRequested.store(false);
    stoppingRequested.store(false);
    selectedTransport.store(chosen);
    if (chosen == Transport::UsbSerial) {
        usbStartup.store(0);
        running.store(true);
        if (xTaskCreatePinnedToCore(usbWorker, "gb_usb_pad", 8192, nullptr, 2,
                                    nullptr, 0) != pdPASS) {
            running.store(false);
            return false;
        }
        const uint32_t started = millis();
        while (usbStartup.load() == 0 && uint32_t(millis() - started) < 1500)
            vTaskDelay(pdMS_TO_TICKS(10));
        if (usbStartup.load() == 1) return true;
        stop();
        return false;
    }
    if (NimBLEDevice::isInitialized()) return false; // setup service still owns radio
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) return false;
    if (!NimBLEDevice::init("Game Boy")) {
        unwindInitFailure();
        return false;
    }
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    running.store(true);
    if (xTaskCreatePinnedToCore(worker, "gb_pad", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
        running.store(false);
        NimBLEDevice::deinit(true);
        return false;
    }
    return true;
}

bool Controller::stop(uint32_t timeoutMs) {
    if (!running.load()) return true;
    stopRequested.store(true);
    stoppingRequested.store(true);
    releaseInput();
    if (timeoutMs > 3000) timeoutMs = 3000;
    const uint32_t started = millis();
    while (running.load() && uint32_t(millis() - started) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return !running.load();
}

bool Controller::active() { return running.load(); }
Controller::Transport Controller::transport() { return selectedTransport.load(); }
bool Controller::supportsTransport(Transport value) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && ARDUINO_USB_MODE
    return value == Transport::Bluetooth || value == Transport::UsbSerial;
#else
    return value == Transport::Bluetooth;
#endif
}
bool Controller::stopping() { return stoppingRequested.load(); }
bool Controller::connected() {
    return linkUp.load() && hasReport.load() && uint32_t(millis() - reportAt.load()) < kStaleMs;
}
uint8_t Controller::rawButtons() { return connected() ? raw.load() : 0; }
uint8_t Controller::buttons() {
    const uint8_t value = rawButtons();
    uint8_t map[8];
    portENTER_CRITICAL(&mappingMux);
    memcpy(map, mapping, sizeof map);
    portEXIT_CRITICAL(&mappingMux);
    return controller_detail::mapButtons(value, map);
}
uint32_t Controller::reportCount() { return reports.load(); }
uint32_t Controller::lastReportAgeMs() {
    return hasReport.load() ? uint32_t(millis() - reportAt.load()) : UINT32_MAX;
}

bool Controller::setMapping(const uint8_t proposed[8], bool persist) {
    if (!controller_detail::validMapping(proposed)) return false;
    if (persist) {
        if (nvs_flash_init() != ESP_OK) return false;
        nvs_handle_t handle;
        if (nvs_open("gamepad", NVS_READWRITE, &handle) != ESP_OK) return false;
        const bool ok = nvs_set_blob(handle, "buttons", proposed, 8) == ESP_OK &&
                        nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
        if (!ok) return false;
    }
    portENTER_CRITICAL(&mappingMux);
    memcpy(mapping, proposed, sizeof mapping);
    portEXIT_CRITICAL(&mappingMux);
    return true;
}

}

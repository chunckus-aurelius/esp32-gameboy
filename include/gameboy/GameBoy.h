#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef ARDUINO_ARCH_ESP32
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace gameboy {

enum Button : uint8_t {
  A = 1u, B = 2u, Select = 4u, Start = 8u,
  Right = 16u, Left = 32u, Up = 64u, Down = 128u
};

// All callbacks run on the emulator task. Copy frame/audio data before returning.
// The frame is 160x144 pixels, four shades packed left pixel in low two bits.
struct Callbacks {
  void* context = nullptr;
  uint8_t (*buttons)(void*) = nullptr;
  void (*frame)(void*, const uint8_t* packed, size_t bytes) = nullptr;
  void (*audio)(void*, const int16_t* mono, size_t samples) = nullptr;
};

enum class Phase : uint8_t {
  Idle, Loading, Paused, Playing, Saving, SaveFailed, Error, Stopping
};

struct Status {
  Phase phase = Phase::Idle;
  int error = 0;
  char errorMessage[128]{};
  char game[256]{};
  uint32_t frames = 0;
  bool saveDirty = false;
  bool stateAvailable = false;
};

// One Session owns the single core. Mount /sdcard before list/start.
// A failed save phase keeps the core and ROM alive until requestRetrySave succeeds.
class Session {
 public:
  Session() = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  bool begin(Callbacks callbacks);
  bool list(char names[][256], size_t capacity, size_t* count, bool* truncated);
  // Validates ROM identity and checkpoint CRC before offering Continue.
  // Call on the UI task before start, not concurrently with a running game.
  bool hasCheckpoint(const char* romPath);
  // Returns once the task is launched; the load result arrives through status().
  bool start(const char* romPath, bool resumeState = false);
  void requestPause();
  void requestResume();
  void requestSave();
  void requestRetrySave();
  void requestStop();
  Status status() const;
  bool join(uint32_t timeoutMs);

 private:
  static void taskEntry(void* context);
  static void saveEntry(void* context);
  void run();
  void update(Phase phase, int error = 0, const char* message = nullptr);
  void setDirty(bool dirty);
  bool startSaveWorker();
  bool queueBatterySnapshot();
  bool submitBatteryBuffer();
  void waitBatteryWrite();
  bool persist(bool checkpoint);
  void cleanup();

  Callbacks callbacks_{};
  mutable portMUX_TYPE statusMux_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_{};
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t done_ = nullptr;
  TaskHandle_t saveTask_ = nullptr;
  SemaphoreHandle_t saveWake_ = nullptr;
  SemaphoreHandle_t saveDone_ = nullptr;
  SemaphoreHandle_t saveExit_ = nullptr;
  std::atomic<bool> saveWorkerStop_{false};
  std::atomic<bool> saveWorkerBusy_{false};
  std::atomic<bool> saveWorkerPending_{false};
  std::atomic<bool> saveWorkerFailed_{false};
  std::atomic<bool> pause_{false};
  std::atomic<bool> resume_{false};
  std::atomic<bool> save_{false};
  std::atomic<bool> retry_{false};
  std::atomic<bool> stop_{false};
  char romPath_[288]{};
  char savePath_[288]{};
  char statePath_[300]{};
  bool resumeState_ = false;
  uint8_t* rom_ = nullptr;
  uint8_t* saveBuffer_ = nullptr;
  uint8_t* stateBuffer_ = nullptr;
  size_t romSize_ = 0;
  size_t saveSize_ = 0;
  size_t stateSize_ = 0;
  uint32_t romCrc_ = 0;
  bool stateDirty_ = false;
  bool batteryLoaded_ = false;
};

}
#endif

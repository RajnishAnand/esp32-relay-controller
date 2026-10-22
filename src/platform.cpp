#include "platform.h"

#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "log.h"

namespace platform {
namespace {

struct TaskStart {
  void (*fn)(void*);
  void* arg;
};

void taskTrampoline(void* raw) {
  TaskStart start = *static_cast<TaskStart*>(raw);
  delete static_cast<TaskStart*>(raw);
  start.fn(start.arg);
  vTaskDelete(NULL);
}

unsigned long loopMax = 0;

}  // namespace

void Mutex::init() {
  if (handle_ == nullptr) handle_ = xSemaphoreCreateMutex();
}

void Mutex::lock() {
  if (handle_ != nullptr) xSemaphoreTake(static_cast<SemaphoreHandle_t>(handle_), portMAX_DELAY);
}

void Mutex::unlock() {
  if (handle_ != nullptr) xSemaphoreGive(static_cast<SemaphoreHandle_t>(handle_));
}

bool startTask(const char* name, void (*fn)(void*), void* arg, uint32_t stackBytes) {
  TaskStart* start = new TaskStart{fn, arg};
  if (xTaskCreatePinnedToCore(taskTrampoline, name, stackBytes, start, 1, NULL, 0) != pdPASS) {
    delete start;
    return false;
  }
  return true;
}

void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1); }

void noteLoopDuration(unsigned long ms) {
  if (ms > loopMax) loopMax = ms;
}

unsigned long loopMaxMs() { return loopMax; }

uint32_t minFreeHeap() { return ESP.getMinFreeHeap(); }

uint32_t maxAllocHeap() { return ESP.getMaxAllocHeap(); }

void beginWatchdog() {
  if (esp_task_wdt_init(cfg::kWatchdogTimeoutS, true) == ESP_OK && esp_task_wdt_add(NULL) == ESP_OK) {
    LOGI("watchdog armed: %u s", (unsigned)cfg::kWatchdogTimeoutS);
  } else {
    LOGW("watchdog not armed");
  }
}

void feed() { esp_task_wdt_reset(); }

void restart() { ESP.restart(); }

time_t epochNow() { return time(nullptr); }

uint32_t freeHeap() { return ESP.getFreeHeap(); }

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "power_on";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "other";
  }
}

bool elapsed(unsigned long since, unsigned long durationMs) { return millis() - since >= durationMs; }

}  // namespace platform

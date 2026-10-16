#include "platform.h"

#include <esp_system.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "log.h"

namespace platform {

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

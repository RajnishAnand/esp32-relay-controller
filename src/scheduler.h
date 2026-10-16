#pragma once

#include <Arduino.h>
#include <time.h>

struct Schedule {
  bool enabled;
  uint8_t onHour;
  uint8_t onMinute;
  uint8_t offHour;
  uint8_t offMinute;
};

namespace scheduler {
void begin();
void loop();
void startNtp();
void noteManualChange(uint8_t index);
const Schedule& get(uint8_t index);
void set(uint8_t index, const Schedule& schedule);
bool synced();
bool localTime(struct tm& out);
}  // namespace scheduler

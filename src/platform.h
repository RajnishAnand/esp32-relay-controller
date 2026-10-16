#pragma once

#include <Arduino.h>
#include <time.h>

namespace platform {
void beginWatchdog();
void feed();
void restart();
time_t epochNow();
uint32_t freeHeap();
const char* resetReason();
bool elapsed(unsigned long since, unsigned long durationMs);
}  // namespace platform

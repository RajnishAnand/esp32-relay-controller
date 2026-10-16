#pragma once

#include <Arduino.h>

namespace relays {
void begin();
void loop();
bool busy();
bool get(uint8_t index);
void set(uint8_t index, bool on);
void setAll(bool on);
const String& name(uint8_t index);
void rename(uint8_t index, const String& name);
}  // namespace relays

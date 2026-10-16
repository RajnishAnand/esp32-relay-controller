#pragma once

#include <Arduino.h>

namespace network {
void begin();
void loop();
bool connected();
bool apActive();
String ip();
int rssi();
void setOnConnected(void (*callback)());
}  // namespace network

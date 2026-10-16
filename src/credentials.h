#pragma once

#include <Arduino.h>

namespace credentials {
void begin();
String wifiSsid();
String wifiPassword();
String apPassword();
String apiToken();
bool wifiConfigured();
bool apPasswordValid();
bool apiTokenValid();
}  // namespace credentials

#pragma once

#include <Arduino.h>

namespace credentials {
void begin();
void seed(const char* wifiSsid, const char* wifiPassword, const char* apPassword, const char* apiToken);
String wifiSsid();
String wifiPassword();
String apPassword();
String apiToken();
String guestPassword();
bool wifiConfigured();
bool apPasswordValid();
bool apiTokenValid();
bool guestEnabled();
void setWifi(const String& ssid, const String& password);
bool setGuestPassword(const String& password);
void clearGuestPassword();
}  // namespace credentials

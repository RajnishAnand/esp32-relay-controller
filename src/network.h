#pragma once

#include <Arduino.h>

namespace network {

enum class ChangeState : uint8_t { Idle, Trying, Succeeded, Failed };
enum class ScanState : uint8_t { Idle, Running, Done };

struct ChangeStatus {
  ChangeState state;
  String message;
};

struct ScanResult {
  String ssid;
  int rssi;
  bool secure;
  int channel;
};

void begin();
void loop();
bool connected();
bool apActive();
String ip();
int rssi();
void setOnConnected(void (*callback)());
void setPump(void (*pump)());

bool requestConnect(const String& ssid, const String& password);
ChangeStatus changeStatus();

bool startScan();
ScanState scanState();
size_t scanResults(ScanResult* out, size_t max);

}  // namespace network

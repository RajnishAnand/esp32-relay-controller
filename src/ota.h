#pragma once

#include <Arduino.h>

namespace ota {

enum class State : uint8_t { Idle, Checking, UpToDate, Available, Downloading, Verifying, Rebooting, Error };

struct Status {
  State state;
  const char* stateName;
  String latest;
  String message;
  uint8_t progress;
  bool configured;
  bool pendingConfirm;
  long lastCheckSecondsAgo;
};

void begin();
void loop();
bool requestCheck();
bool requestApply();
Status status();

}  // namespace ota

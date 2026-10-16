#include "led.h"

#include <Arduino.h>

#include "config.h"

namespace led {
namespace {
constexpr uint8_t kChannel = 0;
bool connectedState = false;
}  // namespace

void begin() {
  ledcSetup(kChannel, 5000, 8);
  ledcAttachPin(cfg::kStatusLedPin, kChannel);
  update();
}

void setConnected(bool connected) { connectedState = connected; }

void update() {
  if (!connectedState) {
    ledcWrite(kChannel, 255);
    return;
  }
  ledcWrite(kChannel, (millis() % 1600) < 150 ? 25 : 0);
}

}  // namespace led

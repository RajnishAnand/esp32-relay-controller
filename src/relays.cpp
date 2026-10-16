#include "relays.h"

#include <Arduino.h>

#include "config.h"
#include "platform.h"
#include "storage.h"

namespace relays {
namespace {

bool target[cfg::kRelayCount];
bool applied[cfg::kRelayCount];
String names[cfg::kRelayCount];
uint8_t pendingOn = 0;
unsigned long lastOnAt = 0;
bool hasLastOn = false;

uint8_t level(bool on) { return (cfg::kRelayActiveLow ? !on : on) ? HIGH : LOW; }

void drive(uint8_t i) { digitalWrite(cfg::kRelayPins[i], level(applied[i])); }

void makeKey(char* buf, size_t size, char prefix, uint8_t i) {
  snprintf(buf, size, "%c%u", prefix, (unsigned)i);
}

void request(uint8_t i, bool on, bool persist) {
  target[i] = on;
  if (persist) {
    char key[8];
    makeKey(key, sizeof(key), 'r', i);
    storage::prefs().putBool(key, on);
  }
  if (!on) {
    pendingOn &= (uint8_t) ~(1u << i);
    applied[i] = false;
    drive(i);
  } else if (!applied[i]) {
    pendingOn |= (uint8_t)(1u << i);
  }
}

}  // namespace

void begin() {
  Preferences& prefs = storage::prefs();
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) {
    char key[8];
    makeKey(key, sizeof(key), 'r', i);
    bool saved = prefs.getBool(key, false);
    makeKey(key, sizeof(key), 'n', i);
    char fallback[12];
    snprintf(fallback, sizeof(fallback), "Relay %u", (unsigned)(i + 1));
    names[i] = prefs.getString(key, fallback);

    target[i] = false;
    applied[i] = false;
    digitalWrite(cfg::kRelayPins[i], level(false));
    pinMode(cfg::kRelayPins[i], OUTPUT);
    if (saved) request(i, true, false);
  }
  while (pendingOn) {
    loop();
    delay(10);
  }
}

void loop() {
  if (!pendingOn) return;
  if (hasLastOn && !platform::elapsed(lastOnAt, cfg::kRelayStaggerMs)) return;
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) {
    if (pendingOn & (1u << i)) {
      pendingOn &= (uint8_t) ~(1u << i);
      applied[i] = true;
      drive(i);
      lastOnAt = millis();
      hasLastOn = true;
      return;
    }
  }
}

bool busy() { return pendingOn != 0; }

bool get(uint8_t index) { return index < cfg::kRelayCount && target[index]; }

void set(uint8_t index, bool on) {
  if (index < cfg::kRelayCount) request(index, on, true);
}

void setAll(bool on) {
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) request(i, on, true);
}

const String& name(uint8_t index) { return names[index < cfg::kRelayCount ? index : 0]; }

void rename(uint8_t index, const String& value) {
  if (index >= cfg::kRelayCount) return;
  names[index] = value;
  char key[8];
  makeKey(key, sizeof(key), 'n', index);
  storage::prefs().putString(key, value);
}

}  // namespace relays

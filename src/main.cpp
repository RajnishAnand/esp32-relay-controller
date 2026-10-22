#include <Arduino.h>

#include "api.h"
#include "config.h"
#include "credentials.h"
#include "led.h"
#include "log.h"
#include "network.h"
#include "ota.h"
#include "platform.h"
#include "relays.h"
#include "scheduler.h"
#include "storage.h"
#include "version.h"

namespace {

void pump() {
  api::loop();
  led::update();
  scheduler::loop();
  relays::loop();
  platform::feed();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  LOGI("relay controller v%s", FIRMWARE_VERSION);
  if (!storage::begin()) LOGE("storage init failed");
  ota::begin();
  delay(cfg::kBootSettleMs);
  led::begin();
  credentials::begin();
  relays::begin();
  scheduler::begin();
  network::setOnConnected(scheduler::startNtp);
  network::begin();
  api::begin();
  network::setPump(pump);
  platform::beginWatchdog();
}

void loop() {
  unsigned long start = millis();
  pump();
  network::loop();
  ota::loop();
  platform::noteLoopDuration(millis() - start);
}

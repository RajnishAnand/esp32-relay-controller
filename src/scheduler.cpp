#include "scheduler.h"

#include "config.h"
#include "log.h"
#include "platform.h"
#include "relays.h"
#include "storage.h"

namespace scheduler {
namespace {

constexpr time_t kMinValidEpoch = 1700000000;

Schedule schedules[cfg::kRelayCount];
bool manualTouched[cfg::kRelayCount];
bool timeSynced = false;
bool primed = false;
long lastMinute = 0;
unsigned long lastStampMs = 0;

void makeKey(char* buf, size_t size, const char* prefix, uint8_t i) {
  snprintf(buf, size, "%s%u", prefix, (unsigned)i);
}

void save(uint8_t i) {
  Preferences& prefs = storage::prefs();
  char key[10];
  makeKey(key, sizeof(key), "se", i);
  prefs.putBool(key, schedules[i].enabled);
  makeKey(key, sizeof(key), "soh", i);
  prefs.putUChar(key, schedules[i].onHour);
  makeKey(key, sizeof(key), "som", i);
  prefs.putUChar(key, schedules[i].onMinute);
  makeKey(key, sizeof(key), "sfh", i);
  prefs.putUChar(key, schedules[i].offHour);
  makeKey(key, sizeof(key), "sfm", i);
  prefs.putUChar(key, schedules[i].offMinute);
}

void stampAlive(time_t now) { storage::prefs().putULong("alive", (uint32_t)now); }

time_t lastOccurrence(time_t now, uint8_t hour, uint8_t minute) {
  long local = (long)now + cfg::kGmtOffsetSec;
  long dayStart = local - (local % 86400L);
  time_t t = (time_t)(dayStart + hour * 3600L + minute * 60L - cfg::kGmtOffsetSec);
  if (t > now) t -= 86400;
  return t;
}

void catchUp(time_t now) {
  uint32_t lastAlive = storage::prefs().getULong("alive", 0);
  if (lastAlive == 0) {
    LOGI("catch-up skipped: no previous uptime record");
    return;
  }
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) {
    if (!schedules[i].enabled || manualTouched[i]) continue;
    time_t lastOn = lastOccurrence(now, schedules[i].onHour, schedules[i].onMinute);
    time_t lastOff = lastOccurrence(now, schedules[i].offHour, schedules[i].offMinute);
    bool latestIsOn = lastOn >= lastOff;
    time_t latest = latestIsOn ? lastOn : lastOff;
    if (latest <= (time_t)lastAlive) continue;
    if (relays::get(i) == latestIsOn) continue;
    relays::set(i, latestIsOn);
    LOGI("catch-up: relay %u -> %s", (unsigned)i, latestIsOn ? "on" : "off");
  }
}

bool fireMinute(int minuteOfDay) {
  bool fired = false;
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) {
    if (!schedules[i].enabled) continue;
    int onMinute = schedules[i].onHour * 60 + schedules[i].onMinute;
    int offMinute = schedules[i].offHour * 60 + schedules[i].offMinute;
    if (minuteOfDay == onMinute) {
      relays::set(i, true);
      fired = true;
      LOGI("schedule: relay %u on", (unsigned)i);
    } else if (minuteOfDay == offMinute) {
      relays::set(i, false);
      fired = true;
      LOGI("schedule: relay %u off", (unsigned)i);
    }
  }
  return fired;
}

}  // namespace

void begin() {
  Preferences& prefs = storage::prefs();
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) {
    char key[10];
    makeKey(key, sizeof(key), "se", i);
    schedules[i].enabled = prefs.getBool(key, false);
    makeKey(key, sizeof(key), "soh", i);
    schedules[i].onHour = prefs.getUChar(key, 7);
    makeKey(key, sizeof(key), "som", i);
    schedules[i].onMinute = prefs.getUChar(key, 0);
    makeKey(key, sizeof(key), "sfh", i);
    schedules[i].offHour = prefs.getUChar(key, 22);
    makeKey(key, sizeof(key), "sfm", i);
    schedules[i].offMinute = prefs.getUChar(key, 0);
    manualTouched[i] = false;
  }
}

void startNtp() { configTime(cfg::kGmtOffsetSec, 0, cfg::kNtpServer1, cfg::kNtpServer2); }

void loop() {
  time_t now = platform::epochNow();
  if (!timeSynced) {
    if (now < kMinValidEpoch) return;
    timeSynced = true;
    LOGI("time synced");
  }

  long minute = ((long)now + cfg::kGmtOffsetSec) / 60;

  if (!primed) {
    primed = true;
    catchUp(now);
    stampAlive(now);
    lastStampMs = millis();
    lastMinute = minute;
    return;
  }

  if (platform::elapsed(lastStampMs, cfg::kAliveStampIntervalMs)) {
    lastStampMs = millis();
    stampAlive(now);
  }

  if (minute == lastMinute) return;

  long from = lastMinute + 1;
  if (minute - lastMinute > cfg::kMaxScheduleGapMinutes || minute < lastMinute) from = minute;

  bool fired = false;
  for (long m = from; m <= minute; m++) fired |= fireMinute((int)(m % 1440L));
  lastMinute = minute;
  if (fired) stampAlive(now);
}

void noteManualChange(uint8_t index) {
  if (index < cfg::kRelayCount) manualTouched[index] = true;
}

const Schedule& get(uint8_t index) { return schedules[index < cfg::kRelayCount ? index : 0]; }

void set(uint8_t index, const Schedule& schedule) {
  if (index >= cfg::kRelayCount) return;
  schedules[index] = schedule;
  save(index);
  if (timeSynced) stampAlive(platform::epochNow());
}

bool synced() { return timeSynced; }

bool localTime(struct tm& out) {
  if (!timeSynced) return false;
  time_t local = platform::epochNow() + cfg::kGmtOffsetSec;
  gmtime_r(&local, &out);
  return true;
}

}  // namespace scheduler

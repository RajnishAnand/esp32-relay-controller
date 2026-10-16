#include <config.h>

#include "../../../src/relays.h"
#include "../../../src/scheduler.h"
#include "testlib.h"

static void settle() {
  for (int i = 0; i < 12; i++) {
    relays::loop();
    delay(450);
  }
}

static void bootWorld() {
  relays::begin();
  scheduler::begin();
}

static void setSchedule(uint8_t i, bool enabled, uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM) {
  Schedule s = {enabled, onH, onM, offH, offM};
  scheduler::set(i, s);
}

static time_t at(int h, int m, int s = 0) { return localEpoch(2026, 1, 5, h, m, s); }

TEST(scheduler_fires_on_and_off_at_their_minute) {
  bootWorld();
  setSchedule(0, true, 18, 30, 23, 0);
  g_epoch = at(18, 29, 50);
  scheduler::loop();
  CHECK(!relays::get(0));
  g_epoch = at(18, 30, 5);
  scheduler::loop();
  settle();
  CHECK(relays::get(0));
  g_epoch = at(23, 0, 30);
  scheduler::loop();
  CHECK(!relays::get(0));
}

TEST(scheduler_disabled_schedule_never_fires) {
  bootWorld();
  setSchedule(0, false, 18, 30, 23, 0);
  g_epoch = at(18, 29, 50);
  scheduler::loop();
  g_epoch = at(18, 31, 0);
  scheduler::loop();
  settle();
  CHECK(!relays::get(0));
}

TEST(scheduler_is_inert_until_time_is_synced) {
  bootWorld();
  setSchedule(0, true, 0, 0, 0, 1);
  g_epoch = 1000;
  scheduler::loop();
  CHECK(!scheduler::synced());
  g_epoch = at(10, 0);
  scheduler::loop();
  CHECK(scheduler::synced());
}

TEST(scheduler_catches_up_off_event_missed_during_outage) {
  g_nvs["r0"] = "1";
  bootWorld();
  setSchedule(0, true, 7, 0, 22, 0);
  g_nvs["alive"] = std::to_string(at(21, 50));
  g_epoch = at(22, 30);
  scheduler::loop();
  CHECK(!relays::get(0));
}

TEST(scheduler_catches_up_on_event_missed_during_outage) {
  bootWorld();
  setSchedule(0, true, 7, 0, 22, 0);
  g_nvs["alive"] = std::to_string(at(6, 50));
  g_epoch = at(8, 0);
  scheduler::loop();
  settle();
  CHECK(relays::get(0));
}

TEST(scheduler_does_not_replay_events_that_already_ran) {
  g_nvs["r0"] = "1";
  bootWorld();
  setSchedule(0, true, 7, 0, 22, 0);
  g_nvs["alive"] = std::to_string(at(22, 10));
  g_epoch = at(22, 30);
  scheduler::loop();
  CHECK(relays::get(0));
}

TEST(scheduler_catch_up_needs_an_uptime_record) {
  g_nvs["r0"] = "1";
  bootWorld();
  setSchedule(0, true, 7, 0, 22, 0);
  g_epoch = at(22, 30);
  scheduler::loop();
  CHECK(relays::get(0));
}

TEST(scheduler_catch_up_skips_relays_the_user_touched) {
  g_nvs["r0"] = "1";
  bootWorld();
  setSchedule(0, true, 7, 0, 22, 0);
  g_nvs["alive"] = std::to_string(at(21, 50));
  scheduler::noteManualChange(0);
  g_epoch = at(22, 30);
  scheduler::loop();
  CHECK(relays::get(0));
}

TEST(scheduler_catch_up_handles_overnight_windows) {
  g_nvs["r0"] = "1";
  bootWorld();
  setSchedule(0, true, 22, 0, 6, 0);
  g_nvs["alive"] = std::to_string(at(5, 0));
  g_epoch = at(7, 0);
  scheduler::loop();
  CHECK(!relays::get(0));
}

TEST(scheduler_catch_up_overnight_window_turns_on) {
  bootWorld();
  setSchedule(0, true, 22, 0, 6, 0);
  g_nvs["alive"] = std::to_string(at(20, 0));
  g_epoch = at(23, 0);
  scheduler::loop();
  settle();
  CHECK(relays::get(0));
}

TEST(scheduler_processes_minutes_skipped_while_loop_was_blocked) {
  bootWorld();
  setSchedule(0, true, 18, 30, 23, 0);
  g_epoch = at(18, 29, 50);
  scheduler::loop();
  g_epoch = at(18, 33, 0);
  scheduler::loop();
  settle();
  CHECK(relays::get(0));
}

TEST(scheduler_does_not_replay_after_a_large_clock_jump) {
  bootWorld();
  setSchedule(0, true, 18, 30, 23, 0);
  g_epoch = at(18, 29, 50);
  scheduler::loop();
  g_epoch = at(20, 0, 0);
  scheduler::loop();
  settle();
  CHECK(!relays::get(0));
}

TEST(scheduler_survives_the_clock_going_backwards) {
  bootWorld();
  setSchedule(0, true, 18, 30, 23, 0);
  g_epoch = at(19, 0, 0);
  scheduler::loop();
  g_epoch = at(18, 0, 0);
  scheduler::loop();
  g_epoch = at(18, 30, 10);
  scheduler::loop();
  settle();
  CHECK(relays::get(0));
}

TEST(scheduler_stamps_uptime_periodically_and_after_events) {
  bootWorld();
  setSchedule(0, true, 18, 30, 23, 0);
  g_epoch = at(18, 29, 50);
  scheduler::loop();
  CHECK(g_nvs["alive"] == std::to_string(g_epoch));
  g_epoch = at(18, 30, 5);
  scheduler::loop();
  CHECK(g_nvs["alive"] == std::to_string(g_epoch));
  delay(cfg::kAliveStampIntervalMs + 1);
  g_epoch = at(18, 31, 0);
  scheduler::loop();
  CHECK(g_nvs["alive"] == std::to_string(g_epoch));
}

TEST(scheduler_persists_schedules) {
  bootWorld();
  setSchedule(2, true, 5, 45, 21, 15);
  scheduler::begin();
  const Schedule& s = scheduler::get(2);
  CHECK(s.enabled && s.onHour == 5 && s.onMinute == 45 && s.offHour == 21 && s.offMinute == 15);
}

TEST(scheduler_reports_local_time) {
  bootWorld();
  g_epoch = at(14, 3, 20);
  scheduler::loop();
  struct tm t;
  CHECK(scheduler::localTime(t));
  CHECK(t.tm_hour == 14 && t.tm_min == 3);
  scheduler::startNtp();
  CHECK(g_configTimeCalls == 1);
}

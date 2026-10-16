#include <config.h>

#include "../../../src/relays.h"
#include "testlib.h"

static std::vector<PinWrite> activeWrites() {
  std::vector<PinWrite> out;
  for (size_t i = 0; i < g_pinWrites.size(); i++)
    if (g_pinWrites[i].level == LOW) out.push_back(g_pinWrites[i]);
  return out;
}

static void settle() {
  for (int i = 0; i < 12; i++) {
    relays::loop();
    delay(450);
  }
}

TEST(relays_boot_drives_inactive_before_anything_else) {
  g_nvs["r0"] = "1";
  g_nvs["r2"] = "1";
  relays::begin();
  CHECK(g_pinWrites.size() >= 4);
  for (size_t i = 0; i < 4; i++) CHECK(g_pinWrites[i].level == HIGH);
}

TEST(relays_boot_restore_is_staggered) {
  g_nvs["r0"] = "1";
  g_nvs["r1"] = "1";
  g_nvs["r2"] = "1";
  relays::begin();
  std::vector<PinWrite> on = activeWrites();
  CHECK(on.size() == 3);
  if (on.size() == 3) {
    CHECK(on[0].pin == 27 && on[1].pin == 26 && on[2].pin == 14);
    CHECK(on[1].t - on[0].t >= cfg::kRelayStaggerMs);
    CHECK(on[2].t - on[1].t >= cfg::kRelayStaggerMs);
  }
  CHECK(relays::get(0) && relays::get(1) && relays::get(2) && !relays::get(3));
  CHECK(!relays::busy());
}

TEST(relays_off_is_immediate_and_persisted) {
  relays::begin();
  relays::set(1, true);
  settle();
  CHECK(g_nvs["r1"] == "1");
  g_pinWrites.clear();
  relays::set(1, false);
  CHECK(g_pinWrites.size() == 1 && g_pinWrites[0].pin == 26 && g_pinWrites[0].level == HIGH);
  CHECK(!relays::get(1));
  CHECK(g_nvs["r1"] == "0");
}

TEST(relays_set_all_staggers_on_transitions) {
  relays::begin();
  g_pinWrites.clear();
  relays::setAll(true);
  CHECK(relays::busy());
  for (uint8_t i = 0; i < 4; i++) CHECK(relays::get(i));
  relays::loop();
  CHECK(activeWrites().size() == 1);
  relays::loop();
  CHECK(activeWrites().size() == 1);
  delay(cfg::kRelayStaggerMs - 1);
  relays::loop();
  CHECK(activeWrites().size() == 1);
  delay(1);
  relays::loop();
  CHECK(activeWrites().size() == 2);
  settle();
  CHECK(activeWrites().size() == 4);
  CHECK(!relays::busy());
}

TEST(relays_off_cancels_a_pending_on) {
  relays::begin();
  g_pinWrites.clear();
  relays::setAll(true);
  relays::loop();
  relays::set(3, false);
  settle();
  CHECK(activeWrites().size() == 3);
  CHECK(!relays::get(3));
}

TEST(relays_names_persist_and_reload) {
  relays::begin();
  CHECK(relays::name(0) == "Relay 1");
  relays::rename(2, "Pump");
  CHECK(g_nvs["n2"] == "Pump");
  relays::begin();
  CHECK(relays::name(2) == "Pump");
}

TEST(relays_ignore_out_of_range) {
  relays::begin();
  relays::set(9, true);
  CHECK(!relays::busy());
  CHECK(!relays::get(9));
}

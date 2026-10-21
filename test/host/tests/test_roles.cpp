#include "api_helpers.h"

static const char* kGuest = "guest-pass-1234";

static void enableGuest() { CHECK(call(HTTP_POST, "/api/guest/password", args("password", kGuest)).code == 200); }

TEST(roles_guest_can_view_and_switch_only) {
  boot();
  enableGuest();
  CHECK(call(HTTP_GET, "/api/status", Args(), kGuest).code == 200);
  CHECK(call(HTTP_GET, "/api/set", args("relay", "2", "state", "1"), kGuest).code == 200);
  CHECK(relays::get(2));
  CHECK(call(HTTP_GET, "/api/all", args("state", "0"), kGuest).code == 200);
  CHECK(!relays::get(2));

  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", "x"), kGuest).code == 403);
  CHECK(call(HTTP_GET, "/api/schedule", args("relay", "0", "enabled", "1", "on", "07:00", "off", "08:00"), kGuest).code == 403);
  CHECK(call(HTTP_GET, "/api/info", Args(), kGuest).code == 403);
  CHECK(call(HTTP_GET, "/api/guest", Args(), kGuest).code == 403);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "another-pass-1"), kGuest).code == 403);
  CHECK(call(HTTP_POST, "/api/guest/disable", Args(), kGuest).code == 403);
  CHECK(call(HTTP_GET, "/api/wifi/status", Args(), kGuest).code == 403);
  CHECK(call(HTTP_POST, "/api/wifi/scan", Args(), kGuest).code == 403);
  CHECK(call(HTTP_GET, "/api/wifi/scan", Args(), kGuest).code == 403);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "x"), kGuest).code == 403);
  CHECK(call(HTTP_GET, "/api/update/status", Args(), kGuest).code == 403);
  CHECK(call(HTTP_POST, "/api/update/check", Args(), kGuest).code == 403);
  CHECK(call(HTTP_POST, "/api/update/apply", Args(), kGuest).code == 403);
  CHECK(relays::name(0) == "Relay 1");
  CHECK(!scheduler::get(0).enabled);
}

TEST(roles_admin_can_use_every_endpoint) {
  boot();
  CHECK(call(HTTP_GET, "/api/status").code == 200);
  CHECK(call(HTTP_GET, "/api/info").code == 200);
  CHECK(call(HTTP_GET, "/api/guest").code == 200);
  CHECK(call(HTTP_GET, "/api/wifi/status").code == 200);
  CHECK(call(HTTP_GET, "/api/wifi/scan").code == 200);
  CHECK(call(HTTP_GET, "/api/update/status").code == 200);
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", "x")).code == 200);
}

TEST(roles_status_reports_the_callers_role) {
  boot();
  enableGuest();
  CHECK(has(call(HTTP_GET, "/api/status").body, "\"role\":\"admin\""));
  CHECK(has(call(HTTP_GET, "/api/status", Args(), kGuest).body, "\"role\":\"guest\""));
  CHECK(isValidJson(call(HTTP_GET, "/api/status", Args(), kGuest).body));
}

TEST(roles_guest_password_rules) {
  boot();
  CHECK(call(HTTP_POST, "/api/guest/password").code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "short")).code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "1234567")).code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", " leading-space")).code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "trailing-space ")).code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "has\x01" "control")).code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "bad-utf8-\xFF\xFE")).code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", std::string(65, 'a').c_str())).code == 400);
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", kToken)).code == 400);
  CHECK(has(call(HTTP_GET, "/api/guest").body, "\"enabled\":false"));
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "caf\xC3\xA9-pass-9")).code == 200);
  CHECK(call(HTTP_GET, "/api/status", Args(), "caf\xC3\xA9-pass-9").code == 200);
}

TEST(roles_guest_can_be_disabled_and_changed) {
  boot();
  CHECK(has(call(HTTP_GET, "/api/guest").body, "\"enabled\":false"));
  enableGuest();
  CHECK(has(call(HTTP_GET, "/api/guest").body, "\"enabled\":true"));
  CHECK(call(HTTP_POST, "/api/guest/password", args("password", "second-pass-22")).code == 200);
  CHECK(call(HTTP_GET, "/api/status", Args(), kGuest).code == 401);
  CHECK(call(HTTP_GET, "/api/status", Args(), "second-pass-22").code == 200);
  CHECK(call(HTTP_POST, "/api/guest/disable").code == 200);
  CHECK(call(HTTP_GET, "/api/status", Args(), "second-pass-22").code == 401);
  CHECK(has(call(HTTP_GET, "/api/guest").body, "\"enabled\":false"));
}

TEST(roles_passwords_are_never_returned) {
  boot();
  enableGuest();
  const char* paths[] = {"/api/status", "/api/info", "/api/guest", "/api/wifi/status", "/api/update/status"};
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    HttpResponse r = call(HTTP_GET, paths[i]);
    CHECK(!has(r.body, kGuest));
    CHECK(!has(r.body, kToken));
  }
}

TEST(roles_guest_login_locks_after_repeated_failures_but_admin_does_not) {
  boot();
  enableGuest();
  for (int i = 0; i < 8; i++) CHECK(call(HTTP_GET, "/api/status", Args(), "wrong-guess-here").code == 401);
  CHECK(call(HTTP_GET, "/api/status", Args(), kGuest).code == 429);
  CHECK(call(HTTP_GET, "/api/status", Args(), "wrong-guess-here").code == 429);
  CHECK(call(HTTP_GET, "/api/status").code == 200);
  delay(cfg::kGuestLockoutMs - 1000);
  CHECK(call(HTTP_GET, "/api/status", Args(), kGuest).code == 429);
  delay(2000);
  CHECK(call(HTTP_GET, "/api/status", Args(), kGuest).code == 200);
}

TEST(roles_lockout_counter_resets_after_the_window) {
  boot();
  enableGuest();
  for (int i = 0; i < 7; i++) call(HTTP_GET, "/api/status", Args(), "wrong-guess-here");
  delay(cfg::kGuestLockoutMs + 1);
  for (int i = 0; i < 7; i++) CHECK(call(HTTP_GET, "/api/status", Args(), "wrong-guess-here").code == 401);
  CHECK(call(HTTP_GET, "/api/status", Args(), kGuest).code == 200);
}

TEST(roles_requests_without_a_key_never_trigger_lockout) {
  boot();
  enableGuest();
  for (int i = 0; i < 30; i++) CHECK(call(HTTP_GET, "/api/status", Args(), nullptr).code == 401);
  CHECK(call(HTTP_GET, "/api/status", Args(), kGuest).code == 200);
}

TEST(roles_wrong_keys_are_plain_401_when_guest_is_disabled) {
  boot();
  for (int i = 0; i < 30; i++) CHECK(call(HTTP_GET, "/api/status", Args(), "wrong-guess-here").code == 401);
}

TEST(roles_guest_switching_counts_as_a_manual_change) {
  g_nvs["r0"] = "1";
  boot();
  enableGuest();
  Schedule s = {true, 7, 0, 22, 0};
  scheduler::set(0, s);
  g_nvs["alive"] = std::to_string(localEpoch(2026, 1, 5, 21, 50, 0));
  CHECK(call(HTTP_GET, "/api/set", args("relay", "0", "state", "1"), kGuest).code == 200);
  g_epoch = localEpoch(2026, 1, 5, 22, 30, 0);
  scheduler::loop();
  CHECK(relays::get(0));
}

#include <config.h>

#include <WebServer.h>

#include "../../../src/api.h"
#include "../../../src/network.h"
#include "../../../src/relays.h"
#include "../../../src/scheduler.h"
#include "testlib.h"

#include "api_helpers.h"

TEST(api_requires_the_token) {
  boot();
  CHECK(call(HTTP_GET, "/api/status", Args(), nullptr).code == 401);
  CHECK(call(HTTP_GET, "/api/status", Args(), "wrong").code == 401);
  CHECK(call(HTTP_GET, "/api/status", Args(), "0123456789abcdef0123456789abcdeg").code == 401);
  CHECK(call(HTTP_GET, "/api/status", Args(), "").code == 401);
  CHECK(call(HTTP_GET, "/api/status").code == 200);
}

TEST(api_requires_the_token_on_every_api_route) {
  boot();
  const char* routes[] = {"/api/status", "/api/info", "/api/set", "/api/all", "/api/rename", "/api/schedule",
                          "/api/guest", "/api/wifi/status", "/api/wifi/scan"};
  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
    CHECK(call(HTTP_GET, routes[i], Args(), nullptr).code == 401);
  CHECK(call(HTTP_POST, "/api/update/check", Args(), nullptr).code == 401);
  CHECK(call(HTTP_POST, "/api/guest/password", Args(), nullptr).code == 401);
  CHECK(call(HTTP_POST, "/api/guest/disable", Args(), nullptr).code == 401);
  CHECK(call(HTTP_POST, "/api/wifi/scan", Args(), nullptr).code == 401);
  CHECK(call(HTTP_POST, "/api/wifi/connect", Args(), nullptr).code == 401);
  CHECK(call(HTTP_POST, "/api/update/apply", Args(), nullptr).code == 401);
  CHECK(call(HTTP_GET, "/api/update/status", Args(), nullptr).code == 401);
}

TEST(api_fails_closed_when_not_provisioned) {
  boot();
  g_nvs.erase("api_token");
  CHECK(call(HTTP_GET, "/api/status").code == 503);
  g_nvs["api_token"] = "short";
  CHECK(call(HTTP_GET, "/api/status", Args(), "short").code == 503);
  g_nvs["api_token"] = "change-me-this-is-a-placeholder";
  CHECK(call(HTTP_GET, "/api/status", Args(), "change-me-this-is-a-placeholder").code == 503);
  CHECK(!relays::get(0));
}

TEST(api_set_parses_strictly) {
  boot();
  CHECK(call(HTTP_GET, "/api/set", args("relay", "abc", "state", "1")).code == 400);
  CHECK(call(HTTP_GET, "/api/set", args("relay", "4", "state", "1")).code == 400);
  CHECK(call(HTTP_GET, "/api/set", args("relay", "-1", "state", "1")).code == 400);
  CHECK(call(HTTP_GET, "/api/set", args("relay", "1")).code == 400);
  CHECK(call(HTTP_GET, "/api/set", args("state", "1")).code == 400);
  CHECK(call(HTTP_GET, "/api/set", args("relay", "1", "state", "maybe")).code == 400);
  CHECK(!relays::get(0) && !relays::get(1));
  CHECK(call(HTTP_GET, "/api/set", args("relay", "1", "state", "true")).code == 200);
  CHECK(relays::get(1));
  CHECK(call(HTTP_POST, "/api/set", args("relay", "1", "state", "OFF")).code == 200);
  CHECK(!relays::get(1));
  CHECK(!relays::get(0));
}

TEST(api_all_sets_every_relay_and_validates) {
  boot();
  CHECK(call(HTTP_GET, "/api/all", args("state", "x")).code == 400);
  CHECK(call(HTTP_GET, "/api/all").code == 400);
  CHECK(call(HTTP_GET, "/api/all", args("state", "1")).code == 200);
  for (uint8_t i = 0; i < 4; i++) CHECK(relays::get(i));
  CHECK(relays::busy());
  CHECK(call(HTTP_GET, "/api/all", args("state", "0")).code == 200);
  for (uint8_t i = 0; i < 4; i++) CHECK(!relays::get(i));
}

TEST(api_status_is_valid_json_with_hostile_names) {
  boot();
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", "say \"hi\" \\ back")).code == 200);
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "1", "name", "Caf\xC3\xA9 \xF0\x9F\x92\xA1")).code == 200);
  HttpResponse r = call(HTTP_GET, "/api/status");
  CHECK(r.code == 200 && r.type == "application/json");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "say \\\"hi\\\" \\\\ back"));
  CHECK(r.headers["Cache-Control"] == "no-store");
}

TEST(api_rename_enforces_the_name_rules) {
  boot();
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", "bad\x01name")).code == 400);
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", "bad\xFF")).code == 400);
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", std::string(25, 'a').c_str())).code == 400);
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", "   ")).code == 400);
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0")).code == 400);
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "9", "name", "x")).code == 400);
  CHECK(relays::name(0) == "Relay 1");
  CHECK(call(HTTP_GET, "/api/rename", args("relay", "0", "name", "  Pump  ")).code == 200);
  CHECK(relays::name(0) == "Pump");
}

TEST(api_schedule_validates_and_persists) {
  boot();
  CHECK(call(HTTP_GET, "/api/schedule", args("relay", "1", "enabled", "1", "on", "7:5", "off", "22:00")).code == 400);
  CHECK(call(HTTP_GET, "/api/schedule", args("relay", "1", "enabled", "1", "on", "07:05", "off", "25:00")).code == 400);
  CHECK(call(HTTP_GET, "/api/schedule", args("relay", "1", "enabled", "1", "on", "07:05")).code == 400);
  CHECK(call(HTTP_GET, "/api/schedule", args("relay", "1", "enabled", "x", "on", "07:05", "off", "22:00")).code == 400);
  CHECK(!scheduler::get(1).enabled);
  CHECK(call(HTTP_GET, "/api/schedule", args("relay", "1", "enabled", "true", "on", "07:05", "off", "22:30")).code == 200);
  CHECK(scheduler::get(1).enabled && scheduler::get(1).onMinute == 5 && scheduler::get(1).offMinute == 30);
  HttpResponse r = call(HTTP_GET, "/api/status");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "\"onTime\":\"07:05\""));
}

TEST(api_info_reports_device_state) {
  boot();
  HttpResponse r = call(HTTP_GET, "/api/info");
  CHECK(r.code == 200 && isValidJson(r.body));
  CHECK(has(r.body, "\"firmware\":\"1.0.0\""));
  CHECK(has(r.body, "\"synced\":false"));
  CHECK(has(r.body, "\"relayCount\":4"));
  CHECK(has(r.body, "\"minFreeHeap\":90000"));
  CHECK(has(r.body, "\"maxAllocHeap\":60000"));
  CHECK(has(r.body, "\"loopMaxMs\":"));
  g_epoch = localEpoch(2026, 1, 5, 14, 3, 0);
  scheduler::loop();
  r = call(HTTP_GET, "/api/info");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "\"local\":\"2026-01-05 14:03\""));
}

TEST(api_update_endpoints_are_post_only_and_guarded) {
  boot();
  CHECK(call(HTTP_GET, "/api/update/check").code == 404);
  CHECK(call(HTTP_GET, "/api/update/apply").code == 404);
  CHECK(call(HTTP_POST, "/api/update/apply").code == 409);
  CHECK(call(HTTP_POST, "/api/update/check").code == 200);
  CHECK(call(HTTP_POST, "/api/update/check").code == 409);
  HttpResponse r = call(HTTP_GET, "/api/update/status");
  CHECK(r.code == 200 && isValidJson(r.body));
  CHECK(has(r.body, "\"state\":\"checking\""));
  CHECK(has(r.body, "\"current\":\"1.0.0\""));
}

TEST(api_dashboard_is_public_and_sends_security_headers) {
  boot();
  HttpResponse r = call(HTTP_GET, "/", Args(), nullptr);
  CHECK(r.code == 200 && r.type == "text/html");
  CHECK(has(r.headers["Content-Security-Policy"], "default-src 'none'"));
  CHECK(has(r.headers["Content-Security-Policy"], "frame-ancestors 'none'"));
  CHECK(r.headers["X-Content-Type-Options"] == "nosniff");
  CHECK(has(r.body, "X-API-Key"));
  CHECK(!has(r.body, kToken));
}

TEST(api_unknown_routes_return_404) {
  boot();
  CHECK(call(HTTP_GET, "/nope", Args(), nullptr).code == 404);
  CHECK(call(HTTP_POST, "/api/status").code == 404 || call(HTTP_POST, "/api/status").code == 200);
}

#include "api_helpers.h"

#include "../../../src/credentials.h"

static void tick() { delay(10); }

static void bootOnHome() {
  g_wifiNetworks["home"] = "home-pass-1";
  g_wifiNetworks["office"] = "office-pass-1";
  g_nvs["wifi_ssid"] = "home";
  g_nvs["wifi_pass"] = "home-pass-1";
  boot();
  network::setPump(tick);
}

static size_t at(const std::string& text, const char* part) { return text.find(part); }

TEST(wifi_scan_runs_asynchronously_and_reports_sorted_deduplicated_networks) {
  boot();
  CHECK(call(HTTP_POST, "/api/wifi/scan").code == 200);
  CHECK(g_scanStarts == 1);
  HttpResponse r = call(HTTP_GET, "/api/wifi/scan");
  CHECK(r.code == 200 && isValidJson(r.body));
  CHECK(has(r.body, "\"state\":\"running\""));
  CHECK(call(HTTP_POST, "/api/wifi/scan").code == 200);
  CHECK(g_scanStarts == 1);

  ScanEntry e1 = {"Home", -50, 3, 6};
  ScanEntry e2 = {"Cafe", -70, 0, 1};
  ScanEntry e3 = {"Home", -40, 3, 11};
  ScanEntry e4 = {"", -30, 3, 1};
  ScanEntry e5 = {"bad\xFF", -20, 3, 1};
  ScanEntry e6 = {"say \"hi\"", -90, 3, 3};
  g_scanEntries.push_back(e6);
  g_scanEntries.push_back(e2);
  g_scanEntries.push_back(e1);
  g_scanEntries.push_back(e3);
  g_scanEntries.push_back(e4);
  g_scanEntries.push_back(e5);
  g_scanState = (int)g_scanEntries.size();

  r = call(HTTP_GET, "/api/wifi/scan");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "\"state\":\"done\""));
  CHECK(has(r.body, "{\"ssid\":\"Home\",\"rssi\":-40,\"secure\":true,\"channel\":11}"));
  CHECK(has(r.body, "{\"ssid\":\"Cafe\",\"rssi\":-70,\"secure\":false,\"channel\":1}"));
  CHECK(has(r.body, "say \\\"hi\\\""));
  CHECK(!has(r.body, "-50"));
  CHECK(!has(r.body, "-30") && !has(r.body, "-20"));
  CHECK(at(r.body, "Home") < at(r.body, "Cafe") && at(r.body, "Cafe") < at(r.body, "say"));
}

TEST(wifi_scan_keeps_only_the_strongest_networks_when_there_are_too_many) {
  boot();
  for (int i = 0; i < 30; i++) {
    ScanEntry e = {"net" + std::to_string(i), -90 + i, 3, 1};
    g_scanEntries.push_back(e);
  }
  g_scanState = 30;
  HttpResponse r = call(HTTP_GET, "/api/wifi/scan");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "\"ssid\":\"net29\""));
  CHECK(has(r.body, "\"ssid\":\"net10\""));
  CHECK(!has(r.body, "\"ssid\":\"net9\""));
}

TEST(wifi_connect_validates_its_input) {
  boot();
  CHECK(call(HTTP_POST, "/api/wifi/connect").code == 400);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "")).code == 400);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", std::string(33, 'a').c_str())).code == 400);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "bad\x01name")).code == 400);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "bad\xFF")).code == 400);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "net", "password", "1234567")).code == 400);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "net", "password", std::string(64, 'a').c_str())).code == 400);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "net", "password", "has\x01" "control")).code == 400);
  CHECK(has(call(HTTP_GET, "/api/wifi/status").body, "\"state\":\"idle\""));
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "OpenNet")).code == 200);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "Other")).code == 409);
  CHECK(call(HTTP_POST, "/api/wifi/scan").code == 409);
}

TEST(wifi_connect_to_a_new_network_saves_it_and_takes_the_static_address) {
  bootOnHome();
  CHECK(network::connected());
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "office", "password", "office-pass-1")).code == 200);
  CHECK(has(call(HTTP_GET, "/api/wifi/status").body, "\"state\":\"trying\""));
  network::loop();
  HttpResponse r = call(HTTP_GET, "/api/wifi/status");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "\"state\":\"succeeded\""));
  CHECK(has(r.body, "\"ssid\":\"office\""));
  CHECK(has(r.body, "\"connected\":true"));
  CHECK(has(r.body, "\"ip\":\"192.168.1.250\""));
  CHECK(g_nvs["wifi_ssid"] == "office" && g_nvs["wifi_pass"] == "office-pass-1");
  CHECK(!has(r.body, "office-pass-1"));
}

TEST(wifi_connect_failure_restores_the_previous_network_and_keeps_its_credentials) {
  g_nvs["ap_pass"] = "access-point-pass";
  bootOnHome();
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "office", "password", "wrong-password")).code == 200);
  network::loop();
  HttpResponse r = call(HTTP_GET, "/api/wifi/status");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "\"state\":\"failed\""));
  CHECK(has(r.body, "previous network was restored"));
  CHECK(has(r.body, "\"connected\":true"));
  CHECK(has(r.body, "\"ssid\":\"home\""));
  CHECK(g_nvs["wifi_ssid"] == "home" && g_nvs["wifi_pass"] == "home-pass-1");
  CHECK(g_softApStarts >= 1 && g_softApStops >= 1);
  CHECK(!network::apActive());
}

TEST(wifi_connect_failure_with_nothing_to_go_back_to) {
  boot();
  g_wifiNetworks["only"] = "only-pass-12";
  network::setPump(tick);
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "ghost", "password", "ghost-pass-1")).code == 200);
  network::loop();
  HttpResponse r = call(HTTP_GET, "/api/wifi/status");
  CHECK(has(r.body, "\"state\":\"failed\""));
  CHECK(has(r.body, "Could not connect to ghost"));
  CHECK(has(r.body, "\"configured\":false"));
  CHECK(g_nvs.count("wifi_ssid") == 0);
}

TEST(wifi_can_be_provisioned_on_a_device_that_has_no_network_yet) {
  g_wifiNetworks["home"] = "home-pass-1";
  boot();
  CHECK(!network::connected());
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "home", "password", "home-pass-1")).code == 200);
  network::loop();
  CHECK(network::connected());
  CHECK(g_nvs["wifi_ssid"] == "home");
  CHECK(has(call(HTTP_GET, "/api/wifi/status").body, "\"state\":\"succeeded\""));
}

TEST(wifi_open_network_needs_no_password) {
  g_wifiNetworks["Cafe"] = "";
  boot();
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "Cafe")).code == 200);
  network::loop();
  CHECK(network::connected());
  CHECK(g_nvs["wifi_ssid"] == "Cafe");
}

TEST(wifi_hostile_ssid_is_escaped_in_status) {
  g_wifiNetworks["say \"hi\" \\"] = "";
  boot();
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "say \"hi\" \\")).code == 200);
  network::loop();
  HttpResponse r = call(HTTP_GET, "/api/wifi/status");
  CHECK(isValidJson(r.body));
  CHECK(has(r.body, "say \\\"hi\\\" \\\\"));
}

TEST(wifi_switch_keeps_serving_requests_through_the_pump) {
  bootOnHome();
  static int served = 0;
  served = 0;
  network::setPump([]() {
    served++;
    delay(10);
  });
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "office", "password", "wrong-password")).code == 200);
  network::loop();
  CHECK(served > 0);
}

#include "../../../src/credentials.h"
#include "testlib.h"

TEST(credentials_seed_stores_values_and_remembers_the_secrets_it_saw) {
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "admin-token-0123456789");
  CHECK(g_nvs["wifi_ssid"] == "home");
  CHECK(g_nvs["wifi_pass"] == "home-pass-1");
  CHECK(g_nvs["ap_pass"] == "ap-pass-1234");
  CHECK(g_nvs["api_token"] == "admin-token-0123456789");
  CHECK(g_nvs.count("seed_hash") == 1);
}

TEST(credentials_seed_does_not_undo_changes_made_through_the_api) {
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "admin-token-0123456789");
  credentials::setWifi("office", "office-pass-1");
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "admin-token-0123456789");
  CHECK(credentials::wifiSsid() == "office");
  CHECK(credentials::wifiPassword() == "office-pass-1");
}

TEST(credentials_seed_applies_again_when_secrets_h_changes) {
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "admin-token-0123456789");
  credentials::setWifi("office", "office-pass-1");
  credentials::seed("home2", "home-pass-2", "ap-pass-1234", "admin-token-0123456789");
  CHECK(credentials::wifiSsid() == "home2");
  CHECK(credentials::wifiPassword() == "home-pass-2");
}

TEST(credentials_seed_with_a_new_admin_token_takes_effect) {
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "admin-token-0123456789");
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "rotated-token-9876543210");
  CHECK(credentials::apiToken() == "rotated-token-9876543210");
}

TEST(credentials_release_builds_never_touch_stored_values) {
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "admin-token-0123456789");
  std::string hash = g_nvs["seed_hash"];
  credentials::setWifi("office", "office-pass-1");
  credentials::seed("", "", "", "");
  CHECK(credentials::wifiSsid() == "office");
  CHECK(credentials::apiToken() == "admin-token-0123456789");
  CHECK(g_nvs["seed_hash"] == hash);
  credentials::seed("home", "home-pass-1", "ap-pass-1234", "admin-token-0123456789");
  CHECK(credentials::wifiSsid() == "office");
}

TEST(credentials_seed_skips_placeholders) {
  credentials::seed("your-wifi-name", "your-wifi-password", "change-me-ap-password", "real-admin-token-012345");
  CHECK(g_nvs.count("wifi_ssid") == 0);
  CHECK(g_nvs.count("ap_pass") == 0);
  CHECK(g_nvs["api_token"] == "real-admin-token-012345");
}

TEST(credentials_guest_password_round_trip) {
  CHECK(!credentials::guestEnabled());
  CHECK(credentials::setGuestPassword("guest-pass-1234"));
  CHECK(credentials::guestEnabled());
  CHECK(credentials::guestPassword() == "guest-pass-1234");
  credentials::clearGuestPassword();
  CHECK(!credentials::guestEnabled());
  g_nvs["guest_pass"] = "short";
  CHECK(!credentials::guestEnabled());
}

#include "credentials.h"

#include "config.h"
#include "storage.h"
#include "util.h"

namespace credentials {
namespace {

uint32_t fold(uint32_t hash, const char* text) {
  while (*text) {
    hash ^= (uint8_t)*text++;
    hash *= 16777619u;
  }
  hash ^= 0xFFu;
  return hash * 16777619u;
}

void seedKey(const char* key, const char* value) {
  if (value[0] == '\0' || util::isPlaceholder(value)) return;
  Preferences& prefs = storage::prefs();
  if (prefs.getString(key, "") != value) prefs.putString(key, value);
}

String read(const char* key) { return storage::prefs().getString(key, ""); }

}  // namespace

void seed(const char* wifiSsid, const char* wifiPassword, const char* apPassword, const char* apiToken) {
  if (apiToken[0] == '\0') return;
  uint32_t hash = 2166136261u;
  hash = fold(hash, wifiSsid);
  hash = fold(hash, wifiPassword);
  hash = fold(hash, apPassword);
  hash = fold(hash, apiToken);
  Preferences& prefs = storage::prefs();
  if (prefs.getULong("seed_hash", 0) == hash) return;
  seedKey("wifi_ssid", wifiSsid);
  seedKey("wifi_pass", wifiPassword);
  seedKey("ap_pass", apPassword);
  seedKey("api_token", apiToken);
  prefs.putULong("seed_hash", hash);
}

void begin() { seed(WIFI_SSID, WIFI_PASSWORD, AP_PASSWORD, API_TOKEN); }

String wifiSsid() { return read("wifi_ssid"); }
String wifiPassword() { return read("wifi_pass"); }
String apPassword() { return read("ap_pass"); }
String apiToken() { return read("api_token"); }
String guestPassword() { return read("guest_pass"); }

bool wifiConfigured() { return wifiSsid().length() > 0; }

bool apPasswordValid() {
  String pass = apPassword();
  return pass.length() >= 8 && pass.length() <= 63 && !util::isPlaceholder(pass.c_str());
}

bool apiTokenValid() {
  String token = apiToken();
  return token.length() >= 16 && !util::isPlaceholder(token.c_str());
}

bool guestEnabled() { return util::validGuestPassword(guestPassword()); }

void setWifi(const String& ssid, const String& password) {
  Preferences& prefs = storage::prefs();
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", password);
}

bool setGuestPassword(const String& password) { return storage::prefs().putString("guest_pass", password) > 0; }

void clearGuestPassword() { storage::prefs().remove("guest_pass"); }

}  // namespace credentials

#include "credentials.h"

#include "config.h"
#include "storage.h"
#include "util.h"

namespace credentials {
namespace {

void seed(const char* key, const char* value) {
  if (value[0] == '\0' || util::isPlaceholder(value)) return;
  Preferences& prefs = storage::prefs();
  if (prefs.getString(key, "") != value) prefs.putString(key, value);
}

String read(const char* key) { return storage::prefs().getString(key, ""); }

}  // namespace

void begin() {
  seed("wifi_ssid", WIFI_SSID);
  seed("wifi_pass", WIFI_PASSWORD);
  seed("ap_pass", AP_PASSWORD);
  seed("api_token", API_TOKEN);
}

String wifiSsid() { return read("wifi_ssid"); }
String wifiPassword() { return read("wifi_pass"); }
String apPassword() { return read("ap_pass"); }
String apiToken() { return read("api_token"); }

bool wifiConfigured() { return wifiSsid().length() > 0; }

bool apPasswordValid() {
  String pass = apPassword();
  return pass.length() >= 8 && pass.length() <= 63 && !util::isPlaceholder(pass.c_str());
}

bool apiTokenValid() {
  String token = apiToken();
  return token.length() >= 16 && !util::isPlaceholder(token.c_str());
}

}  // namespace credentials

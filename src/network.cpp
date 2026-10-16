#include "network.h"

#include <WiFi.h>

#include "config.h"
#include "credentials.h"
#include "led.h"
#include "log.h"
#include "platform.h"

namespace network {
namespace {

bool stationUp = false;
bool accessPointUp = false;
unsigned long lastRetryMs = 0;
void (*onConnected)() = nullptr;

bool waitConnected(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    platform::feed();
    led::update();
  }
  return WiFi.status() == WL_CONNECTED;
}

void beginStation() { WiFi.begin(credentials::wifiSsid().c_str(), credentials::wifiPassword().c_str()); }

bool connectStation() {
  WiFi.mode(WIFI_STA);
  beginStation();
  if (!waitConnected(cfg::kWifiConnectTimeoutMs)) return false;

  IPAddress leased = WiFi.localIP();
  IPAddress gateway = WiFi.gatewayIP();
  IPAddress subnet = WiFi.subnetMask();
  IPAddress dns = WiFi.dnsIP();

  if (subnet != IPAddress(255, 255, 255, 0)) {
    LOGW("subnet is not /24, keeping DHCP address");
    return true;
  }
  IPAddress fixed = leased;
  fixed[3] = cfg::kStaticHostOctet;
  if (fixed == leased) return true;

  WiFi.disconnect();
  delay(200);
  if (!WiFi.config(fixed, gateway, subnet, dns)) {
    LOGW("static address rejected, using DHCP");
    beginStation();
    return waitConnected(cfg::kWifiConnectTimeoutMs);
  }
  beginStation();
  if (waitConnected(cfg::kWifiConnectTimeoutMs)) return true;
  LOGW("static address %s did not connect (conflict?)", fixed.toString().c_str());
  return false;
}

String apSsid() {
  uint64_t mac = ESP.getEfuseMac();
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
  return String(cfg::kApSsidPrefix) + suffix;
}

void startAccessPoint() {
  if (accessPointUp) return;
  if (!credentials::apPasswordValid()) {
    LOGW("access point disabled: AP password not provisioned");
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid().c_str(), credentials::apPassword().c_str());
  accessPointUp = true;
  LOGI("access point %s at %s", apSsid().c_str(), WiFi.softAPIP().toString().c_str());
}

void stopAccessPoint() {
  if (!accessPointUp) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  accessPointUp = false;
}

}  // namespace

void begin() {
  WiFi.persistent(false);
  led::setConnected(false);
  if (!credentials::wifiConfigured()) {
    LOGW("no WiFi credentials provisioned");
    startAccessPoint();
    return;
  }
  if (connectStation()) {
    stationUp = true;
    led::setConnected(true);
    LOGI("connected, ip %s", WiFi.localIP().toString().c_str());
    if (onConnected) onConnected();
  } else {
    startAccessPoint();
  }
}

void loop() {
  if (!credentials::wifiConfigured()) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!stationUp) {
      stationUp = true;
      led::setConnected(true);
      LOGI("reconnected, ip %s", WiFi.localIP().toString().c_str());
      if (onConnected) onConnected();
      stopAccessPoint();
    }
    return;
  }

  if (stationUp) {
    stationUp = false;
    led::setConnected(false);
    LOGW("WiFi lost");
  }
  startAccessPoint();
  if (platform::elapsed(lastRetryMs, cfg::kWifiRetryIntervalMs)) {
    lastRetryMs = millis();
    beginStation();
  }
}

bool connected() { return stationUp && WiFi.status() == WL_CONNECTED; }

bool apActive() { return accessPointUp; }

String ip() { return connected() ? WiFi.localIP().toString() : String(); }

int rssi() { return connected() ? WiFi.RSSI() : 0; }

void setOnConnected(void (*callback)()) { onConnected = callback; }

}  // namespace network

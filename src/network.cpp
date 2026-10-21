#include "network.h"

#include <WiFi.h>

#include "config.h"
#include "credentials.h"
#include "led.h"
#include "log.h"
#include "platform.h"
#include "util.h"

namespace network {
namespace {

bool stationUp = false;
bool accessPointUp = false;
unsigned long lastRetryMs = 0;
void (*onConnected)() = nullptr;
void (*pumpFn)() = nullptr;

bool changePending = false;
String pendingSsid;
String pendingPassword;
ChangeState changeState = ChangeState::Idle;
String changeMessage;

bool waitConnected(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    if (pumpFn) {
      pumpFn();
      delay(20);
    } else {
      delay(250);
    }
    platform::feed();
    led::update();
  }
  return WiFi.status() == WL_CONNECTED;
}

void beginStation(const String& ssid, const String& password) { WiFi.begin(ssid.c_str(), password.c_str()); }

void beginSaved() { beginStation(credentials::wifiSsid(), credentials::wifiPassword()); }

bool connectWith(const String& ssid, const String& password) {
  WiFi.mode(accessPointUp ? WIFI_AP_STA : WIFI_STA);
  WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  beginStation(ssid, password);
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
    beginStation(ssid, password);
    return waitConnected(cfg::kWifiConnectTimeoutMs);
  }
  beginStation(ssid, password);
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

void markConnected() {
  stationUp = true;
  led::setConnected(true);
  LOGI("connected, ip %s", WiFi.localIP().toString().c_str());
  stopAccessPoint();
  if (onConnected) onConnected();
}

void performChange() {
  String ssid = pendingSsid;
  String password = pendingPassword;
  pendingSsid = "";
  pendingPassword = "";
  changePending = false;

  String previousSsid = credentials::wifiSsid();
  String previousPassword = credentials::wifiPassword();

  WiFi.scanDelete();
  stationUp = false;
  led::setConnected(false);
  startAccessPoint();
  WiFi.disconnect();
  LOGI("switching WiFi to %s", ssid.c_str());

  if (connectWith(ssid, password)) {
    credentials::setWifi(ssid, password);
    changeState = ChangeState::Succeeded;
    changeMessage = "";
    markConnected();
    return;
  }

  bool restored = previousSsid.length() > 0 && connectWith(previousSsid, previousPassword);
  changeState = ChangeState::Failed;
  if (restored) {
    changeMessage = "Could not connect to " + ssid + "; the previous network was restored";
    markConnected();
  } else if (previousSsid.length() > 0) {
    changeMessage = "Could not connect to " + ssid + "; the previous network is unavailable and will be retried";
  } else {
    changeMessage = "Could not connect to " + ssid;
  }
}

}  // namespace

void begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  led::setConnected(false);
  if (!credentials::wifiConfigured()) {
    LOGW("no WiFi credentials provisioned");
    startAccessPoint();
    return;
  }
  if (connectWith(credentials::wifiSsid(), credentials::wifiPassword())) {
    markConnected();
  } else {
    startAccessPoint();
  }
}

void loop() {
  if (changePending) {
    performChange();
    return;
  }
  if (!credentials::wifiConfigured()) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!stationUp) markConnected();
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
    beginSaved();
  }
}

bool connected() { return stationUp && WiFi.status() == WL_CONNECTED; }

bool apActive() { return accessPointUp; }

String ip() { return connected() ? WiFi.localIP().toString() : String(); }

int rssi() { return connected() ? WiFi.RSSI() : 0; }

void setOnConnected(void (*callback)()) { onConnected = callback; }

void setPump(void (*pump)()) { pumpFn = pump; }

bool requestConnect(const String& ssid, const String& password) {
  if (changePending || changeState == ChangeState::Trying) return false;
  pendingSsid = ssid;
  pendingPassword = password;
  changePending = true;
  changeState = ChangeState::Trying;
  changeMessage = "";
  return true;
}

ChangeStatus changeStatus() {
  ChangeStatus status;
  status.state = changeState;
  status.message = changeMessage;
  return status;
}

bool startScan() {
  if (changePending || changeState == ChangeState::Trying) return false;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return true;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  return true;
}

ScanState scanState() {
  int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) return ScanState::Running;
  if (found >= 0) return ScanState::Done;
  return ScanState::Idle;
}

size_t scanResults(ScanResult* out, size_t max) {
  int16_t found = WiFi.scanComplete();
  if (found <= 0 || max == 0) return 0;
  size_t count = 0;
  for (int16_t i = 0; i < found && i < 255; i++) {
    String ssid = WiFi.SSID((uint8_t)i);
    if (!util::validSsid(ssid)) continue;
    ScanResult entry;
    entry.ssid = ssid;
    entry.rssi = (int)WiFi.RSSI((uint8_t)i);
    entry.secure = WiFi.encryptionType((uint8_t)i) != WIFI_AUTH_OPEN;
    entry.channel = (int)WiFi.channel((uint8_t)i);

    size_t same = count;
    for (size_t j = 0; j < count; j++)
      if (out[j].ssid == ssid) same = j;
    if (same < count) {
      if (entry.rssi > out[same].rssi) out[same] = entry;
      continue;
    }
    if (count < max) {
      out[count++] = entry;
      continue;
    }
    size_t weakest = 0;
    for (size_t j = 1; j < count; j++)
      if (out[j].rssi < out[weakest].rssi) weakest = j;
    if (entry.rssi > out[weakest].rssi) out[weakest] = entry;
  }
  for (size_t a = 1; a < count; a++) {
    ScanResult key = out[a];
    size_t b = a;
    while (b > 0 && out[b - 1].rssi < key.rssi) {
      out[b] = out[b - 1];
      b--;
    }
    out[b] = key;
  }
  return count;
}

}  // namespace network

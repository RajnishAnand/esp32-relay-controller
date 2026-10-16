#include <WebServer.h>
#include <Update.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include <algorithm>

#include "../../../src/platform.h"

unsigned long g_millis = 1000;
std::vector<PinWrite> g_pinWrites;
int g_configTimeCalls = 0;
int g_restarts = 0;
SerialStub Serial;
EspStub ESP;
time_t g_epoch = 0;
int g_feeds = 0;

void delay(unsigned long ms) { g_millis += ms; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t level) {
  PinWrite w = {g_millis, pin, level};
  g_pinWrites.push_back(w);
}
bool ledcSetup(uint8_t, uint32_t, uint8_t) { return true; }
void ledcAttachPin(uint8_t, uint8_t) {}
void ledcWrite(uint8_t, uint32_t) {}
void configTime(long, int, const char*, const char*, const char*) { g_configTimeCalls++; }
void EspStub::restart() { g_restarts++; }

WiFiClass WiFi;
wl_status_t g_wifiStatus = WL_CONNECTED;
IPAddress g_wifiIp(192, 168, 1, 50);
bool WiFiClass::mode(WiFiMode_t) { return true; }
void WiFiClass::begin(const char*, const char*) {}
bool WiFiClass::config(IPAddress ip, IPAddress, IPAddress, IPAddress) {
  g_wifiIp = ip;
  return true;
}
bool WiFiClass::disconnect(bool) { return true; }
wl_status_t WiFiClass::status() { return g_wifiStatus; }
IPAddress WiFiClass::localIP() { return g_wifiIp; }
IPAddress WiFiClass::gatewayIP() { return IPAddress(192, 168, 1, 1); }
IPAddress WiFiClass::subnetMask() { return IPAddress(255, 255, 255, 0); }
IPAddress WiFiClass::dnsIP() { return IPAddress(192, 168, 1, 1); }
IPAddress WiFiClass::softAPIP() { return IPAddress(192, 168, 4, 1); }
bool WiFiClass::softAP(const char*, const char*) { return true; }
bool WiFiClass::softAPdisconnect(bool) { return true; }
void WiFiClass::persistent(bool) {}
int WiFiClass::RSSI() { return -55; }
void WiFiClientSecure::setInsecure() {}
void WiFiClientSecure::setCACert(const char*) {}

std::map<std::string, std::string> g_nvs;
bool Preferences::begin(const char*, bool) { return true; }
bool Preferences::getBool(const char* k, bool d) { return g_nvs.count(k) ? g_nvs[k] == "1" : d; }
size_t Preferences::putBool(const char* k, bool v) {
  g_nvs[k] = v ? "1" : "0";
  return 1;
}
uint8_t Preferences::getUChar(const char* k, uint8_t d) { return g_nvs.count(k) ? (uint8_t)atoi(g_nvs[k].c_str()) : d; }
size_t Preferences::putUChar(const char* k, uint8_t v) {
  g_nvs[k] = std::to_string(v);
  return 1;
}
uint32_t Preferences::getULong(const char* k, uint32_t d) { return g_nvs.count(k) ? (uint32_t)atoll(g_nvs[k].c_str()) : d; }
size_t Preferences::putULong(const char* k, uint32_t v) {
  g_nvs[k] = std::to_string(v);
  return 4;
}
String Preferences::getString(const char* k, String d) { return g_nvs.count(k) ? String(g_nvs[k].c_str()) : d; }
size_t Preferences::putString(const char* k, String v) {
  g_nvs[k] = v.s;
  return v.length();
}

std::map<std::string, std::string> g_files;
static std::string curData;
static std::string curName;
static size_t curPos = 0;
int WiFiClient::available() {
  size_t r = curData.size() - curPos;
  return (int)(r > 700 ? 700 : r);
}
size_t WiFiClient::readBytes(uint8_t* b, size_t n) {
  size_t k = std::min(n, curData.size() - curPos);
  memcpy(b, curData.data() + curPos, k);
  curPos += k;
  return k;
}
bool WiFiClient::connected() { return curPos < curData.size(); }
bool HTTPClient::begin(WiFiClient&, String url) {
  curName = url.s.substr(url.s.rfind('/') + 1);
  return true;
}
void HTTPClient::setFollowRedirects(followRedirects_t) {}
void HTTPClient::setTimeout(uint16_t) {}
int HTTPClient::GET() {
  if (!g_files.count(curName)) return 404;
  curData = g_files[curName];
  curPos = 0;
  return 200;
}
int HTTPClient::getSize() { return (int)curData.size(); }
WiFiClient* HTTPClient::getStreamPtr() {
  static WiFiClient c;
  return &c;
}
void HTTPClient::end() {}
bool HTTPClient::connected() { return curPos < curData.size(); }

UpdateClass Update;
std::string g_updateWritten;
bool g_updateEnded = false;
bool g_updateAborted = false;
bool UpdateClass::begin(size_t, int) {
  g_updateWritten.clear();
  g_updateEnded = g_updateAborted = false;
  return true;
}
size_t UpdateClass::write(uint8_t* d, size_t n) {
  g_updateWritten.append((const char*)d, n);
  return n;
}
bool UpdateClass::end(bool) {
  g_updateEnded = true;
  return true;
}
void UpdateClass::abort() { g_updateAborted = true; }

bool g_bootPartitionSet = false;
static esp_partition_t partition;
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &partition; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
  g_bootPartitionSet = true;
  return ESP_OK;
}
esp_reset_reason_t esp_reset_reason(void) { return ESP_RST_POWERON; }
esp_err_t esp_task_wdt_init(uint32_t, bool) { return ESP_OK; }
esp_err_t esp_task_wdt_add(TaskHandle_t) { return ESP_OK; }
esp_err_t esp_task_wdt_reset(void) { return ESP_OK; }

WebServer* WebServer::instance = nullptr;
void WebServer::on(const char* uri, THandlerFunction fn) { on(uri, HTTP_ANY, fn); }
void WebServer::on(const char* uri, HTTPMethod method, THandlerFunction fn) {
  Route r = {uri, method, fn};
  routes.push_back(r);
}
void WebServer::onNotFound(THandlerFunction fn) { notFound = fn; }
void WebServer::collectHeaders(const char*[], const size_t) {}
void WebServer::begin() {}
void WebServer::handleClient() {}
bool WebServer::hasArg(const String& name) { return args.count(name.s) > 0; }
String WebServer::arg(const String& name) { return args.count(name.s) ? String(args[name.s].c_str()) : String(); }
String WebServer::header(const String& name) { return headers.count(name.s) ? String(headers[name.s].c_str()) : String(); }
void WebServer::sendHeader(const String& name, const String& value, bool) { response.headers[name.s] = value.s; }
void WebServer::send(int code, const char* type, const String& body) {
  response.code = code;
  response.type = type;
  response.body = body.s;
}
void WebServer::send_P(int code, const char* type, const char* body) {
  response.code = code;
  response.type = type;
  response.body = body;
}
HttpResponse WebServer::perform(HTTPMethod method, const std::string& uri) {
  response = HttpResponse();
  response.code = 0;
  for (size_t i = 0; i < routes.size(); i++) {
    if (routes[i].uri == uri && (routes[i].method == HTTP_ANY || routes[i].method == method)) {
      routes[i].fn();
      return response;
    }
  }
  if (notFound) notFound();
  return response;
}

namespace platform {
void beginWatchdog() {}
void feed() { g_feeds++; }
void restart() { g_restarts++; }
time_t epochNow() { return g_epoch; }
uint32_t freeHeap() { return 100000; }
const char* resetReason() { return "power_on"; }
bool elapsed(unsigned long since, unsigned long durationMs) { return millis() - since >= durationMs; }
}  // namespace platform

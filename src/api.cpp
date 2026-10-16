#include "api.h"

#include <WebServer.h>

#include "config.h"
#include "credentials.h"
#include "dashboard.h"
#include "network.h"
#include "ota.h"
#include "platform.h"
#include "relays.h"
#include "scheduler.h"
#include "util.h"
#include "version.h"

namespace api {
namespace {

WebServer server(80);
const char* kHeaderKeys[] = {"X-API-Key"};

void sendText(int code, const String& text) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "text/plain", text);
}

void sendJson(const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

bool authorized() {
  if (!credentials::apiTokenValid()) {
    sendText(503, "device not provisioned");
    return false;
  }
  if (!util::constantTimeEquals(server.header("X-API-Key"), credentials::apiToken())) {
    sendText(401, "unauthorized");
    return false;
  }
  return true;
}

bool relayArg(uint8_t& index) {
  if (!server.hasArg("relay")) {
    sendText(400, "missing relay");
    return false;
  }
  if (!util::parseRelayIndex(server.arg("relay"), index)) {
    sendText(400, "relay must be 0-" + String((int)cfg::kRelayCount - 1));
    return false;
  }
  return true;
}

bool boolArg(const char* name, bool& value) {
  if (!server.hasArg(name)) {
    sendText(400, String("missing ") + name);
    return false;
  }
  if (!util::parseBool(server.arg(name), value)) {
    sendText(400, String(name) + " must be 1, 0, true, false, on or off");
    return false;
  }
  return true;
}

String boolText(bool value) { return value ? "true" : "false"; }

String formatTime(uint8_t hour, uint8_t minute) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)hour, (unsigned)minute);
  return String(buf);
}

void handleStatus() {
  String json = "{\"relays\":[";
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) {
    const Schedule& s = scheduler::get(i);
    if (i) json += ",";
    json += "{\"name\":\"" + util::jsonEscape(relays::name(i)) + "\"";
    json += ",\"state\":" + boolText(relays::get(i));
    json += ",\"schedEnabled\":" + boolText(s.enabled);
    json += ",\"onTime\":\"" + formatTime(s.onHour, s.onMinute) + "\"";
    json += ",\"offTime\":\"" + formatTime(s.offHour, s.offMinute) + "\"}";
  }
  json += "]}";
  sendJson(json);
}

void handleSet() {
  uint8_t index;
  bool on;
  if (!relayArg(index) || !boolArg("state", on)) return;
  scheduler::noteManualChange(index);
  relays::set(index, on);
  sendText(200, "ok");
}

void handleAll() {
  bool on;
  if (!boolArg("state", on)) return;
  for (uint8_t i = 0; i < cfg::kRelayCount; i++) scheduler::noteManualChange(i);
  relays::setAll(on);
  sendText(200, "ok");
}

void handleRename() {
  uint8_t index;
  if (!relayArg(index)) return;
  if (!server.hasArg("name")) return sendText(400, "missing name");
  String name = server.arg("name");
  name.trim();
  if (!util::validName(name)) {
    return sendText(400, "name must be 1-" + String((int)cfg::kNameMaxLen) +
                             " bytes of valid UTF-8 without control characters");
  }
  relays::rename(index, name);
  sendText(200, "ok");
}

void handleSchedule() {
  uint8_t index;
  bool enabled;
  if (!relayArg(index) || !boolArg("enabled", enabled)) return;
  if (!server.hasArg("on") || !server.hasArg("off")) return sendText(400, "missing on or off");
  Schedule s;
  s.enabled = enabled;
  if (!util::parseHHMM(server.arg("on"), s.onHour, s.onMinute) ||
      !util::parseHHMM(server.arg("off"), s.offHour, s.offMinute)) {
    return sendText(400, "on and off must be HH:MM, 00:00-23:59");
  }
  scheduler::set(index, s);
  sendText(200, "ok");
}

void handleInfo() {
  String json = "{\"firmware\":\"" + String(FIRMWARE_VERSION) + "\"";
  json += ",\"uptimeSeconds\":" + String((unsigned long)(millis() / 1000UL));
  json += ",\"freeHeap\":" + String((unsigned long)platform::freeHeap());
  json += ",\"resetReason\":\"" + String(platform::resetReason()) + "\"";
  json += ",\"relayCount\":" + String((int)cfg::kRelayCount);
  json += ",\"wifi\":{\"connected\":" + boolText(network::connected());
  json += ",\"apActive\":" + boolText(network::apActive());
  json += ",\"ip\":\"" + network::ip() + "\"";
  json += ",\"rssi\":" + String(network::rssi()) + "}";
  struct tm t;
  json += ",\"time\":{\"synced\":" + boolText(scheduler::synced());
  if (scheduler::localTime(t)) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
             t.tm_min);
    json += ",\"local\":\"" + String(buf) + "\"";
  }
  json += "}}";
  sendJson(json);
}

void handleUpdateStatus() {
  ota::Status s = ota::status();
  String json = "{\"current\":\"" + String(FIRMWARE_VERSION) + "\"";
  json += ",\"configured\":" + boolText(s.configured);
  json += ",\"state\":\"" + String(s.stateName) + "\"";
  json += ",\"latest\":\"" + util::jsonEscape(s.latest) + "\"";
  json += ",\"progress\":" + String((int)s.progress);
  json += ",\"message\":\"" + util::jsonEscape(s.message) + "\"";
  json += ",\"pendingConfirm\":" + boolText(s.pendingConfirm);
  json += ",\"lastCheckSecondsAgo\":" + String(s.lastCheckSecondsAgo) + "}";
  sendJson(json);
}

void handleUpdateCheck() {
  if (!ota::requestCheck()) return sendText(409, "busy");
  sendText(200, "ok");
}

void handleUpdateApply() {
  if (!ota::requestApply()) return sendText(409, "no update available");
  sendText(200, "ok");
}

void handleDashboard() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Referrer-Policy", "no-referrer");
  server.sendHeader("Content-Security-Policy",
                    "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                    "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
  server.send_P(200, "text/html", dashboard::kHtml);
}

void route(const char* path, HTTPMethod method, void (*handler)()) {
  server.on(path, method, [handler]() {
    if (authorized()) handler();
  });
}

}  // namespace

void begin() {
  server.collectHeaders(kHeaderKeys, 1);
  server.on("/", HTTP_GET, handleDashboard);
  route("/api/status", HTTP_GET, handleStatus);
  route("/api/info", HTTP_GET, handleInfo);
  route("/api/set", HTTP_ANY, handleSet);
  route("/api/all", HTTP_ANY, handleAll);
  route("/api/rename", HTTP_ANY, handleRename);
  route("/api/schedule", HTTP_ANY, handleSchedule);
  route("/api/update/status", HTTP_GET, handleUpdateStatus);
  route("/api/update/check", HTTP_POST, handleUpdateCheck);
  route("/api/update/apply", HTTP_POST, handleUpdateApply);
  server.onNotFound([]() { sendText(404, "not found"); });
  server.begin();
}

void loop() { server.handleClient(); }

}  // namespace api

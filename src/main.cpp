/*
 * ESP32-WROOM 4-Channel Relay Controller (PlatformIO / C++)
 * -----------------------------------------------------------
 * - 5V relay module, active-LOW inputs (standard for this class of board —
 *   verify with a meter on IN1 if relays behave inverted; flip RELAY_ACTIVE_LOW).
 * - Pin mapping per your wiring (no strapping pins used — safe set):
 *     IN1 -> GPIO27   IN2 -> GPIO26   IN3 -> GPIO14   IN4 -> GPIO25
 * - Boot sequence: 1s settle delay, then state restored from NVS with a
 *   stagger between relays turning ON (not simultaneous) to limit inrush.
 * - WiFi:
 *     - Initial connect: DHCP first to learn gateway/subnet, then reconnects
 *       forcing the host address to STATIC_HOST_OCTET (.250 by default).
 *       Assumes a /24 (255.255.255.0) subnet — see setupWiFi() for the
 *       fallback behavior if that assumption is wrong for your network.
 *     - If WiFi drops after connecting, the device keeps retrying in the
 *       background (non-blocking) every WIFI_RETRY_INTERVAL_MS.
 *     - If WiFi is unavailable at all (boot or runtime), the device starts
 *       its own fallback access point (AP_SSID/AP_PASSWORD below) so the
 *       dashboard stays reachable at 192.168.4.1 while it keeps trying to
 *       rejoin your real network in the background.
 *     - CAVEAT: if WiFi was unavailable at boot, the one-time DHCP→static-IP
 *       derivation is skipped for that session (no blocking re-run of it
 *       happens in the background, to avoid freezing the AP's web server
 *       during retries). The device will still reconnect via DHCP once WiFi
 *       returns, just not necessarily at exactly .250. Power-cycle it once
 *       WiFi is back if you specifically need the static address restored.
 * - Status LED (assumed GPIO2 — verify for your specific board):
 *     solid ON        = not connected to your WiFi (includes AP fallback mode)
 *     dim short pulse  = connected to your WiFi
 * - Scheduling: each relay can have an optional daily ON time and OFF time.
 *   Requires NTP time sync, which requires internet access via your real
 *   WiFi network at least once — it will NOT work while only running in
 *   AP fallback mode with no upstream internet. Timezone defaults to IST
 *   (UTC+5:30) — change GMT_OFFSET_SEC if that's wrong for you.
 * - Dashboard is served from flash (no SPIFFS/LittleFS needed), all control
 *   is fetch()-based, no page reloads. Settings panel (gear icon) allows
 *   renaming switches and configuring their schedules.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "time.h"

// ---------------- USER CONFIG ----------------

const char* WIFI_SSID     = "Zen";
const char* WIFI_PASSWORD = "genos951";

// Fallback access point — used only when your real WiFi is unreachable.
// WPA2 requires 8+ chars for AP_PASSWORD. CHANGE THIS before deploying
// anywhere not fully trusted; anyone in range can join this AP otherwise.
const char* AP_SSID     = "RELAY Control 1";
const char* AP_PASSWORD = "relay1234";

// Desired last octet of the static IP, e.g. 250 -> x.x.x.250.
// Gateway/subnet/DNS are auto-detected from DHCP at boot — see setupWiFi().
const uint8_t STATIC_HOST_OCTET = 250;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000; // initial boot attempt
const unsigned long WIFI_RETRY_INTERVAL_MS  = 15000; // background reconnect interval

const unsigned long INITIAL_BOOT_DELAY_MS = 1000; // let power/relay board settle before driving pins

#define RELAY_ACTIVE_LOW true   // standard for 5V opto-isolated relay boards; flip if inverted

// Order: index 0=IN1, 1=IN2, 2=IN3, 3=IN4
const uint8_t RELAY_PINS[4] = {27, 26, 14, 25};

#define STATUS_LED_PIN 2
#define LED_CHANNEL 0
#define LED_FREQ 5000
#define LED_RESOLUTION 8   // bits

const unsigned long STARTUP_STAGGER_MS = 400; // delay between relays turning ON at boot

// Timezone for scheduling. Default: India Standard Time (UTC+5:30).
const long GMT_OFFSET_SEC = 5 * 3600 + 1800;
const int  DST_OFFSET_SEC = 0;
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const time_t MIN_VALID_EPOCH = 1700000000; // ~Nov 2023 — below this, NTP hasn't synced yet

// ------------------------------------------------

Preferences prefs;
WebServer server(80);
bool relayState[4] = {false, false, false, false};
String relayNames[4];

struct Schedule {
  bool enabled;
  uint8_t onHour, onMinute;
  uint8_t offHour, offMinute;
};
Schedule schedules[4];

bool staConnected = false;
bool apActive = false;
unsigned long lastReconnectAttempt = 0;
bool timeSynced = false;
int lastScheduleCheckMinute = -1;

// ---------------- Relay control ----------------

void applyPinFromState(uint8_t idx) {
  bool physicalLevel = RELAY_ACTIVE_LOW ? !relayState[idx] : relayState[idx];
  digitalWrite(RELAY_PINS[idx], physicalLevel ? HIGH : LOW);
}

void setRelay(uint8_t idx, bool on, bool persist = true) {
  if (idx > 3) return;
  relayState[idx] = on;
  applyPinFromState(idx);
  if (persist) {
    char key[8];
    snprintf(key, sizeof(key), "r%d", idx);
    prefs.putBool(key, on);
  }
}

void loadStateFromFlash() {
  for (uint8_t i = 0; i < 4; i++) {
    char key[8];
    snprintf(key, sizeof(key), "r%d", i);
    relayState[i] = prefs.getBool(key, false); // default OFF if never saved

    char nkey[8];
    snprintf(nkey, sizeof(nkey), "n%d", i);
    char defaultName[12];
    snprintf(defaultName, sizeof(defaultName), "Relay %d", i + 1);
    relayNames[i] = prefs.getString(nkey, defaultName);

    char ekey[8], onhk[8], onmk[8], offhk[8], offmk[8];
    snprintf(ekey, sizeof(ekey), "se%d", i);
    snprintf(onhk, sizeof(onhk), "soh%d", i);
    snprintf(onmk, sizeof(onmk), "som%d", i);
    snprintf(offhk, sizeof(offhk), "sfh%d", i);
    snprintf(offmk, sizeof(offmk), "sfm%d", i);
    schedules[i].enabled   = prefs.getBool(ekey, false);
    schedules[i].onHour    = prefs.getUChar(onhk, 7);
    schedules[i].onMinute  = prefs.getUChar(onmk, 0);
    schedules[i].offHour   = prefs.getUChar(offhk, 22);
    schedules[i].offMinute = prefs.getUChar(offmk, 0);
  }
}

void saveSchedule(uint8_t idx) {
  char ekey[8], onhk[8], onmk[8], offhk[8], offmk[8];
  snprintf(ekey, sizeof(ekey), "se%d", idx);
  snprintf(onhk, sizeof(onhk), "soh%d", idx);
  snprintf(onmk, sizeof(onmk), "som%d", idx);
  snprintf(offhk, sizeof(offhk), "sfh%d", idx);
  snprintf(offmk, sizeof(offmk), "sfm%d", idx);
  prefs.putBool(ekey, schedules[idx].enabled);
  prefs.putUChar(onhk, schedules[idx].onHour);
  prefs.putUChar(onmk, schedules[idx].onMinute);
  prefs.putUChar(offhk, schedules[idx].offHour);
  prefs.putUChar(offmk, schedules[idx].offMinute);
}

// Restore relay pins on boot, staggering ON transitions to avoid all four
// relays (and whatever they switch) energizing simultaneously.
void restoreRelaysStaggered() {
  bool firstOnApplied = false;
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    if (relayState[i]) {
      if (firstOnApplied) {
        delay(STARTUP_STAGGER_MS);
      }
      applyPinFromState(i);
      firstOnApplied = true;
    } else {
      applyPinFromState(i); // OFF transitions applied immediately, no inrush concern
    }
  }
}

// ---------------- Status LED ----------------

enum LedState { LED_CONNECTING, LED_CONNECTED };
LedState currentLedState = LED_CONNECTING;

void updateStatusLed() {
  if (currentLedState == LED_CONNECTING) {
    ledcWrite(LED_CHANNEL, 255); // solid full brightness
  } else {
    // Dim short pulse roughly every 1.6s to indicate "connected, idle"
    unsigned long phase = millis() % 1600;
    ledcWrite(LED_CHANNEL, phase < 150 ? 25 : 0);
  }
}

// ---------------- NTP / scheduling ----------------

void configureNTP() {
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  timeSynced = false; // confirmed true once checkSchedules() sees a plausible timestamp
}

// Fires at most once per calendar minute, only once NTP has produced a
// plausible timestamp. Silently does nothing until both conditions hold.
void checkSchedules() {
  time_t now;
  time(&now);

  if (!timeSynced) {
    if (now < MIN_VALID_EPOCH) return; // NTP hasn't synced yet
    timeSynced = true;
    Serial.println("NTP time synced — schedules are now active.");
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  int minuteOfDay = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  if (minuteOfDay == lastScheduleCheckMinute) return; // already handled this minute
  lastScheduleCheckMinute = minuteOfDay;

  for (uint8_t i = 0; i < 4; i++) {
    if (!schedules[i].enabled) continue;
    int onM  = schedules[i].onHour * 60 + schedules[i].onMinute;
    int offM = schedules[i].offHour * 60 + schedules[i].offMinute;
    if (minuteOfDay == onM) {
      setRelay(i, true);
      Serial.printf("Schedule: relay %d ON (%02d:%02d)\n", i, schedules[i].onHour, schedules[i].onMinute);
    } else if (minuteOfDay == offM) {
      setRelay(i, false);
      Serial.printf("Schedule: relay %d OFF (%02d:%02d)\n", i, schedules[i].offHour, schedules[i].offMinute);
    }
  }
}

// ---------------- WiFi: initial connect (auto-detected subnet, forced host octet) ----------------

bool waitForConnection(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(300);
    Serial.print(".");
    updateStatusLed();
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// One-shot, blocking — only called at boot. Background reconnects use a
// lighter, non-blocking path in handleWifiMonitor() instead.
bool setupWiFi() {
  currentLedState = LED_CONNECTING;
  WiFi.mode(WIFI_STA);

  Serial.print("Connecting via DHCP to learn network config");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (!waitForConnection(WIFI_CONNECT_TIMEOUT_MS)) {
    Serial.println("DHCP connect failed within timeout.");
    return false;
  }

  IPAddress dhcpIP  = WiFi.localIP();
  IPAddress gateway = WiFi.gatewayIP();
  IPAddress subnet  = WiFi.subnetMask();
  IPAddress dns     = WiFi.dnsIP();

  Serial.printf("DHCP lease: %s  gateway: %s  subnet: %s\n",
                dhcpIP.toString().c_str(), gateway.toString().c_str(), subnet.toString().c_str());

  if (subnet != IPAddress(255, 255, 255, 0)) {
    Serial.println("WARNING: subnet mask is not 255.255.255.0 — skipping static IP "
                    "derivation (it would be wrong). Staying on the DHCP-assigned address.");
    return true;
  }

  IPAddress targetIP = dhcpIP;
  targetIP[3] = STATIC_HOST_OCTET;

  if (targetIP == dhcpIP) {
    Serial.println("DHCP already assigned the target host octet — keeping current lease.");
    return true;
  }

  Serial.printf("Reconnecting with static IP: %s\n", targetIP.toString().c_str());

  WiFi.disconnect(true);
  delay(200);

  if (!WiFi.config(targetIP, gateway, subnet, dns)) {
    Serial.println("WiFi.config() rejected the derived static IP — reconnecting via DHCP instead.");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    return waitForConnection(WIFI_CONNECT_TIMEOUT_MS);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Reconnecting with static IP");
  if (waitForConnection(WIFI_CONNECT_TIMEOUT_MS)) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("Static-IP reconnect failed — possible IP conflict with another "
                  "device already using this address, or the router rejected it.");
  return false;
}

// ---------------- WiFi: fallback AP + background auto-reconnect ----------------

void startAPFallback() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA); // AP for local access, STA kept alive for background retries
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  apActive = true;
  Serial.print("AP fallback active. Connect to \"");
  Serial.print(AP_SSID);
  Serial.print("\" and open http://");
  Serial.println(WiFi.softAPIP());
}

void stopAPFallback() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("Real WiFi reconnected — AP fallback disabled.");
}

// Non-blocking: call every loop(). Detects drops, starts AP fallback
// immediately so the dashboard stays reachable, and retries the real
// network in the background without freezing the web server.
void handleWifiMonitor() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!staConnected) {
      staConnected = true;
      currentLedState = LED_CONNECTED;
      Serial.print("WiFi connected. IP: ");
      Serial.println(WiFi.localIP());
      configureNTP();
      if (apActive) stopAPFallback();
    }
    return;
  }

  if (staConnected) {
    staConnected = false;
    currentLedState = LED_CONNECTING;
    Serial.println("WiFi dropped — starting AP fallback, retrying in background.");
  }

  if (!apActive) {
    startAPFallback();
  }

  if (millis() - lastReconnectAttempt > WIFI_RETRY_INTERVAL_MS) {
    lastReconnectAttempt = millis();
    Serial.println("Background: attempting WiFi reconnect...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// ---------------- Dashboard HTML ----------------

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Relay Control</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; background:#111; color:#eee; margin:0; padding:24px; }
  .header { display:flex; justify-content:space-between; align-items:center; margin-bottom:20px; }
  h1 { font-size:1.3rem; margin:0; }
  .gear { background:none; border:none; color:#eee; font-size:1.4rem; cursor:pointer; padding:4px 8px; }
  .card { background:#1c1c1c; border-radius:10px; padding:16px 20px; margin-bottom:12px;
          display:flex; justify-content:space-between; align-items:center; }
  .name { font-size:1.05rem; }
  .status { font-size:0.85rem; opacity:0.6; margin-left:8px; }
  .sched { font-size:0.75rem; opacity:0.45; display:block; margin-top:2px; }
  .switch { position:relative; width:56px; height:30px; flex-shrink:0; }
  .switch input { opacity:0; width:0; height:0; }
  .slider { position:absolute; cursor:pointer; inset:0; background:#444; border-radius:30px; transition:0.2s; }
  .slider:before { content:""; position:absolute; height:22px; width:22px; left:4px; bottom:4px;
                    background:#eee; border-radius:50%; transition:0.2s; }
  input:checked + .slider { background:#2e8b57; }
  input:checked + .slider:before { transform:translateX(26px); }
  .allrow { display:flex; gap:10px; margin-top:20px; }
  button.action { flex:1; padding:10px; border:none; border-radius:8px; background:#333; color:#eee;
           font-size:0.95rem; cursor:pointer; }
  button.action:active { background:#444; }
  #conn { font-size:0.8rem; opacity:0.5; margin-top:16px; }

  .overlay { display:none; position:fixed; inset:0; background:rgba(0,0,0,0.6);
             align-items:center; justify-content:center; overflow-y:auto; padding:20px; }
  .overlay.open { display:flex; }
  .panel { background:#1c1c1c; border-radius:12px; padding:20px; width:min(90vw, 380px); }
  .panel h2 { margin-top:0; font-size:1.1rem; }
  .relayBlock { border-top:1px solid #333; padding-top:12px; margin-top:12px; }
  .relayBlock:first-of-type { border-top:none; margin-top:0; }
  .panel label { display:block; font-size:0.85rem; opacity:0.7; margin:8px 0 4px; }
  .panel input[type=text] { width:100%; padding:8px; border-radius:6px; border:1px solid #444;
                             background:#111; color:#eee; font-size:0.95rem; }
  .schedRow { display:flex; gap:8px; align-items:center; margin-top:6px; }
  .schedRow input[type=time] { flex:1; padding:6px; border-radius:6px; border:1px solid #444;
                                background:#111; color:#eee; font-size:0.9rem; }
  .panel .rowbtns { display:flex; gap:10px; margin-top:18px; }
</style>
</head>
<body>
<div class="header">
  <h1>Relay Dashboard</h1>
  <button class="gear" onclick="openSettings()" title="Configure">&#9881;</button>
</div>

<div id="relays"></div>
<div class="allrow">
  <button class="action" onclick="setAll(true)">All ON</button>
  <button class="action" onclick="setAll(false)">All OFF</button>
</div>
<div id="conn">connecting...</div>

<div class="overlay" id="settingsOverlay">
  <div class="panel">
    <h2>Configure Switches</h2>
    <div id="configFields"></div>
    <div class="rowbtns">
      <button class="action" onclick="saveSettings()">Save</button>
      <button class="action" onclick="closeSettings()">Cancel</button>
    </div>
  </div>
</div>

<script>
let lastData = null;

async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();
    lastData = data;
    render(data);
    document.getElementById('conn').textContent = 'connected';
  } catch (e) {
    document.getElementById('conn').textContent = 'disconnected — retrying...';
  }
}

function render(data) {
  const container = document.getElementById('relays');
  container.innerHTML = '';
  data.relays.forEach((r, i) => {
    const card = document.createElement('div');
    card.className = 'card';
    const schedText = r.schedEnabled ? `Schedule: ${r.onTime} – ${r.offTime}` : '';
    card.innerHTML = `
      <span class="name">${r.name}<span class="status">${r.state ? 'ON' : 'OFF'}</span>
        ${schedText ? `<span class="sched">${schedText}</span>` : ''}
      </span>
      <label class="switch">
        <input type="checkbox" ${r.state ? 'checked' : ''} onchange="toggle(${i}, this.checked)">
        <span class="slider"></span>
      </label>`;
    container.appendChild(card);
  });
}

async function toggle(idx, state) {
  await fetch(`/api/set?relay=${idx}&state=${state ? 1 : 0}`);
  fetchStatus();
}

async function setAll(state) {
  await fetch(`/api/all?state=${state ? 1 : 0}`);
  fetchStatus();
}

function openSettings() {
  if (!lastData) return;
  const fields = document.getElementById('configFields');
  fields.innerHTML = '';
  lastData.relays.forEach((r, i) => {
    const block = document.createElement('div');
    block.className = 'relayBlock';

    const nameLabel = document.createElement('label');
    nameLabel.textContent = `Switch ${i + 1} name`;
    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.value = r.name;
    nameInput.id = `nameInput${i}`;

    const schedLabel = document.createElement('label');
    schedLabel.textContent = 'Daily schedule';
    const schedRow = document.createElement('div');
    schedRow.className = 'schedRow';
    const enableCb = document.createElement('input');
    enableCb.type = 'checkbox';
    enableCb.id = `schedEnable${i}`;
    enableCb.checked = r.schedEnabled;
    const onInput = document.createElement('input');
    onInput.type = 'time';
    onInput.id = `schedOn${i}`;
    onInput.value = r.onTime || '07:00';
    const offInput = document.createElement('input');
    offInput.type = 'time';
    offInput.id = `schedOff${i}`;
    offInput.value = r.offTime || '22:00';
    schedRow.appendChild(enableCb);
    schedRow.appendChild(onInput);
    schedRow.appendChild(offInput);

    block.appendChild(nameLabel);
    block.appendChild(nameInput);
    block.appendChild(schedLabel);
    block.appendChild(schedRow);
    fields.appendChild(block);
  });
  document.getElementById('settingsOverlay').classList.add('open');
}

function closeSettings() {
  document.getElementById('settingsOverlay').classList.remove('open');
}

async function saveSettings() {
  for (let i = 0; i < 4; i++) {
    const nameVal = document.getElementById(`nameInput${i}`).value.trim();
    if (nameVal.length > 0) {
      await fetch(`/api/rename?relay=${i}&name=${encodeURIComponent(nameVal)}`);
    }
    const enabled = document.getElementById(`schedEnable${i}`).checked;
    const onVal = document.getElementById(`schedOn${i}`).value || '07:00';
    const offVal = document.getElementById(`schedOff${i}`).value || '22:00';
    await fetch(`/api/schedule?relay=${i}&enabled=${enabled ? 1 : 0}&on=${onVal}&off=${offVal}`);
  }
  closeSettings();
  fetchStatus();
}

fetchStatus();
setInterval(fetchStatus, 4000); // periodic resync in case another client changes state
</script>
</body>
</html>
)HTML";

// ---------------- HTTP handlers ----------------

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String json = "{\"relays\":[";
  for (uint8_t i = 0; i < 4; i++) {
    char onBuf[6], offBuf[6];
    snprintf(onBuf, sizeof(onBuf), "%02d:%02d", schedules[i].onHour, schedules[i].onMinute);
    snprintf(offBuf, sizeof(offBuf), "%02d:%02d", schedules[i].offHour, schedules[i].offMinute);
    json += "{\"name\":\"" + relayNames[i] + "\",\"state\":" + (relayState[i] ? "true" : "false");
    json += ",\"schedEnabled\":" + String(schedules[i].enabled ? "true" : "false");
    json += ",\"onTime\":\"" + String(onBuf) + "\",\"offTime\":\"" + String(offBuf) + "\"}";
    if (i < 3) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSet() {
  if (!server.hasArg("relay") || !server.hasArg("state")) {
    server.send(400, "text/plain", "missing relay or state param");
    return;
  }
  int idx = server.arg("relay").toInt();
  bool state = server.arg("state").toInt() == 1;
  if (idx < 0 || idx > 3) {
    server.send(400, "text/plain", "relay index out of range");
    return;
  }
  setRelay(idx, state);
  server.send(200, "text/plain", "ok");
}

void handleAll() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "missing state param");
    return;
  }
  bool state = server.arg("state").toInt() == 1;
  for (uint8_t i = 0; i < 4; i++) setRelay(i, state);
  server.send(200, "text/plain", "ok");
}

void handleRename() {
  if (!server.hasArg("relay") || !server.hasArg("name")) {
    server.send(400, "text/plain", "missing relay or name param");
    return;
  }
  int idx = server.arg("relay").toInt();
  if (idx < 0 || idx > 3) {
    server.send(400, "text/plain", "relay index out of range");
    return;
  }
  String name = server.arg("name");
  if (name.length() == 0 || name.length() > 24) {
    server.send(400, "text/plain", "name must be 1-24 chars");
    return;
  }
  relayNames[idx] = name;
  char nkey[8];
  snprintf(nkey, sizeof(nkey), "n%d", idx);
  prefs.putString(nkey, name);
  server.send(200, "text/plain", "ok");
}

bool parseHHMM(const String& s, uint8_t& h, uint8_t& m) {
  int hh, mm;
  if (sscanf(s.c_str(), "%d:%d", &hh, &mm) != 2) return false;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  h = (uint8_t)hh;
  m = (uint8_t)mm;
  return true;
}

void handleSchedule() {
  if (!server.hasArg("relay") || !server.hasArg("enabled") ||
      !server.hasArg("on") || !server.hasArg("off")) {
    server.send(400, "text/plain", "missing relay, enabled, on or off param");
    return;
  }
  int idx = server.arg("relay").toInt();
  if (idx < 0 || idx > 3) {
    server.send(400, "text/plain", "relay index out of range");
    return;
  }
  uint8_t onH, onM, offH, offM;
  if (!parseHHMM(server.arg("on"), onH, onM) || !parseHHMM(server.arg("off"), offH, offM)) {
    server.send(400, "text/plain", "on/off must be HH:MM, 00:00-23:59");
    return;
  }
  schedules[idx].enabled   = server.arg("enabled").toInt() == 1;
  schedules[idx].onHour    = onH;
  schedules[idx].onMinute  = onM;
  schedules[idx].offHour   = offH;
  schedules[idx].offMinute = offM;
  saveSchedule(idx);
  server.send(200, "text/plain", "ok");
}

void handleNotFound() {
  server.send(404, "text/plain", "not found");
}

// ---------------- Setup / loop ----------------

void setup() {
  Serial.begin(115200);
  delay(INITIAL_BOOT_DELAY_MS); // let power rails / relay board settle before driving any pins

  // Status LED
  ledcSetup(LED_CHANNEL, LED_FREQ, LED_RESOLUTION);
  ledcAttachPin(STATUS_LED_PIN, LED_CHANNEL);
  currentLedState = LED_CONNECTING;
  updateStatusLed();

  // Load persisted state BEFORE configuring pins, so we know what to drive them to.
  prefs.begin("relay", false); // namespace "relay", read-write
  loadStateFromFlash();

  // Configure pins and restore state with stagger to avoid simultaneous
  // switching / inrush on multiple loads at once.
  restoreRelaysStaggered();

  // WiFi: one-shot blocking attempt at boot (DHCP-discover, then pin to .250).
  // If it fails, start the fallback AP immediately so the dashboard is
  // reachable even on a first boot with no WiFi available.
  if (setupWiFi()) {
    staConnected = true;
    currentLedState = LED_CONNECTED;
    configureNTP();
  } else {
    startAPFallback();
  }

  // Routes
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/set", handleSet);
  server.on("/api/all", handleAll);
  server.on("/api/rename", handleRename);
  server.on("/api/schedule", handleSchedule);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();
  updateStatusLed();
  handleWifiMonitor();
  checkSchedules();
}
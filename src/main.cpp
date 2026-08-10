/*
 * ESP32-WROOM 4-Channel Relay Controller (PlatformIO / C++)
 * -----------------------------------------------------------
 * - 5V relay module, active-LOW inputs (standard for this class of board —
 *   verify with a meter on IN1 if relays behave inverted; flip RELAY_ACTIVE_LOW).
 * - Pin mapping per your wiring (no strapping pins used — safe set):
 *     IN1 -> GPIO27   IN2 -> GPIO26   IN3 -> GPIO14   IN4 -> GPIO25
 * - State persisted in NVS via Preferences, restored on boot with a stagger
 *   delay between relays being switched ON (not simultaneous) to limit
 *   inrush / voltage sag.
 * - Onboard status LED (assumed GPIO2 — verify for your specific board):
 *     solid ON       = WiFi connecting / disconnected
 *     dim short pulse = WiFi connected
 * - IP assignment: connects via DHCP first to learn the network's gateway
 *   and subnet mask, then reconnects with those same values but forces the
 *   host address to end in STATIC_HOST_OCTET (.250 by default). This assumes
 *   a standard /24 (255.255.255.0) subnet — true for nearly all home/office
 *   routers. If yours uses something else, check the serial log after first
 *   boot; the derived address will be wrong and you'll need to hardcode it
 *   instead (see setupWiFi() below).
 *   CAVEAT: this does not check whether .250 is already taken by another
 *   device on your network. If you get IP conflicts, pick a different
 *   STATIC_HOST_OCTET or reserve .250 in your router's DHCP settings.
 * - Dashboard is served from flash (no SPIFFS/LittleFS needed), all control
 *   is fetch()-based, no page reloads.
 * - Relay names are renameable from the dashboard settings panel and persist
 *   in NVS.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ---------------- USER CONFIG ----------------

const char* WIFI_SSID     = "Zen";
const char* WIFI_PASSWORD = "genos951";

// Desired last octet of the static IP, e.g. 250 -> x.x.x.250.
// Gateway/subnet/DNS are auto-detected from DHCP at boot — see setupWiFi().
const uint8_t STATIC_HOST_OCTET = 250;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;

#define RELAY_ACTIVE_LOW true   // standard for 5V opto-isolated relay boards; flip if inverted

// Order: index 0=IN1, 1=IN2, 2=IN3, 3=IN4
const uint8_t RELAY_PINS[4] = {27, 26, 14, 25};

#define STATUS_LED_PIN 2
#define LED_CHANNEL 0
#define LED_FREQ 5000
#define LED_RESOLUTION 8   // bits

const unsigned long STARTUP_STAGGER_MS = 400; // delay between relays turning ON at boot

// ------------------------------------------------

Preferences prefs;
WebServer server(80);
bool relayState[4] = {false, false, false, false};
String relayNames[4];

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
  }
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

// ---------------- WiFi (auto-detected subnet, forced host octet) ----------------

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

void setupWiFi() {
  currentLedState = LED_CONNECTING;
  WiFi.mode(WIFI_STA);

  // Phase 1: connect via plain DHCP so the router tells us the real
  // gateway/subnet/DNS for this network. We don't know these in advance.
  Serial.print("Connecting via DHCP to learn network config");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (!waitForConnection(WIFI_CONNECT_TIMEOUT_MS)) {
    currentLedState = LED_CONNECTING; // stays solid — not connected
    Serial.println("DHCP connect failed within timeout. Relays still work locally "
                    "and keep their state; dashboard unreachable until WiFi connects. "
                    "No retry loop implemented here — add one if this must run "
                    "unattended on a flaky network.");
    return;
  }

  IPAddress dhcpIP  = WiFi.localIP();
  IPAddress gateway = WiFi.gatewayIP();
  IPAddress subnet  = WiFi.subnetMask();
  IPAddress dns     = WiFi.dnsIP();

  Serial.printf("DHCP lease: %s  gateway: %s  subnet: %s\n",
                dhcpIP.toString().c_str(), gateway.toString().c_str(), subnet.toString().c_str());

  // Only the last octet is forced; the network/gateway/subnet/DNS all come
  // from what DHCP just told us. This is only correct for a /24 subnet
  // (255.255.255.0) — check the subnet printed above if you're unsure.
  if (subnet != IPAddress(255, 255, 255, 0)) {
    Serial.println("WARNING: subnet mask is not 255.255.255.0 — the auto-derived "
                    "static IP below is probably wrong for your network. Falling "
                    "back to the DHCP-assigned address instead.");
    currentLedState = LED_CONNECTED;
    return;
  }

  IPAddress targetIP = dhcpIP;
  targetIP[3] = STATIC_HOST_OCTET;

  if (targetIP == dhcpIP) {
    Serial.println("DHCP already assigned the target host octet — keeping current lease.");
    currentLedState = LED_CONNECTED;
    return;
  }

  Serial.printf("Reconnecting with static IP: %s\n", targetIP.toString().c_str());

  // Phase 2: disconnect and reconnect with the derived static config applied.
  // WiFi.config() only takes effect on the next WiFi.begin(), not retroactively.
  WiFi.disconnect(true);
  delay(200);

  if (!WiFi.config(targetIP, gateway, subnet, dns)) {
    Serial.println("WiFi.config() rejected the derived static IP — reconnecting via DHCP instead.");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    currentLedState = waitForConnection(WIFI_CONNECT_TIMEOUT_MS) ? LED_CONNECTED : LED_CONNECTING;
    return;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Reconnecting with static IP");
  if (waitForConnection(WIFI_CONNECT_TIMEOUT_MS)) {
    currentLedState = LED_CONNECTED;
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    currentLedState = LED_CONNECTING;
    Serial.println("Static-IP reconnect failed — possible IP conflict with another "
                    "device already using this address, or the router rejected it. "
                    "Dashboard is unreachable until this resolves.");
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
             align-items:center; justify-content:center; }
  .overlay.open { display:flex; }
  .panel { background:#1c1c1c; border-radius:12px; padding:20px; width:min(90vw, 360px); }
  .panel h2 { margin-top:0; font-size:1.1rem; }
  .panel label { display:block; font-size:0.85rem; opacity:0.7; margin:12px 0 4px; }
  .panel input[type=text] { width:100%; padding:8px; border-radius:6px; border:1px solid #444;
                             background:#111; color:#eee; font-size:0.95rem; }
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
    <h2>Rename Switches</h2>
    <div id="renameFields"></div>
    <div class="rowbtns">
      <button class="action" onclick="saveNames()">Save</button>
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
    card.innerHTML = `
      <span class="name">${r.name}<span class="status">${r.state ? 'ON' : 'OFF'}</span></span>
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
  const fields = document.getElementById('renameFields');
  fields.innerHTML = '';
  lastData.relays.forEach((r, i) => {
    const label = document.createElement('label');
    label.textContent = `Switch ${i + 1} name`;
    const input = document.createElement('input');
    input.type = 'text';
    input.value = r.name;
    input.id = `nameInput${i}`;
    fields.appendChild(label);
    fields.appendChild(input);
  });
  document.getElementById('settingsOverlay').classList.add('open');
}

function closeSettings() {
  document.getElementById('settingsOverlay').classList.remove('open');
}

async function saveNames() {
  for (let i = 0; i < 4; i++) {
    const val = document.getElementById(`nameInput${i}`).value.trim();
    if (val.length === 0) continue;
    await fetch(`/api/rename?relay=${i}&name=${encodeURIComponent(val)}`);
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
    json += "{\"name\":\"" + relayNames[i] + "\",\"state\":" + (relayState[i] ? "true" : "false") + "}";
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

void handleNotFound() {
  server.send(404, "text/plain", "not found");
}

// ---------------- Setup / loop ----------------

void setup() {
  Serial.begin(115200);

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

  // WiFi: DHCP-discover network config, then pin the host address to .250
  setupWiFi();

  // Routes
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/set", handleSet);
  server.on("/api/all", handleAll);
  server.on("/api/rename", handleRename);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();
  updateStatusLed();
}
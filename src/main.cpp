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
 * - Missed-event catch-up (power outage at trigger time): the device stamps
 *   "last known alive" time into NVS every few minutes and whenever a
 *   schedule event fires. After the first NTP sync following boot, it finds
 *   each enabled schedule's most recent event; if that event happened AFTER
 *   the last alive stamp (i.e. while the device was off), it is applied once.
 *   LIMITS: (1) it can't run until NTP syncs, so for the first seconds after
 *   power returns relays sit in their last SAVED state, then flip; with no
 *   internet it never runs. A battery-backed RTC (e.g. DS3231) is the only
 *   real fix for that. (2) A relay you manually toggled after boot but before
 *   time sync is skipped by catch-up (your manual action wins).
 * - OTA updates from GitHub releases, approved from the dashboard (see OTA.md):
 *     - Checks <repo>/releases/latest/download/manifest.txt every few hours
 *       (and on demand). Shows "update available"; NOTHING installs until
 *       you tap Install.
 *     - The manifest (version, size, sha256) is ECDSA-signed with a private
 *       key that stays on your computer; the device verifies it against
 *       OTA_PUBLIC_KEY_PEM, then checks the downloaded firmware's SHA-256
 *       against the signed manifest BEFORE activating it. TLS certificates
 *       are not validated by default (OTA_ROOT_CA_PEM = nullptr) because
 *       authenticity comes from the signature, not the transport.
 *     - Software rollback: a freshly installed firmware must stay up for
 *       OTA_CONFIRM_AFTER_MS to confirm itself. If it reboots more than
 *       OTA_MAX_UNCONFIRMED_BOOTS times before that, the previous slot is
 *       re-activated. It cannot catch a crash before setup() runs, or a
 *       firmware that runs but misbehaves.
 * - Dashboard is served from flash (no SPIFFS/LittleFS needed), all control
 *   is fetch()-based, no page reloads. Settings panel (gear icon) allows
 *   renaming switches and configuring their schedules.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "time.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "version.h"

// ---------------- USER CONFIG ----------------

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Fallback access point — used only when your real WiFi is unreachable.
// WPA2 requires 8+ chars for AP_PASSWORD. CHANGE THIS before deploying
// anywhere not fully trusted; anyone in range can join this AP otherwise.
const char* AP_SSID     = "ESP32-Relay-Setup";
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

// ---- OTA updates (see OTA.md) ----
// Repo must be PUBLIC. Release tags must be "v" + version (e.g. v1.0.1), and each
// release must carry the assets firmware.bin, manifest.txt and manifest.sig
// (tools/release.sh creates all three).
const char* OTA_GITHUB_OWNER = "YOUR_GITHUB_USER";
const char* OTA_GITHUB_REPO  = "YOUR_REPO";

// ECDSA P-256 PUBLIC key (PEM) matching the private key tools/release.sh signs with.
const char* OTA_PUBLIC_KEY_PEM =
"-----BEGIN PUBLIC KEY-----\n"
"REPLACE_ME\n"
"-----END PUBLIC KEY-----\n";

// Optional PEM root CA to validate GitHub's TLS certificate. nullptr = TLS is not
// validated (safe here ONLY because everything is verified by signature; see OTA.md).
// If you set one, it must cover BOTH github.com and the asset host it redirects to.
const char* OTA_ROOT_CA_PEM = nullptr;

const unsigned long OTA_AUTO_CHECK_INTERVAL_MS = 6UL * 3600UL * 1000UL; // background check period
const unsigned long OTA_FIRST_CHECK_DELAY_MS   = 120UL * 1000UL;        // first check after boot
const unsigned long OTA_CONFIRM_AFTER_MS       = 90UL * 1000UL;         // uptime that confirms a new image
const uint8_t       OTA_MAX_UNCONFIRMED_BOOTS  = 3;                     // then roll back
const size_t        OTA_MAX_MANIFEST_BYTES     = 512;

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

// Missed-event catch-up bookkeeping
const unsigned long ALIVE_STAMP_INTERVAL_MS = 5UL * 60UL * 1000UL; // "last alive" stamp period
unsigned long lastAliveStampMs = 0;
bool catchUpDone = false;       // catch-up runs once per boot, on first valid time
bool manualTouched[4] = {false, false, false, false}; // user changed relay before catch-up ran

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
  // timeSynced is deliberately NOT reset here: configureNTP() runs on every
  // WiFi reconnect, and the clock stays valid across reconnects.
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
}

// Record "the device was alive at this wall-clock time" so that after a power
// loss we can tell which scheduled events happened while it was off.
void stampAlive(time_t now) {
  prefs.putULong("alive", (uint32_t)now);
}

// Epoch of the most recent occurrence (at or before `now`) of HH:MM local time.
// Uses a fixed 86400s day: correct because DST_OFFSET_SEC is 0 in this firmware.
// If you ever enable DST, revisit this.
time_t lastOccurrence(time_t now, uint8_t hour, uint8_t minute) {
  struct tm t;
  localtime_r(&now, &t);
  t.tm_hour = hour;
  t.tm_min  = minute;
  t.tm_sec  = 0;
  t.tm_isdst = -1;
  time_t candidate = mktime(&t);
  if (candidate > now) candidate -= 86400;
  return candidate;
}

// Applies, once per boot, the most recent schedule event that occurred while
// the device had no power. Only the latest missed event per relay matters,
// because it determines the final state. ON transitions are staggered like
// the boot restore, to avoid switching several loads simultaneously.
void catchUpMissedEvents(time_t now) {
  uint32_t lastAlive = prefs.getULong("alive", 0);
  if (lastAlive == 0) {
    Serial.println("Catch-up: no previous uptime record (first boot with this "
                    "firmware, or never synced) — skipping.");
    return;
  }

  bool firstOnApplied = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (!schedules[i].enabled) continue;
    if (manualTouched[i]) continue; // user already made a manual choice this boot

    time_t lastOn  = lastOccurrence(now, schedules[i].onHour,  schedules[i].onMinute);
    time_t lastOff = lastOccurrence(now, schedules[i].offHour, schedules[i].offMinute);
    bool latestIsOn = (lastOn >= lastOff); // tie -> ON, same precedence as live triggering
    time_t latest = latestIsOn ? lastOn : lastOff;

    if (latest <= (time_t)lastAlive) continue;      // that event happened while we were running
    if (relayState[i] == latestIsOn) continue;      // already in the right state

    if (latestIsOn) {
      if (firstOnApplied) delay(STARTUP_STAGGER_MS);
      firstOnApplied = true;
    }
    setRelay(i, latestIsOn);
    Serial.printf("Catch-up: relay %d -> %s (missed scheduled event while powered off)\n",
                  i, latestIsOn ? "ON" : "OFF");
  }
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

  // First valid time after boot: apply any event missed during downtime.
  if (!catchUpDone) {
    catchUpDone = true;
    catchUpMissedEvents(now);
    stampAlive(now);
    lastAliveStampMs = millis();
    lastScheduleCheckMinute = minuteOfDay; // this minute's event (if any) is already covered above
    return;
  }

  // Periodic "still alive" stamp (cheap: NVS skips unchanged values and
  // wear-levels; one write per 5 min is negligible wear by my estimate).
  if (millis() - lastAliveStampMs >= ALIVE_STAMP_INTERVAL_MS) {
    lastAliveStampMs = millis();
    stampAlive(now);
  }

  if (minuteOfDay == lastScheduleCheckMinute) return; // already handled this minute
  lastScheduleCheckMinute = minuteOfDay;

  bool fired = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (!schedules[i].enabled) continue;
    int onM  = schedules[i].onHour * 60 + schedules[i].onMinute;
    int offM = schedules[i].offHour * 60 + schedules[i].offMinute;
    if (minuteOfDay == onM) {
      setRelay(i, true);
      fired = true;
      Serial.printf("Schedule: relay %d ON (%02d:%02d)\n", i, schedules[i].onHour, schedules[i].onMinute);
    } else if (minuteOfDay == offM) {
      setRelay(i, false);
      fired = true;
      Serial.printf("Schedule: relay %d OFF (%02d:%02d)\n", i, schedules[i].offHour, schedules[i].offMinute);
    }
  }
  // Stamp right after a fired event so a reboot doesn't treat it as "missed".
  if (fired) stampAlive(now);
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

// ---------------- OTA update: GitHub release, signed manifest, approval-gated ----------------
//
// Flow: a check downloads manifest.txt + manifest.sig, verifies the signature, and compares
// versions. If newer, state becomes AVAILABLE and the dashboard offers Install. Install
// downloads firmware.bin, streams it into the inactive OTA slot while hashing it, compares the
// hash to the signed manifest, and only then activates the slot and reboots.
// Nothing here installs without an explicit POST /api/update/apply.

enum OtaState {
  OTA_IDLE, OTA_CHECKING, OTA_UP_TO_DATE, OTA_AVAILABLE,
  OTA_DOWNLOADING, OTA_VERIFYING, OTA_REBOOTING, OTA_ERROR
};

OtaState otaState = OTA_IDLE;
String otaLatestVersion = "";
String otaMessage = "";            // fixed strings + validated versions only (safe to embed in JSON)
uint8_t otaProgress = 0;           // 0-100 while downloading
bool otaCheckRequested = false;
bool otaApplyRequested = false;
bool otaEverChecked = false;
unsigned long otaLastCheckMs = 0;
unsigned long otaNextAutoCheckMs = 0;
bool otaPendingConfirm = false;    // running a freshly installed image that has not confirmed yet
unsigned long otaConfirmAtMs = 0;
size_t otaPendingSize = 0;         // from the signed manifest of the AVAILABLE update
uint8_t otaPendingSha[32];

static inline bool deadlineReached(unsigned long deadline) {
  return (long)(millis() - deadline) >= 0; // wrap-safe
}

bool otaConfigured() {
  return strstr(OTA_GITHUB_OWNER, "YOUR_") == nullptr &&
         strstr(OTA_GITHUB_REPO, "YOUR_") == nullptr &&
         strstr(OTA_PUBLIC_KEY_PEM, "REPLACE_ME") == nullptr;
}

// Strict MAJOR.MINOR.PATCH only (no "v" prefix, no suffixes).
bool parseSemver(const String& s, int& major, int& minor, int& patch) {
  int consumed = 0;
  if (sscanf(s.c_str(), "%d.%d.%d%n", &major, &minor, &patch, &consumed) != 3) return false;
  if (consumed != (int)s.length()) return false;
  return major >= 0 && minor >= 0 && patch >= 0;
}

bool isNewerVersion(const String& remote, const String& local) {
  int r1, r2, r3, l1, l2, l3;
  if (!parseSemver(remote, r1, r2, r3) || !parseSemver(local, l1, l2, l3)) return false;
  if (r1 != l1) return r1 > l1;
  if (r2 != l2) return r2 > l2;
  return r3 > l3;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hexToBytes(const String& hex, uint8_t* out, size_t outLen) {
  if (hex.length() != outLen * 2) return false;
  for (size_t i = 0; i < outLen; i++) {
    int hi = hexVal(hex[i * 2]);
    int lo = hexVal(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

String manifestValue(const String& manifest, const char* key) {
  String needle = String(key) + "=";
  int pos = 0;
  while (pos < (int)manifest.length()) {
    int end = manifest.indexOf('\n', pos);
    if (end < 0) end = manifest.length();
    String line = manifest.substring(pos, end);
    line.trim();
    if (line.startsWith(needle)) return line.substring(needle.length());
    pos = end + 1;
  }
  return "";
}

void otaConfigureTls(WiFiClientSecure& client) {
  if (OTA_ROOT_CA_PEM != nullptr) client.setCACert(OTA_ROOT_CA_PEM);
  else client.setInsecure();
}

// ECDSA (DER) signature over SHA-256(data), checked against OTA_PUBLIC_KEY_PEM.
bool otaVerifySignature(const uint8_t* data, size_t len, const uint8_t* sig, size_t sigLen) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)OTA_PUBLIC_KEY_PEM,
                                       strlen(OTA_PUBLIC_KEY_PEM) + 1);
  if (rc != 0) {
    Serial.printf("OTA: public key parse failed (%d)\n", rc);
    mbedtls_pk_free(&pk);
    return false;
  }
  uint8_t hash[32];
  rc = mbedtls_sha256_ret(data, len, hash, 0);
  if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sigLen);
  mbedtls_pk_free(&pk);
  return rc == 0;
}

// Pumped during long operations so the dashboard, LED and schedules keep working.
void otaPump() {
  server.handleClient();
  updateStatusLed();
  checkSchedules();
}

// Downloads a small file (<= maxBytes, length must be advertised) into buf. Returns length or -1.
int otaFetchSmall(const String& url, uint8_t* buf, size_t maxBytes) {
  WiFiClientSecure client;
  otaConfigureTls(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // GitHub assets redirect to another host
  http.setTimeout(10000);
  if (!http.begin(client, url)) return -1;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("OTA: GET failed, HTTP %d\n", code);
    http.end();
    return -1;
  }
  int len = http.getSize();
  if (len <= 0 || (size_t)len > maxBytes) {
    http.end();
    return -1;
  }
  WiFiClient* stream = http.getStreamPtr();
  int got = 0;
  unsigned long start = millis();
  while (got < len && millis() - start < 10000) {
    int avail = stream->available();
    if (avail > 0) {
      size_t want = (size_t)(avail < (len - got) ? avail : (len - got));
      size_t n = stream->readBytes(buf + got, want);
      if (n == 0) break;
      got += (int)n;
    } else if (!http.connected()) {
      break;
    } else {
      delay(5);
    }
  }
  http.end();
  return got == len ? got : -1;
}

// Returns nullptr on a clean check (state set to UP_TO_DATE or AVAILABLE), else an error text.
const char* otaCheckInner() {
  String base = String("https://github.com/") + OTA_GITHUB_OWNER + "/" + OTA_GITHUB_REPO +
                "/releases/latest/download/";

  static uint8_t manifestBuf[OTA_MAX_MANIFEST_BYTES + 1];
  static uint8_t sigBuf[80];
  int mlen = otaFetchSmall(base + "manifest.txt", manifestBuf, OTA_MAX_MANIFEST_BYTES);
  if (mlen < 0) return "Could not download manifest (no release yet, or network error)";
  int slen = otaFetchSmall(base + "manifest.sig", sigBuf, sizeof(sigBuf));
  if (slen < 0) return "Could not download manifest signature";
  if (!otaVerifySignature(manifestBuf, (size_t)mlen, sigBuf, (size_t)slen))
    return "Manifest signature INVALID - refusing update";

  manifestBuf[mlen] = 0;
  String manifest = String((const char*)manifestBuf);
  String version = manifestValue(manifest, "version");
  long size = manifestValue(manifest, "size").toInt();
  uint8_t sha[32];
  int a, b, c;
  if (!parseSemver(version, a, b, c)) return "Manifest has an invalid version";
  if (size < 100000 || size > 4L * 1024 * 1024) return "Manifest has an invalid size";
  if (!hexToBytes(manifestValue(manifest, "sha256"), sha, sizeof(sha))) return "Manifest has an invalid sha256";

  otaLatestVersion = version;
  otaMessage = "";
  if (!isNewerVersion(version, FIRMWARE_VERSION)) {
    otaState = OTA_UP_TO_DATE;
    return nullptr;
  }
  if (prefs.getString("ota_bad", "") == version) {
    otaState = OTA_UP_TO_DATE;
    otaMessage = String("v") + version + " failed on this device and was rolled back; waiting for a newer release";
    return nullptr;
  }
  otaPendingSize = (size_t)size;
  memcpy(otaPendingSha, sha, sizeof(sha));
  otaState = OTA_AVAILABLE;
  return nullptr;
}

// Manual checks surface errors on the dashboard; background checks fail silently
// and leave the previous state untouched.
bool otaRunCheck(bool userInitiated) {
  if (!otaConfigured()) {
    if (userInitiated) { otaState = OTA_ERROR; otaMessage = "Updates are not configured in the firmware (see OTA.md)"; }
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (userInitiated) { otaState = OTA_ERROR; otaMessage = "Not connected to WiFi"; }
    return false;
  }
  OtaState prevState = otaState;
  String prevMessage = otaMessage;
  otaState = OTA_CHECKING;
  const char* err = otaCheckInner();
  if (err != nullptr) {
    Serial.printf("OTA check failed: %s\n", err);
    if (userInitiated) { otaState = OTA_ERROR; otaMessage = err; }
    else { otaState = prevState; otaMessage = prevMessage; }
    return false;
  }
  otaEverChecked = true;
  otaLastCheckMs = millis();
  Serial.printf("OTA check ok: latest v%s, running v%s\n", otaLatestVersion.c_str(), FIRMWARE_VERSION);
  return true;
}

void otaFail(const char* msg) {
  Serial.printf("OTA install failed: %s\n", msg);
  otaState = OTA_ERROR;
  otaMessage = msg;
}

// Runs only after the user's explicit apply request. Blocks (while pumping the web server).
void otaRunInstall() {
  if (otaPendingSize == 0 || otaLatestVersion.length() == 0) { otaFail("No update is staged"); return; }
  if (otaPendingConfirm) { otaFail("The previous update has not confirmed yet - wait a minute and retry"); return; }
  if (WiFi.status() != WL_CONNECTED) { otaFail("Not connected to WiFi"); return; }

  otaState = OTA_DOWNLOADING;
  otaProgress = 0;
  otaMessage = "";

  String url = String("https://github.com/") + OTA_GITHUB_OWNER + "/" + OTA_GITHUB_REPO +
               "/releases/download/v" + otaLatestVersion + "/firmware.bin";

  WiFiClientSecure client;
  otaConfigureTls(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  if (!http.begin(client, url)) { otaFail("Could not start the download"); return; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); otaFail("Firmware download failed (HTTP error)"); return; }
  int len = http.getSize();
  if (len <= 0 || (size_t)len != otaPendingSize) {
    http.end();
    otaFail("Firmware size does not match the signed manifest");
    return;
  }
  if (!Update.begin(otaPendingSize)) {
    http.end();
    otaFail("Firmware does not fit in the update slot");
    return;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);

  static uint8_t buf[1024];
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  unsigned long lastData = millis();
  const char* err = nullptr;

  while (written < otaPendingSize) {
    otaPump();
    int avail = stream->available();
    if (avail > 0) {
      size_t remaining = otaPendingSize - written;
      size_t want = (size_t)avail;
      if (want > sizeof(buf)) want = sizeof(buf);
      if (want > remaining) want = remaining;
      size_t n = stream->readBytes(buf, want);
      if (n == 0) { err = "Download read error"; break; }
      mbedtls_sha256_update_ret(&sha, buf, n);
      if (Update.write(buf, n) != n) { err = "Flash write failed"; break; }
      written += n;
      lastData = millis();
      otaProgress = (uint8_t)((written * 100UL) / otaPendingSize);
    } else {
      if (!http.connected()) { err = "Connection closed before the download finished"; break; }
      if (millis() - lastData > 20000) { err = "Download stalled"; break; }
      delay(2);
    }
  }
  http.end();

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (err != nullptr) { Update.abort(); otaFail(err); return; }

  otaState = OTA_VERIFYING;
  if (memcmp(digest, otaPendingSha, sizeof(digest)) != 0) {
    Update.abort();
    otaFail("Firmware hash does not match the signed manifest - discarded");
    return;
  }

  // Arm the first-boot trial BEFORE activating the new image, so even a crash
  // on its very first boot is counted.
  prefs.putBool("ota_pend", true);
  prefs.putUChar("ota_boots", 0);
  if (!Update.end()) {
    prefs.putBool("ota_pend", false);
    otaFail("Finalizing the update failed");
    return;
  }

  otaState = OTA_REBOOTING;
  Serial.println("OTA: image verified and activated, rebooting...");
  unsigned long t = millis();
  while (millis() - t < 1500) otaPump(); // let the dashboard see "rebooting"
  ESP.restart();
}

// Call early in setup(), right after prefs.begin(). Counts boots of an unconfirmed
// image and re-activates the previous slot if it keeps failing.
void otaBootCheck() {
  otaPendingConfirm = prefs.getBool("ota_pend", false);
  if (!otaPendingConfirm) return;

  uint8_t boots = (uint8_t)(prefs.getUChar("ota_boots", 0) + 1);
  prefs.putUChar("ota_boots", boots);
  Serial.printf("OTA: trial boot %d of %d for v%s\n", boots, OTA_MAX_UNCONFIRMED_BOOTS, FIRMWARE_VERSION);

  if (boots > OTA_MAX_UNCONFIRMED_BOOTS) {
    prefs.putString("ota_bad", FIRMWARE_VERSION);
    prefs.putBool("ota_pend", false);
    prefs.putUChar("ota_boots", 0);
    otaPendingConfirm = false;
    const esp_partition_t* previous = esp_ota_get_next_update_partition(NULL);
    if (previous != NULL && esp_ota_set_boot_partition(previous) == ESP_OK) {
      Serial.println("OTA: new firmware never confirmed - rolling back to the previous slot");
      delay(100);
      ESP.restart();
    }
    Serial.println("OTA: rollback target unavailable - staying on this firmware");
    return;
  }
  otaConfirmAtMs = millis() + OTA_CONFIRM_AFTER_MS;
}

// Call every loop().
void otaLoop() {
  if (otaPendingConfirm && deadlineReached(otaConfirmAtMs)) {
    prefs.putBool("ota_pend", false);
    prefs.putUChar("ota_boots", 0);
    otaPendingConfirm = false;
    Serial.println("OTA: new firmware confirmed healthy.");
  }

  if (otaApplyRequested) {
    otaApplyRequested = false;
    otaRunInstall();
    return;
  }
  if (otaCheckRequested) {
    otaCheckRequested = false;
    otaRunCheck(true);
    return;
  }
  if (staConnected && otaConfigured() && !otaPendingConfirm && deadlineReached(otaNextAutoCheckMs)) {
    otaNextAutoCheckMs = millis() + OTA_AUTO_CHECK_INTERVAL_MS;
    otaRunCheck(false);
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
  .banner { display:none; background:#1c2b22; border:1px solid #2e8b57; border-radius:10px;
            padding:12px 16px; margin-bottom:16px; font-size:0.9rem; }
  .banner.show { display:block; }
  .banner.error { background:#2b1c1c; border-color:#8b2e2e; }
  .banner button { margin-top:10px; width:100%; }
  .bar { height:6px; background:#333; border-radius:3px; margin-top:8px; overflow:hidden; }
  .bar > div { height:100%; background:#2e8b57; width:0%; transition:width 0.3s; }
  .fw { margin-top:16px; border-top:1px solid #333; padding-top:12px; font-size:0.85rem; }
  .fw button { width:100%; margin-top:8px; }
  #fwStatus { opacity:0.6; margin-top:4px; }
</style>
</head>
<body>
<div class="header">
  <h1>Relay Dashboard</h1>
  <button class="gear" onclick="openSettings()" title="Configure">&#9881;</button>
</div>

<div id="updateBanner" class="banner"></div>
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
    <div class="fw">
      <div id="fwInfo">Firmware</div>
      <div id="fwStatus"></div>
      <button class="action" onclick="checkUpdate()">Check for updates</button>
    </div>
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
      <span class="name">${esc(r.name)}<span class="status">${r.state ? 'ON' : 'OFF'}</span>
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

function esc(t) {
  return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// ---- firmware update UI ----
const BUSY = ['checking', 'downloading', 'verifying', 'rebooting'];
let lastVer = null;
let updState = null;
let updFastTimer = null;

function scheduleFast() {
  if (!updFastTimer) updFastTimer = setTimeout(() => { updFastTimer = null; fetchUpdate(); }, 1000);
}

async function fetchUpdate() {
  try {
    const res = await fetch('/api/update/status');
    const u = await res.json();
    if (lastVer && u.current !== lastVer) { location.reload(); return; } // new firmware = new dashboard
    lastVer = u.current;
    updState = u.state;
    renderUpdate(u);
  } catch (e) { /* device busy or rebooting: keep last state, retry below if it was busy */ }
  if (BUSY.includes(updState)) scheduleFast();
}

function renderUpdate(u) {
  const banner = document.getElementById('updateBanner');
  let html = '', cls = 'banner';
  if (u.state === 'available') {
    html = `Update available: v${esc(u.latest)}<button class="action" onclick="installUpdate('${esc(u.latest)}')">Install v${esc(u.latest)}</button>`;
  } else if (u.state === 'downloading') {
    html = `Downloading v${esc(u.latest)}... ${u.progress}%<div class="bar"><div style="width:${u.progress}%"></div></div>`;
  } else if (u.state === 'verifying') {
    html = 'Verifying firmware...';
  } else if (u.state === 'rebooting') {
    html = 'Installed. Rebooting - relays will restore their saved state.';
  } else if (u.state === 'checking') {
    html = 'Checking for updates...';
  } else if (u.state === 'error') {
    html = 'Update problem: ' + esc(u.message);
    cls += ' error';
  }
  banner.className = html ? cls + ' show' : cls;
  banner.innerHTML = html;

  document.getElementById('fwInfo').textContent =
    'Firmware v' + u.current + (u.pendingConfirm ? ' (new firmware verifying itself...)' : '');
  let status = '';
  if (!u.configured) status = 'Updates are not configured in the firmware.';
  else if (u.state === 'up_to_date') status = u.message ? u.message : 'Up to date.';
  else if (u.state === 'available') status = 'v' + u.latest + ' is available.';
  else if (u.state === 'error') status = u.message;
  if (u.lastCheckSecondsAgo >= 0) status += (status ? ' ' : '') + 'Last checked ' + Math.round(u.lastCheckSecondsAgo / 60) + ' min ago.';
  document.getElementById('fwStatus').textContent = status;
}

async function checkUpdate() {
  document.getElementById('fwStatus').textContent = 'Checking...';
  try { await fetch('/api/update/check', { method: 'POST' }); } catch (e) {}
  updState = 'checking';
  fetchUpdate();
}

async function installUpdate(v) {
  if (!confirm('Install v' + v + '?\n\nThe device will reboot. Relays will drop briefly, then restore their saved state. Do not cut power during the update.')) return;
  try { await fetch('/api/update/apply', { method: 'POST' }); } catch (e) {}
  updState = 'downloading';
  fetchUpdate();
}

fetchStatus();
setInterval(fetchStatus, 4000); // periodic resync in case another client changes state
fetchUpdate();
setInterval(fetchUpdate, 4000);
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
    char onBuf[8], offBuf[8]; // sized for any uint8_t pair; valid values are always 5 chars
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
  manualTouched[idx] = true;
  setRelay(idx, state);
  server.send(200, "text/plain", "ok");
}

void handleAll() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "missing state param");
    return;
  }
  bool state = server.arg("state").toInt() == 1;
  for (uint8_t i = 0; i < 4; i++) {
    manualTouched[i] = true;
    setRelay(i, state);
  }
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
  if (timeSynced) { // an edit must not create retroactive "missed" events
    time_t n;
    time(&n);
    stampAlive(n);
  }
  server.send(200, "text/plain", "ok");
}

const char* otaStateName() {
  switch (otaState) {
    case OTA_CHECKING:    return "checking";
    case OTA_UP_TO_DATE:  return "up_to_date";
    case OTA_AVAILABLE:   return "available";
    case OTA_DOWNLOADING: return "downloading";
    case OTA_VERIFYING:   return "verifying";
    case OTA_REBOOTING:   return "rebooting";
    case OTA_ERROR:       return "error";
    default:              return "idle";
  }
}

bool otaBusy() {
  return otaState == OTA_CHECKING || otaState == OTA_DOWNLOADING ||
         otaState == OTA_VERIFYING || otaState == OTA_REBOOTING;
}

void handleUpdateStatus() {
  String json = "{\"current\":\"" + String(FIRMWARE_VERSION) + "\"";
  json += ",\"configured\":" + String(otaConfigured() ? "true" : "false");
  json += ",\"state\":\"" + String(otaStateName()) + "\"";
  json += ",\"latest\":\"" + otaLatestVersion + "\"";
  json += ",\"progress\":" + String((int)otaProgress);
  json += ",\"message\":\"" + otaMessage + "\"";
  json += ",\"pendingConfirm\":" + String(otaPendingConfirm ? "true" : "false");
  json += ",\"lastCheckSecondsAgo\":" +
          (otaEverChecked ? String((long)((millis() - otaLastCheckMs) / 1000UL)) : String("-1"));
  json += "}";
  server.send(200, "application/json", json);
}

void handleUpdateCheck() {
  if (otaBusy()) { server.send(409, "text/plain", "busy"); return; }
  otaCheckRequested = true;
  otaState = OTA_CHECKING; // reflect immediately; the check runs on the next loop()
  server.send(200, "text/plain", "ok");
}

void handleUpdateApply() {
  if (otaState != OTA_AVAILABLE) { server.send(409, "text/plain", "no update available"); return; }
  otaApplyRequested = true;
  otaState = OTA_DOWNLOADING; // reflect immediately; the install runs on the next loop()
  otaProgress = 0;
  otaMessage = "";
  server.send(200, "text/plain", "ok");
}

void handleNotFound() {
  server.send(404, "text/plain", "not found");
}

// ---------------- Setup / loop ----------------

void setup() {
  Serial.begin(115200);
  prefs.begin("relay", false); // namespace "relay", read-write
  otaBootCheck();               // may roll back to the previous firmware and reboot
  delay(INITIAL_BOOT_DELAY_MS); // let power rails / relay board settle before driving any pins

  // Status LED
  ledcSetup(LED_CHANNEL, LED_FREQ, LED_RESOLUTION);
  ledcAttachPin(STATUS_LED_PIN, LED_CHANNEL);
  currentLedState = LED_CONNECTING;
  updateStatusLed();

  // Load persisted state BEFORE configuring pins, so we know what to drive them to.
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
  server.on("/api/update/status", HTTP_GET, handleUpdateStatus);
  server.on("/api/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/api/update/apply", HTTP_POST, handleUpdateApply);
  server.onNotFound(handleNotFound);
  otaNextAutoCheckMs = millis() + OTA_FIRST_CHECK_DELAY_MS;
  server.begin();
}

void loop() {
  server.handleClient();
  updateStatusLed();
  handleWifiMonitor();
  checkSchedules();
  otaLoop();
}

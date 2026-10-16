#include "ota.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "log.h"
#include "network.h"
#include "ota_config.h"
#include "platform.h"
#include "storage.h"
#include "util.h"
#include "version.h"

namespace ota {
namespace {

State state = State::Idle;
String latest;
String message;
uint8_t progress = 0;
bool checkRequested = false;
bool applyRequested = false;
bool everChecked = false;
unsigned long lastCheckMs = 0;
unsigned long autoCheckAnchor = 0;
bool firstAutoCheckDone = false;
bool pendingConfirm = false;
unsigned long bootMs = 0;
size_t stagedSize = 0;
uint8_t stagedSha[32];
void (*pumpFn)() = nullptr;

bool configured() {
  return !util::isPlaceholder(OTA_GITHUB_OWNER) && !util::isPlaceholder(OTA_GITHUB_REPO) &&
         !util::isPlaceholder(OTA_PUBLIC_KEY_PEM);
}

void pump() {
  platform::feed();
  if (pumpFn) pumpFn();
}

void configureTls(WiFiClientSecure& client) {
#ifdef OTA_ROOT_CA_PEM
  client.setCACert(OTA_ROOT_CA_PEM);
#else
  client.setInsecure();
#endif
}

String baseUrl() { return String("https://github.com/") + OTA_GITHUB_OWNER + "/" + OTA_GITHUB_REPO + "/releases/"; }

bool verifySignature(const uint8_t* data, size_t len, const uint8_t* sig, size_t sigLen) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)OTA_PUBLIC_KEY_PEM, strlen(OTA_PUBLIC_KEY_PEM) + 1);
  if (rc != 0) {
    mbedtls_pk_free(&pk);
    return false;
  }
  uint8_t hash[32];
  rc = mbedtls_sha256_ret(data, len, hash, 0);
  if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sigLen);
  mbedtls_pk_free(&pk);
  return rc == 0;
}

int fetchSmall(const String& url, uint8_t* buf, size_t maxBytes) {
  WiFiClientSecure client;
  configureTls(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  if (!http.begin(client, url)) return -1;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGW("OTA GET failed: HTTP %d", code);
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
    platform::feed();
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

const char* checkInner() {
  String base = baseUrl() + "latest/download/";
  static uint8_t manifestBuf[cfg::kOtaMaxManifestBytes + 1];
  static uint8_t sigBuf[80];

  int manifestLen = fetchSmall(base + "manifest.txt", manifestBuf, cfg::kOtaMaxManifestBytes);
  if (manifestLen < 0) return "Could not download manifest (no release yet, or network error)";
  int sigLen = fetchSmall(base + "manifest.sig", sigBuf, sizeof(sigBuf));
  if (sigLen < 0) return "Could not download manifest signature";
  if (!verifySignature(manifestBuf, (size_t)manifestLen, sigBuf, (size_t)sigLen))
    return "Manifest signature INVALID - refusing update";

  manifestBuf[manifestLen] = 0;
  String manifest((const char*)manifestBuf);
  String version = util::manifestValue(manifest, "version");
  long size = util::manifestValue(manifest, "size").toInt();
  uint8_t sha[32];
  int a, b, c;
  if (!util::parseSemver(version, a, b, c)) return "Manifest has an invalid version";
  if (size < 100000 || size > 4L * 1024 * 1024) return "Manifest has an invalid size";
  if (!util::hexToBytes(util::manifestValue(manifest, "sha256"), sha, sizeof(sha)))
    return "Manifest has an invalid sha256";

  latest = version;
  message = "";
  if (!util::isNewerVersion(version, FIRMWARE_VERSION)) {
    state = State::UpToDate;
    return nullptr;
  }
  if (storage::prefs().getString("ota_bad", "") == version) {
    state = State::UpToDate;
    message = String("v") + version + " failed on this device and was rolled back; waiting for a newer release";
    return nullptr;
  }
  stagedSize = (size_t)size;
  memcpy(stagedSha, sha, sizeof(sha));
  state = State::Available;
  return nullptr;
}

bool runCheck(bool userInitiated) {
  if (!configured()) {
    if (userInitiated) {
      state = State::Error;
      message = "Updates are not configured in the firmware";
    }
    return false;
  }
  if (!network::connected()) {
    if (userInitiated) {
      state = State::Error;
      message = "Not connected to WiFi";
    }
    return false;
  }
  State previousState = state;
  String previousMessage = message;
  state = State::Checking;
  const char* error = checkInner();
  if (error != nullptr) {
    LOGW("OTA check failed: %s", error);
    if (userInitiated) {
      state = State::Error;
      message = error;
    } else {
      state = previousState;
      message = previousMessage;
    }
    return false;
  }
  everChecked = true;
  lastCheckMs = millis();
  LOGI("OTA check: latest v%s, running v%s", latest.c_str(), FIRMWARE_VERSION);
  return true;
}

void fail(const char* text) {
  LOGW("OTA install failed: %s", text);
  state = State::Error;
  message = text;
}

void runInstall() {
  if (stagedSize == 0 || latest.length() == 0) return fail("No update is staged");
  if (pendingConfirm) return fail("The previous update has not confirmed yet - wait a minute and retry");
  if (!network::connected()) return fail("Not connected to WiFi");

  state = State::Downloading;
  progress = 0;
  message = "";

  String url = baseUrl() + "download/v" + latest + "/firmware.bin";
  WiFiClientSecure client;
  configureTls(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  if (!http.begin(client, url)) return fail("Could not start the download");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return fail("Firmware download failed (HTTP error)");
  }
  int len = http.getSize();
  if (len <= 0 || (size_t)len != stagedSize) {
    http.end();
    return fail("Firmware size does not match the signed manifest");
  }
  if (!Update.begin(stagedSize)) {
    http.end();
    return fail("Firmware does not fit in the update slot");
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);

  static uint8_t buf[1024];
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  unsigned long lastData = millis();
  const char* error = nullptr;

  while (written < stagedSize) {
    pump();
    int avail = stream->available();
    if (avail > 0) {
      size_t want = (size_t)avail;
      if (want > sizeof(buf)) want = sizeof(buf);
      if (want > stagedSize - written) want = stagedSize - written;
      size_t n = stream->readBytes(buf, want);
      if (n == 0) {
        error = "Download read error";
        break;
      }
      mbedtls_sha256_update_ret(&sha, buf, n);
      if (Update.write(buf, n) != n) {
        error = "Flash write failed";
        break;
      }
      written += n;
      lastData = millis();
      progress = (uint8_t)((written * 100UL) / stagedSize);
    } else {
      if (!http.connected()) {
        error = "Connection closed before the download finished";
        break;
      }
      if (millis() - lastData > 20000) {
        error = "Download stalled";
        break;
      }
      delay(2);
    }
  }
  http.end();

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (error != nullptr) {
    Update.abort();
    return fail(error);
  }

  state = State::Verifying;
  if (memcmp(digest, stagedSha, sizeof(digest)) != 0) {
    Update.abort();
    return fail("Firmware hash does not match the signed manifest - discarded");
  }

  Preferences& prefs = storage::prefs();
  prefs.putBool("ota_pend", true);
  prefs.putUChar("ota_boots", 0);
  if (!Update.end()) {
    prefs.putBool("ota_pend", false);
    return fail("Finalizing the update failed");
  }

  state = State::Rebooting;
  LOGI("OTA image verified and activated, rebooting");
  unsigned long start = millis();
  while (millis() - start < 1500) pump();
  platform::restart();
}

const char* stateName(State s) {
  switch (s) {
    case State::Checking:
      return "checking";
    case State::UpToDate:
      return "up_to_date";
    case State::Available:
      return "available";
    case State::Downloading:
      return "downloading";
    case State::Verifying:
      return "verifying";
    case State::Rebooting:
      return "rebooting";
    case State::Error:
      return "error";
    default:
      return "idle";
  }
}

bool isBusy() {
  return state == State::Checking || state == State::Downloading || state == State::Verifying ||
         state == State::Rebooting;
}

}  // namespace

void begin() {
  bootMs = millis();
  autoCheckAnchor = bootMs;
  Preferences& prefs = storage::prefs();
  pendingConfirm = prefs.getBool("ota_pend", false);
  if (!pendingConfirm) return;

  uint8_t boots = (uint8_t)(prefs.getUChar("ota_boots", 0) + 1);
  prefs.putUChar("ota_boots", boots);
  LOGI("OTA trial boot %u of %u", (unsigned)boots, (unsigned)cfg::kOtaMaxUnconfirmedBoots);
  if (boots <= cfg::kOtaMaxUnconfirmedBoots) return;

  prefs.putString("ota_bad", FIRMWARE_VERSION);
  prefs.putBool("ota_pend", false);
  prefs.putUChar("ota_boots", 0);
  pendingConfirm = false;
  const esp_partition_t* previous = esp_ota_get_next_update_partition(NULL);
  if (previous != NULL && esp_ota_set_boot_partition(previous) == ESP_OK) {
    LOGW("new firmware never confirmed, rolling back");
    delay(100);
    platform::restart();
  }
  LOGE("rollback target unavailable, staying on this firmware");
}

void setPump(void (*pump)()) { pumpFn = pump; }

void loop() {
  if (pendingConfirm && platform::elapsed(bootMs, cfg::kOtaConfirmAfterMs)) {
    Preferences& prefs = storage::prefs();
    prefs.putBool("ota_pend", false);
    prefs.putUChar("ota_boots", 0);
    pendingConfirm = false;
    LOGI("OTA new firmware confirmed");
  }

  if (applyRequested) {
    applyRequested = false;
    runInstall();
    return;
  }
  if (checkRequested) {
    checkRequested = false;
    runCheck(true);
    return;
  }

  unsigned long wait = firstAutoCheckDone ? cfg::kOtaAutoCheckIntervalMs : cfg::kOtaFirstCheckDelayMs;
  if (network::connected() && configured() && !pendingConfirm && platform::elapsed(autoCheckAnchor, wait)) {
    autoCheckAnchor = millis();
    firstAutoCheckDone = true;
    runCheck(false);
  }
}

bool requestCheck() {
  if (isBusy()) return false;
  checkRequested = true;
  state = State::Checking;
  return true;
}

bool requestApply() {
  if (state != State::Available) return false;
  applyRequested = true;
  state = State::Downloading;
  progress = 0;
  message = "";
  return true;
}

Status status() {
  Status s;
  s.state = state;
  s.stateName = stateName(state);
  s.latest = latest;
  s.message = message;
  s.progress = progress;
  s.configured = configured();
  s.pendingConfirm = pendingConfirm;
  s.lastCheckSecondsAgo = everChecked ? (long)((millis() - lastCheckMs) / 1000UL) : -1;
  return s;
}

}  // namespace ota

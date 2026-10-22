#include "ota.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>

#include "config.h"
#include "log.h"
#include "mbedtls_compat.h"
#include "network.h"
#include "ota_config.h"
#include "platform.h"
#include "storage.h"
#include "util.h"
#include "version.h"

namespace ota {
namespace {

platform::Mutex stateLock;
State state = State::Idle;
String latest;
String message;
volatile uint8_t progress = 0;
bool workerRunning = false;
bool everChecked = false;
unsigned long lastCheckMs = 0;
unsigned long autoCheckAnchor = 0;
bool firstAutoCheckDone = false;
bool pendingConfirm = false;
unsigned long bootMs = 0;
size_t stagedSize = 0;
uint8_t stagedSha[32];
String fetchError;

struct CheckOutcome {
  bool ok;
  String error;
  State state;
  String latest;
  String message;
  size_t stagedSize;
  uint8_t sha[32];
};

bool configured() {
  return !util::isPlaceholder(OTA_GITHUB_OWNER) && !util::isPlaceholder(OTA_GITHUB_REPO) &&
         !util::isPlaceholder(OTA_PUBLIC_KEY_PEM);
}

bool busyLocked() {
  return state == State::Checking || state == State::Downloading || state == State::Verifying ||
         state == State::Rebooting;
}

void configureTls(WiFiClientSecure& client) {
#ifdef OTA_ROOT_CA_PEM
  client.setCACert(OTA_ROOT_CA_PEM);
#else
  client.setInsecure();
#endif
}

String baseUrl() { return String("https://github.com/") + OTA_GITHUB_OWNER + "/" + OTA_GITHUB_REPO + "/releases/"; }

String memoryNote() { return String("; largest free block ") + String((unsigned long)platform::maxAllocHeap()) + " bytes"; }

bool verifySignature(const uint8_t* data, size_t len, const uint8_t* sig, size_t sigLen) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)OTA_PUBLIC_KEY_PEM, strlen(OTA_PUBLIC_KEY_PEM) + 1);
  if (rc != 0) {
    mbedtls_pk_free(&pk);
    return false;
  }
  uint8_t hash[32];
  rc = mbedcompat::sha256(data, len, hash);
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
  if (!http.begin(client, url)) {
    fetchError = "could not start the request";
    return -1;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    fetchError = code > 0 ? String("HTTP ") + String(code) : HTTPClient::errorToString(code);
    http.end();
    return -1;
  }
  int len = http.getSize();
  if (len < 0) {
    String body = http.getString();
    http.end();
    if (body.length() == 0 || body.length() > maxBytes) {
      fetchError = "unexpected response size";
      return -1;
    }
    memcpy(buf, body.c_str(), body.length());
    return (int)body.length();
  }
  if (len == 0 || (size_t)len > maxBytes) {
    fetchError = "unexpected response size";
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
      platform::sleepMs(5);
    }
  }
  http.end();
  if (got != len) {
    fetchError = "timed out reading the response";
    return -1;
  }
  return got;
}

CheckOutcome failure(const String& text) {
  CheckOutcome out;
  out.ok = false;
  out.error = text;
  out.state = State::Error;
  out.stagedSize = 0;
  memset(out.sha, 0, sizeof(out.sha));
  return out;
}

CheckOutcome performCheck() {
  String base = baseUrl() + "latest/download/";
  static uint8_t manifestBuf[cfg::kOtaMaxManifestBytes + 1];
  static uint8_t sigBuf[80];

  int manifestLen = fetchSmall(base + "manifest.txt", manifestBuf, cfg::kOtaMaxManifestBytes);
  if (manifestLen < 0) return failure("Could not download the manifest (" + fetchError + ")" + memoryNote());
  int sigLen = fetchSmall(base + "manifest.sig", sigBuf, sizeof(sigBuf));
  if (sigLen < 0) return failure("Could not download the manifest signature (" + fetchError + ")" + memoryNote());
  if (!verifySignature(manifestBuf, (size_t)manifestLen, sigBuf, (size_t)sigLen))
    return failure("Manifest signature INVALID - refusing update");

  manifestBuf[manifestLen] = 0;
  String manifest((const char*)manifestBuf);
  String version = util::manifestValue(manifest, "version");
  long size = util::manifestValue(manifest, "size").toInt();
  uint8_t sha[32];
  int a, b, c;
  if (!util::parseSemver(version, a, b, c)) return failure("Manifest has an invalid version");
  if (size < 100000 || size > 4L * 1024 * 1024) return failure("Manifest has an invalid size");
  if (!util::hexToBytes(util::manifestValue(manifest, "sha256"), sha, sizeof(sha)))
    return failure("Manifest has an invalid sha256");

  CheckOutcome out = failure("");
  out.ok = true;
  out.latest = version;
  out.state = State::UpToDate;
  if (!util::isNewerVersion(version, FIRMWARE_VERSION)) return out;
  if (storage::prefs().getString("ota_bad", "") == version) {
    out.message = String("v") + version + " failed on this device and was rolled back; waiting for a newer release";
    return out;
  }
  out.state = State::Available;
  out.stagedSize = (size_t)size;
  memcpy(out.sha, sha, sizeof(sha));
  return out;
}

void checkWorker(void* arg) {
  bool userInitiated = arg != nullptr;
  State previousState;
  String previousMessage;
  {
    platform::Guard guard(stateLock);
    previousState = state;
    previousMessage = message;
    state = State::Checking;
  }

  CheckOutcome out;
  if (!configured()) out = failure("Updates are not configured in the firmware");
  else if (!network::connected()) out = failure("Not connected to WiFi");
  else out = performCheck();

  {
    platform::Guard guard(stateLock);
    if (!out.ok) {
      LOGW("OTA check failed: %s", out.error.c_str());
      if (userInitiated) {
        state = State::Error;
        message = out.error;
      } else {
        state = previousState;
        message = previousMessage;
      }
    } else {
      latest = out.latest;
      message = out.message;
      state = out.state;
      if (out.state == State::Available) {
        stagedSize = out.stagedSize;
        memcpy(stagedSha, out.sha, sizeof(stagedSha));
      }
      everChecked = true;
      lastCheckMs = millis();
      LOGI("OTA check: latest v%s, running v%s", out.latest.c_str(), FIRMWARE_VERSION);
    }
    workerRunning = false;
  }
}

void setState(State next) {
  platform::Guard guard(stateLock);
  state = next;
}

String install(const String& version, size_t size, const uint8_t* expectedSha) {
  if (size == 0 || version.length() == 0) return "No update is staged";
  bool unconfirmed;
  {
    platform::Guard guard(stateLock);
    unconfirmed = pendingConfirm;
  }
  if (unconfirmed) return "The previous update has not confirmed yet - wait a minute and retry";
  if (!network::connected()) return "Not connected to WiFi";

  String url = baseUrl() + "download/v" + version + "/firmware.bin";
  WiFiClientSecure client;
  configureTls(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  if (!http.begin(client, url)) return "Could not start the download";
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return "Firmware download failed (" + (code > 0 ? String("HTTP ") + String(code) : HTTPClient::errorToString(code)) +
           ")" + memoryNote();
  }
  int len = http.getSize();
  if (len <= 0 || (size_t)len != size) {
    http.end();
    return "Firmware size does not match the signed manifest";
  }
  if (!Update.begin(size)) {
    http.end();
    return "Firmware does not fit in the update slot";
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedcompat::sha256Starts(&sha);

  static uint8_t buf[1024];
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  unsigned long lastData = millis();
  String error;

  while (written < size) {
    platform::feed();
    int avail = stream->available();
    if (avail > 0) {
      size_t want = (size_t)avail;
      if (want > sizeof(buf)) want = sizeof(buf);
      if (want > size - written) want = size - written;
      size_t n = stream->readBytes(buf, want);
      if (n == 0) {
        error = "Download read error";
        break;
      }
      mbedcompat::sha256Update(&sha, buf, n);
      if (Update.write(buf, n) != n) {
        error = "Flash write failed";
        break;
      }
      written += n;
      lastData = millis();
      progress = (uint8_t)((written * 100UL) / size);
      platform::sleepMs(1);
    } else {
      if (!http.connected()) {
        error = "Connection closed before the download finished";
        break;
      }
      if (millis() - lastData > 20000) {
        error = "Download stalled";
        break;
      }
      platform::sleepMs(2);
    }
  }
  http.end();

  uint8_t digest[32];
  mbedcompat::sha256Finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (error.length() > 0) {
    Update.abort();
    return error;
  }

  setState(State::Verifying);
  if (memcmp(digest, expectedSha, sizeof(digest)) != 0) {
    Update.abort();
    return "Firmware hash does not match the signed manifest - discarded";
  }

  Preferences& prefs = storage::prefs();
  prefs.putBool("ota_pend", true);
  prefs.putUChar("ota_boots", 0);
  if (!Update.end()) {
    prefs.putBool("ota_pend", false);
    return "Finalizing the update failed";
  }

  setState(State::Rebooting);
  LOGI("OTA image verified and activated, rebooting");
  platform::sleepMs(1500);
  platform::restart();
  return "";
}

void installWorker(void*) {
  size_t size;
  uint8_t sha[32];
  String version;
  {
    platform::Guard guard(stateLock);
    size = stagedSize;
    memcpy(sha, stagedSha, sizeof(sha));
    version = latest;
  }
  String error = install(version, size, sha);
  platform::Guard guard(stateLock);
  if (error.length() > 0) {
    LOGW("OTA install failed: %s", error.c_str());
    state = State::Error;
    message = error;
  }
  workerRunning = false;
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

void abandon(const char* text) {
  platform::Guard guard(stateLock);
  workerRunning = false;
  state = State::Error;
  message = text;
}

}  // namespace

void begin() {
  stateLock.init();
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

void loop() {
  bool confirmNow = false;
  {
    platform::Guard guard(stateLock);
    if (pendingConfirm && platform::elapsed(bootMs, cfg::kOtaConfirmAfterMs)) {
      pendingConfirm = false;
      confirmNow = true;
    }
  }
  if (confirmNow) {
    Preferences& prefs = storage::prefs();
    prefs.putBool("ota_pend", false);
    prefs.putUChar("ota_boots", 0);
    LOGI("OTA new firmware confirmed");
  }

  unsigned long wait = firstAutoCheckDone ? cfg::kOtaAutoCheckIntervalMs : cfg::kOtaFirstCheckDelayMs;
  if (!platform::elapsed(autoCheckAnchor, wait) || !network::connected() || !configured()) return;
  {
    platform::Guard guard(stateLock);
    if (workerRunning || pendingConfirm || busyLocked()) return;
    workerRunning = true;
  }
  autoCheckAnchor = millis();
  firstAutoCheckDone = true;
  if (!platform::startTask("ota-check", checkWorker, nullptr, cfg::kOtaTaskStackBytes)) {
    platform::Guard guard(stateLock);
    workerRunning = false;
  }
}

bool requestCheck() {
  {
    platform::Guard guard(stateLock);
    if (workerRunning || busyLocked()) return false;
    workerRunning = true;
    state = State::Checking;
    message = "";
  }
  if (!platform::startTask("ota-check", checkWorker, (void*)1, cfg::kOtaTaskStackBytes)) {
    abandon("Could not start the update task (low memory?)");
  }
  return true;
}

bool requestApply() {
  {
    platform::Guard guard(stateLock);
    if (workerRunning || state != State::Available) return false;
    workerRunning = true;
    state = State::Downloading;
    progress = 0;
    message = "";
  }
  if (!platform::startTask("ota-install", installWorker, nullptr, cfg::kOtaTaskStackBytes)) {
    abandon("Could not start the update task (low memory?)");
  }
  return true;
}

Status status() {
  platform::Guard guard(stateLock);
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

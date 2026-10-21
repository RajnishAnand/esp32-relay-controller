#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef RELEASE_BUILD
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define AP_PASSWORD ""
#define API_TOKEN ""
#else
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h: copy include/secrets.example.h to include/secrets.h and edit it"
#endif
static_assert(sizeof(API_TOKEN) - 1 >= 16, "API_TOKEN must be at least 16 characters");
static_assert(sizeof(AP_PASSWORD) - 1 >= 8 && sizeof(AP_PASSWORD) - 1 <= 63,
              "AP_PASSWORD must be 8-63 characters");
#endif

namespace cfg {

constexpr uint8_t kRelayCount = 4;
constexpr uint8_t kRelayPins[kRelayCount] = {27, 26, 14, 25};
constexpr bool kRelayActiveLow = true;
constexpr unsigned long kRelayStaggerMs = 400;
constexpr unsigned long kBootSettleMs = 1000;

constexpr uint8_t kStatusLedPin = 2;

constexpr const char* kApSsidPrefix = "Relay-";
constexpr uint8_t kStaticHostOctet = 250;
constexpr unsigned long kWifiConnectTimeoutMs = 20000;
constexpr unsigned long kWifiRetryIntervalMs = 15000;

constexpr long kGmtOffsetSec = 19800;
constexpr const char* kNtpServer1 = "pool.ntp.org";
constexpr const char* kNtpServer2 = "time.google.com";
constexpr unsigned long kAliveStampIntervalMs = 300000;
constexpr long kMaxScheduleGapMinutes = 10;

constexpr size_t kNameMaxLen = 24;
constexpr uint8_t kGuestMaxFailures = 8;
constexpr unsigned long kGuestLockoutMs = 60000;
constexpr size_t kScanMaxResults = 20;
constexpr uint32_t kWatchdogTimeoutS = 60;

constexpr unsigned long kOtaAutoCheckIntervalMs = 6UL * 3600UL * 1000UL;
constexpr unsigned long kOtaFirstCheckDelayMs = 120UL * 1000UL;
constexpr unsigned long kOtaConfirmAfterMs = 90UL * 1000UL;
constexpr uint8_t kOtaMaxUnconfirmedBoots = 3;
constexpr size_t kOtaMaxManifestBytes = 512;

}  // namespace cfg

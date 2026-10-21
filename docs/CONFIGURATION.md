# Configuration

## Where settings live

| File | Committed | Purpose |
|---|---|---|
| `include/secrets.h` | No (git-ignored) | WiFi credentials, AP password, admin token (`API_TOKEN`). Development builds only |
| `include/ota_config.h` | Yes | GitHub repository and OTA public key. Not secret |
| `include/config.h` | Yes | Tunables (pins, timing, limits) |
| `src/version.h` | Yes | Firmware version, rewritten by `tools/release.sh` |

## Secrets and how they reach the device

Copy `include/secrets.example.h` to `include/secrets.h` and edit it:

| Macro | Requirement |
|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` | Your network |
| `AP_PASSWORD` | 8-63 characters (WPA2). Used by the fallback access point |
| `API_TOKEN` | The **admin token**. At least 16 characters. Generate with `openssl rand -hex 16` |

A **development** build (`pio run -e esp32dev`) copies these into flash (NVS) at boot, but only when the
contents of `secrets.h` have changed since the last time it did so (the device stores a hash of them). It
ignores any value that is empty or still contains the placeholder text `change-me` / `your-`.
A **release** build (`pio run -e release`, `-DRELEASE_BUILD`) compiles in empty strings: it never contains
credentials and never touches the stored ones.

What is configurable where:

| Setting | Set by | Change later |
|---|---|---|
| Admin token (`API_TOKEN`) | `secrets.h`, USB flash | Edit `secrets.h` and flash a development build |
| Fallback AP password (`AP_PASSWORD`) | `secrets.h`, USB flash | Edit `secrets.h` and flash a development build |
| WiFi network | `secrets.h` first, **or** the API / dashboard | Over the API (`/api/wifi/connect`) at any time |
| Guest password | The API / dashboard, by an admin | Over the API at any time; never in `secrets.h` |

Consequences:

- Credentials are not in the repository and not in published firmware, so OTA binaries can be public.
- The device keeps working after OTA because the credentials already sit in flash.
- **WiFi changed over the API survives re-flashing.** Re-flashing a development build with an *unchanged*
  `secrets.h` does not overwrite it. Editing `secrets.h` (any of the four values) makes the next development
  flash overwrite the stored values, including WiFi, with what the file says.
- To rotate the **admin token** or **AP password**, edit `include/secrets.h` and flash a development build over
  USB. They cannot be changed over the API.
- A device flashed with a release build and no prior credentials is unprovisioned: the API answers
  `503 device not provisioned` and the AP does not start. Flash a development build first.
- A device with an admin token and AP password but **no WiFi** starts its access point; connect to it and set
  the WiFi from the dashboard or API.
- The device refuses to use an admin token shorter than 16 characters or one containing a placeholder.

## OTA settings (`include/ota_config.h`)

| Macro | Meaning |
|---|---|
| `OTA_GITHUB_OWNER`, `OTA_GITHUB_REPO` | Public repository that hosts the releases |
| `OTA_PUBLIC_KEY_PEM` | ECDSA P-256 public key matching your signing key |
| `OTA_ROOT_CA_PEM` (optional) | Define it to validate GitHub's TLS certificate; see [OTA.md](OTA.md) |

Until the placeholders are replaced the device reports "Updates are not configured" and fetches nothing.
`tools/release.sh` refuses to run while they are unchanged.

## Tunables (`include/config.h`)

| Constant | Default | Meaning |
|---|---|---|
| `kRelayPins` | 27, 26, 14, 25 | GPIO per relay (index 0 = IN1) |
| `kRelayActiveLow` | `true` | Flip if your board switches on a HIGH input |
| `kRelayStaggerMs` | 400 | Minimum gap between relay switch-ons |
| `kBootSettleMs` | 1000 | Delay before driving any pin at boot |
| `kStatusLedPin` | 2 | Status LED GPIO |
| `kApSsidPrefix` | `Relay-` | Fallback AP name prefix (4 hex digits follow) |
| `kStaticHostOctet` | 250 | Last octet of the static address |
| `kWifiConnectTimeoutMs` | 20000 | Per connection attempt at boot |
| `kWifiRetryIntervalMs` | 15000 | Background reconnect interval |
| `kGmtOffsetSec` | 19800 | Local time offset for schedules (UTC+5:30). No DST handling |
| `kNtpServer1`, `kNtpServer2` | pool.ntp.org, time.google.com | Time servers |
| `kAliveStampIntervalMs` | 300000 | How often the last-alive time is saved |
| `kMaxScheduleGapMinutes` | 10 | Largest run of skipped minutes that is replayed |
| `kNameMaxLen` | 24 | Maximum relay name length in bytes |
| `kWatchdogTimeoutS` | 60 | Task watchdog timeout |
| `kOtaAutoCheckIntervalMs` | 6 h | Background update check period |
| `kOtaFirstCheckDelayMs` | 120 s | Delay before the first background check |
| `kOtaConfirmAfterMs` | 90 s | Uptime that confirms a freshly installed image |
| `kOtaMaxUnconfirmedBoots` | 3 | Boots before rollback |

Changing the relay count requires changing `kRelayCount`, `kRelayPins`, and the dashboard (which renders
four switches).

## Build environments

| Environment | Command | Use |
|---|---|---|
| `esp32dev` | `pio run -e esp32dev -t upload` | Development. Needs `include/secrets.h`; seeds credentials |
| `release` | `pio run -e release` | Publishing. No secrets compiled in |

The platform is pinned (`espressif32@6.5.0`, Arduino-ESP32 2.0.14). The code uses the 2.x LEDC API
(`ledcSetup`/`ledcAttachPin`); bumping the platform to Arduino-ESP32 3.x requires porting that. SHA-256 calls go
through `src/mbedtls_compat.h`, which supports both mbedtls 2.28 (the framework's) and 3.x (some host distributions).

The partition table is the default dual-slot layout (`default.csv`, 1,310,720 bytes per app slot). Check the
size reported at the end of `pio run`; a firmware larger than one slot cannot be installed over the air.
The 1.0.0 development build measured 1,004,621 bytes (76.6% of a slot).

## Time zone

Set `kGmtOffsetSec` in seconds east of UTC (IST is `5*3600 + 1800 = 19800`). The firmware does not apply
daylight saving time.

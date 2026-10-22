# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **Roles:** the flashed token is now the *admin* credential; admins can set a *guest* password. Guests can view
  status and switch relays (`/api/status`, `/api/set`, `/api/all`) and nothing else. `GET /api/status` reports the
  caller's `role`.
- **Guest password management:** `GET /api/guest`, `POST /api/guest/password`, `POST /api/guest/disable`.
- **Guest brute-force throttle:** 8 failed guest attempts pause guest login for 60 s; admin is unaffected.
- **WiFi over the API and dashboard:** `POST`/`GET /api/wifi/scan`, `GET /api/wifi/status`,
  `POST /api/wifi/connect`. A switch saves the new credentials only after it connects, and reverts to the
  previous network on failure. A device with no network can be provisioned through its access point.
- Dashboard: role-aware layout, WiFi scan/connect, guest password controls, error messages from the device.
- Dashboard tests that run the real page in jsdom (24 tests), and 42 more host tests (106 total).

### Fixed (responsiveness)
- **Update checks and downloads no longer freeze the device.** They run on a background task; previously a check
  blocked the main loop for many seconds (several TLS handshakes), so the dashboard timed out.
- Update failures now say why: the HTTP status or connection error, plus the largest free memory block.
  Responses without a `Content-Length` (chunked) are accepted for the manifest files.
- **WiFi scanning:** it now cancels a pending connection attempt, pauses reconnect retries while it runs, and
  reports (`409`) when the radio refuses to start it. It listens 150 ms per channel instead of 300 ms.
- WiFi modem sleep is turned off to avoid latency spikes and missed connections.
- Network changes are refused while a firmware download is running.
- Dashboard: 12 s request timeouts with a clear message, one polling request at a time (no pile-up behind a busy
  device), and a scan that retries through transient failures instead of giving up.

### Added (diagnostics)
- `GET /api/info` reports `minFreeHeap`, `maxAllocHeap` and `loopMaxMs`.

### Changed
- `/api/info` and the `/api/update/*` endpoints are admin-only (they always needed the only credential that
  existed; guests are refused).
- Development builds seed credentials from `secrets.h` only when it has changed, so a WiFi change made over the
  API is not undone by re-flashing unchanged secrets.
- The dashboard is rewritten in plain HTML (about 10 KB, down from 13 KB, with more features).

### Fixed
- Host tests failed to build on systems with mbedtls 3.x (for example Arch), which removed the `_ret` SHA-256
  functions. `src/mbedtls_compat.h` selects the right names; verified against mbedtls 2.28.8 and 3.6.2.
- `make -C test/host` now checks for its prerequisites and prints the install command instead of failing
  with a compiler error.

## [1.0.0]

First production-oriented release.

### Added
- Four-channel relay control with persisted state, restored at boot.
- Non-blocking, staggered switching-on of relays for boot restore, API calls, schedules and catch-up.
- Per-relay daily on/off schedules with NTP time, missed-event catch-up after a power outage, and
  recovery of minutes skipped while the main loop was busy.
- WiFi with automatic static host address, background reconnect and a fallback access point.
- Web dashboard and HTTP API protected by an access token (`X-API-Key`).
- `GET /api/info` for firmware, uptime, network, time-sync and reset diagnostics.
- Signed over-the-air updates from GitHub releases, approved from the dashboard, with a software rollback.
- Task watchdog, glitch-aware relay pin initialisation, strict input validation.
- Host test suite (64 tests) and a mutation-checked CI workflow.

### Security
- Credentials are no longer compiled into released binaries. Development builds seed them into flash;
  release builds contain none, and `tools/release.sh` refuses to publish a binary that does.
- The API fails closed until a token is provisioned.
- Relay names are validated as UTF-8, escaped in JSON and escaped in the dashboard.
- The dashboard is served with a restrictive Content-Security-Policy.

### Changed
- `state` parameters accept `1`, `0`, `true`, `false`, `on`, `off` and reject anything else
  (previously any value other than `1` meant off).
- Relay indexes must be exactly `0`-`3` (previously non-numeric input silently meant relay `0`).
- `state` in `/api/status` reports the commanded state; physical switch-on can lag by up to
  three stagger intervals.
- Source split into modules; secrets and OTA settings moved to `include/`.

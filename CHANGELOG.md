# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

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

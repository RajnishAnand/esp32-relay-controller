# ESP32 Relay Controller

Firmware for an ESP32 (WROOM-32) driving a 4-channel relay module, with a web dashboard, an HTTP API
for automation and agents, daily schedules, and signed over-the-air updates from GitHub releases. 

> **Mains safety.** If these relays switch mains voltage, the wiring must be done by a qualified person in
> a proper enclosure. This project is software; it cannot make unsafe wiring safe. See
> [docs/HARDWARE.md](docs/HARDWARE.md).

## Features

- Four relays with state saved to flash and restored after a power cut
- Switch-on is **staggered** (400 ms apart) everywhere: boot, API, schedules, catch-up, to limit inrush
- Daily on/off schedule per relay, with **catch-up** if the power was out at the trigger time
- Dashboard: toggle switches, rename switches, edit schedules, firmware update banner
- HTTP API protected by an access token, designed for scripts and agents
- WiFi with automatic static address (`x.x.x.250`), background reconnect, and a fallback access point
- Signed OTA updates from GitHub releases, installed only when you approve, with software rollback
- Credentials stay out of the source tree and out of released binaries

## Hardware

| Part | Notes |
|---|---|
| ESP32 WROOM-32 dev board (ESP-32S) | PlatformIO board `esp32dev` |
| 5 V 4-channel relay module, active-LOW inputs | Most opto-isolated modules |

| Relay | Relay board input | ESP32 GPIO |
|---|---|---|
| 0 | IN1 | 27 |
| 1 | IN2 | 26 |
| 2 | IN3 | 14 |
| 3 | IN4 | 25 |

The onboard LED (GPIO 2) is solid while not connected to your WiFi and gives a short dim pulse while connected.
Pin choices avoid the ESP32 strapping pins; see [docs/HARDWARE.md](docs/HARDWARE.md).

## Quick start

Requires [PlatformIO](https://platformio.org/).

1. **Create your secrets file** (it is git-ignored):

   ```bash
   cp include/secrets.example.h include/secrets.h
   ```

   Edit it: WiFi name and password, a fallback-AP password (8-63 characters), and an API token
   (16+ characters; `openssl rand -hex 16` makes a good one).

2. **Flash over USB:**

   ```bash
   pio run -e esp32dev -t upload
   pio device monitor
   ```

   The device copies the secrets into its flash on boot. Later builds and OTA updates do not need them.

3. **Open the dashboard** at the address shown in the serial log (normally `http://<your-subnet>.250/`),
   enter your API token, and switch relays.

If WiFi is unavailable the device starts an access point named `Relay-XXXX`
(password = your AP password); the dashboard is then at `http://192.168.4.1/`.

## Using the API

Every `/api/*` request needs the token in an `X-API-Key` header:

```bash
BASE=http://192.168.1.250
KEY=your-token

curl -s -H "X-API-Key: $KEY" "$BASE/api/status"
curl -s -H "X-API-Key: $KEY" "$BASE/api/set?relay=0&state=1"
curl -s -H "X-API-Key: $KEY" "$BASE/api/info"
```

Full reference, including tool definitions for agents: [docs/API_CAPABILITIES.md](docs/API_CAPABILITIES.md).

## Updating the firmware

Updates are published as GitHub releases signed with a key you control. The dashboard shows
"Update available" and installs only when you tap **Install**. Setup and the release procedure are in
[docs/OTA.md](docs/OTA.md).

## Documentation

| Document | Contents |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Modules, boot sequence, persistence, scheduling and network behaviour |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | Secrets, tunables, build environments, changing credentials |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Wiring, strapping pins, relay board behaviour, safety |
| [docs/API_CAPABILITIES.md](docs/API_CAPABILITIES.md) | HTTP API reference and agent integration |
| [docs/OTA.md](docs/OTA.md) | Signing keys, releasing, rollback, trust model |
| [docs/SECURITY.md](docs/SECURITY.md) | Threat model and what is and is not protected |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Symptoms and fixes |
| [CHANGELOG.md](CHANGELOG.md) | Release history |

## Repository layout

```
include/        config.h (tunables), secrets.example.h, ota_config.h
src/            firmware modules (relays, scheduler, network, ota, api, ...)
test/host/      host-side unit tests and Arduino/ESP stubs
tools/          release.sh
docs/           documentation
```

## Development

```bash
make -C test/host        # 64 host tests + syntax check of every source file
pio run -e esp32dev      # development build (needs include/secrets.h)
pio run -e release       # release build (no secrets)
```

The host tests run the real source files against stub Arduino/ESP headers and real mbedtls. They cover input
validation, relay staggering, scheduling and catch-up, the OTA trust chain (signatures verified against
`openssl`-signed fixtures), rollback, and the HTTP handlers. CI (`.github/workflows/ci.yml`) runs them and
builds both firmware environments.

## Project status

| Area | Status |
|---|---|
| Relay switching on the pin set above | Verified on hardware (original prototype) |
| Host tests (logic, validation, OTA trust chain) | Automated, 64 passing |
| WiFi static IP, AP fallback, reconnect | Not yet verified on hardware |
| Watchdog, ESP32 `HTTPClient`/`Update`/redirect behaviour, live GitHub OTA | Not yet verified on hardware |
| CI workflow and `release.sh` | Written, not yet run end to end |

Update this table as you verify each area on a real device.

## License

MIT, see [LICENSE](LICENSE). Replace the copyright holder placeholder before publishing.

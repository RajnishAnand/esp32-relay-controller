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
- **Two roles:** an *admin* token (flashed) and a *guest* password (set by the admin). Guests can only view and
  switch relays; admins can configure everything
- HTTP API designed for scripts and agents; give an agent the guest password to limit what it can do
- WiFi with automatic static address (`x.x.x.250`), background reconnect, and a fallback access point
- **WiFi setup over the API or dashboard:** scan for networks and connect, with automatic fallback to the
  previous network if the new one fails
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

   Edit it: WiFi name and password, a fallback-AP password (8-63 characters), and an admin token
   (`API_TOKEN`, 16+ characters; `openssl rand -hex 16` makes a good one).

2. **Flash over USB:**

   ```bash
   pio run -e esp32dev -t upload
   pio device monitor
   ```

   The device copies the secrets into its flash on boot. Later builds and OTA updates do not need them.

3. **Open the dashboard** at the address shown in the serial log (normally `http://<your-subnet>.250/`),
   enter your admin token, and switch relays.

If WiFi is unavailable the device starts an access point named `Relay-XXXX`
(password = your AP password); the dashboard is then at `http://192.168.4.1/`. Sign in with the admin token,
open **Settings > WiFi**, scan, pick your network and connect. After the first flash you can change the WiFi
this way without a cable.

To let someone (or an automation) switch relays without admin rights, set a **guest password** under
**Settings > Guest access**.

## Using the API

Every `/api/*` request needs a credential in an `X-API-Key` header: the admin token (full access) or the
guest password (view and switch only):

```bash
BASE=http://192.168.1.250

curl -s -H "X-API-Key: $GUEST" "$BASE/api/status"
curl -s -H "X-API-Key: $GUEST" "$BASE/api/set?relay=0&state=1"
curl -s -H "X-API-Key: $ADMIN" "$BASE/api/info"
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

Host tests need `g++`, `make`, `openssl`, `python3` and the mbedtls development headers
(the firmware build itself needs none of these; it uses the framework's bundled mbedtls):

| Distribution | Command |
|---|---|
| Debian / Ubuntu | `sudo apt install build-essential libmbedtls-dev openssl python3` |
| Arch | `sudo pacman -S --needed base-devel mbedtls openssl python` |
| Fedora | `sudo dnf install gcc-c++ make mbedtls-devel openssl python3` |

mbedtls 2.28 and 3.x are both supported (`src/mbedtls_compat.h`). If the headers are not in a default
location, pass `MBEDTLS_CFLAGS="-I/path/include" MBEDTLS_LIBS="-L/path/lib -lmbedcrypto"` to `make`.

```bash
make -C test/host        # 93 host tests + syntax check of every source file
(cd test/dashboard && npm install && npm test)   # 18 dashboard tests in a simulated browser (needs Node)
pio run -e esp32dev      # development build (needs include/secrets.h)
pio run -e release       # release build (no secrets)
```

The host tests run the real source files against stub Arduino/ESP headers and real mbedtls. They cover input
validation, relay staggering, scheduling and catch-up, roles and the guest lockout, WiFi scan and
switch-with-revert, credential seeding, the OTA trust chain (signatures verified against `openssl`-signed
fixtures), rollback, and the HTTP handlers. The dashboard tests run the real page in jsdom against a simulated
device. CI (`.github/workflows/ci.yml`) runs them and
builds both firmware environments.

## Project status

| Area | Status |
|---|---|
| Firmware compiles and links (`pio run -e esp32dev`, Arduino-ESP32 2.0.14) | Verified. Flash 1,004,621 of 1,310,720 bytes (76.6%), RAM 14.8% |
| Relay switching on the pin set above | Verified on hardware (original prototype) |
| Host tests (logic, validation, roles, WiFi flow, OTA trust chain) | Automated, 93 passing |
| Dashboard behaviour (roles, WiFi, guest, updates, escaping) | Automated in jsdom, 18 passing |
| WiFi static IP, AP fallback, reconnect, **WiFi scan and network switching** | Not yet verified on hardware |
| Watchdog, ESP32 `HTTPClient`/`Update`/redirect behaviour, live GitHub OTA | Not yet verified on hardware |
| `release` environment build, CI workflow and `release.sh` | Not yet run |

Update this table as you verify each area on a real device.

## License

MIT, see [LICENSE](LICENSE). Replace the copyright holder placeholder before publishing.

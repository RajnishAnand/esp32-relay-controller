# Architecture

## Modules

| Module (`src/`) | Responsibility |
|---|---|
| `main.cpp` | Boot order and the main loop |
| `config.h` (`include/`) | Compile-time tunables; selects secrets for development or release builds |
| `storage` | The single NVS (`Preferences`) namespace, `relay` |
| `credentials` | WiFi, AP, admin token and guest password in NVS; seeds from `secrets.h` in development builds when it has changed |
| `relays` | Relay state, pin driving, staggered switch-on, names |
| `scheduler` | NTP time, daily schedules, catch-up, uptime stamping |
| `network` | WiFi connect (static host address), fallback AP, background reconnect |
| `ota` | Signed update check/install, boot trial and rollback |
| `api` | HTTP server, roles and authentication, validation, JSON |
| `dashboard.h` | The single-page dashboard served from flash |
| `util` | Pure helpers: parsing, validation, JSON escaping, semver, manifest parsing |
| `led`, `platform`, `log.h` | Status LED, watchdog/reset/clock wrappers, logging macros |

Dependencies point one way: `api` uses everything below it; `ota` is independent of `api` and is given a
"pump" callback so the web server keeps running during a download.

## Boot sequence

1. Serial and NVS start.
2. `ota::begin()` runs first: if the previous OTA image is still unconfirmed it counts this boot and may roll
   back (see [OTA.md](OTA.md)).
3. A 1 s settle delay lets power rails and the relay board stabilise before any pin is driven.
4. LED starts; credentials are seeded (development builds only).
5. `relays::begin()` writes the **inactive** level to each pin before switching it to output (to avoid a
   spurious pulse on active-LOW boards), then restores the saved state. Relays that were on are switched on
   one at a time, 400 ms apart. This call blocks until restore is complete (about 1.2 s worst case).
6. Schedules load. `network::begin()` connects WiFi (blocking, up to roughly 20-60 s) or starts the fallback AP.
7. The HTTP server starts and the task watchdog is armed.

The API is therefore unreachable until step 7. While in step 6 the relays already hold their restored state.

## Main loop

One cooperative loop on the Arduino loop task: HTTP requests, LED, scheduler, relay stagger, watchdog feed,
WiFi monitor, OTA bookkeeping. The loop must stay short, because the HTTP server is single-threaded and only
serves a request when the loop reaches it.

Operations that can take many seconds are kept off the loop:

| Operation | Where it runs |
|---|---|
| Update check, update download and flash write | A background task (`ota-check` / `ota-install`, pinned to core 0, 16 KB stack, created per request and deleted when done). Each TLS handshake takes seconds and a check makes several, so this must not run on the loop |
| WiFi scan | Asynchronous in the radio driver; the loop only reads the result |
| WiFi network change, boot-time connect | On the loop, but with the pump so requests keep being served (the boot connect runs before the server starts) |

The OTA task and the loop share the OTA state behind a mutex (`platform::Mutex`). The task writes results in one
step after its network I/O, never while holding the lock across a network call. Persistent storage (NVS) is safe
to use from both. `GET /api/info` reports `loopMaxMs`, the longest the loop has ever taken, so a blocked loop is
visible.

WiFi modem sleep is turned off (`WiFi.setSleep(false)`): the default power saving adds latency and can make an
always-on server miss incoming connections. The device is mains powered, so the power cost does not matter.

Timers use elapsed-time arithmetic (`millis() - since >= duration`) rather than absolute deadlines, which
stay correct across `millis()` wrap and long uptimes.

## Relays

- `state` is the **commanded** state and is what is persisted.
- Switching **off** is immediate and cancels a pending switch-on.
- Switching **on** is queued; at most one relay is switched on per `kRelayStaggerMs`. `setAll(true)` on four
  relays takes about 1.2 s to complete.
- Outputs are active-LOW by default (`kRelayActiveLow`); the API always speaks logical on/off.

## Scheduling

Each relay has one daily schedule: enabled flag, on time, off time (device local time, `kGmtOffsetSec`,
default UTC+5:30, no daylight-saving handling).

- **Needs NTP.** Nothing happens until the clock has synchronised (a year-2023+ timestamp). Time does not
  survive a power cut (no RTC). In fallback-AP mode without internet, schedules are inactive.
- **Edge-triggered.** An event fires when the minute of day equals the on or off time. Between events the
  relay keeps whatever state it has, including manual changes. If on and off times are equal, on wins.
- **Skipped minutes are recovered.** If the loop was busy and minutes were skipped, each missed minute (up to
  `kMaxScheduleGapMinutes`, 10) is processed in order. A larger jump or a clock moving backwards is not
  replayed.
- **Catch-up after a power outage.** The device records a "last alive" time in flash every 5 minutes and right
  after any schedule event or schedule edit. At the first valid time after boot, for each enabled schedule it
  finds the most recent event; if that event happened after the last-alive time, it was missed while powered
  off and is applied once.
  - Only the latest missed event per relay matters.
  - A relay the user toggled after boot but before time sync is skipped.
  - With no last-alive record (first boot of this firmware) nothing is caught up.
  - Catch-up cannot run before NTP syncs, so after power returns relays sit in their saved state for the
    sync delay (seconds, or indefinitely without internet) and then may change. A battery-backed RTC is the
    only way to remove that window.
  - Example: schedule off at 22:00, power out 21:50-22:30. After sync the 22:00 off event is
    applied, so the load runs for a few seconds past power-up before switching off.

## Network

1. Connect with DHCP to learn the real gateway, subnet mask and DNS.
2. If the mask is `255.255.255.0`, reconnect with the same network but the host address forced to
   `kStaticHostOctet` (250). Other masks keep the DHCP address.
3. If WiFi is not configured, or the connection fails, start a fallback access point `Relay-XXXX`
   (XXXX derived from the chip ID) with the provisioned AP password, reachable at `192.168.4.1`. The AP is not
   started if no valid AP password is provisioned.
4. While disconnected the device retries every `kWifiRetryIntervalMs` (15 s) without blocking the web server.
   When the station connects, the AP is shut down.

### Changing the network at runtime

`POST /api/wifi/connect` only queues the request; `network::loop()` performs it:

1. Start the fallback AP (if an AP password is provisioned) so there is a way back in, and leave the current network.
2. Connect to the new network with DHCP, then take the `.250` host address as at boot.
3. **Success:** save the new credentials, stop the AP, report `succeeded`. **Failure:** do not save anything,
   reconnect to the previous network, report `failed` with a message.

Connecting can take well over 20 s (two attempts per network). During it the main-loop pump keeps the web
server, schedules and relays running, and the watchdog is fed. Credentials are written only after a
successful connection, so a typo cannot overwrite a working configuration.

Scanning (`POST /api/wifi/scan`) is asynchronous: the radio scans in the background and `GET` reads the result.
Results are deduplicated, sorted strongest first and capped; names that are empty or not valid UTF-8 are dropped
so the JSON stays valid.

The ESP32 radio cannot scan while it is trying to connect. If the device is not connected, starting a scan
cancels the pending connection attempt, turns driver auto-reconnect off, and pauses the 15 s retry timer until
the scan ends; then both are restored. If the device is connected it is left alone. A scan that the radio refuses
to start is reported (`409`) rather than silently doing nothing. Each channel is listened to for 150 ms
(`kScanDwellMs`) to keep the time away from your channel short; weak distant networks can be missed. Scanning
still takes the radio off the channel it is using for a moment, so a dashboard on the same radio may pause for
a second or two.

## Roles and authentication

One `X-API-Key` header, two credentials. The device compares the value, in constant time, against the admin
token and then the guest password, and the endpoint table in [API_CAPABILITIES.md](API_CAPABILITIES.md) says
what each role may call. Missing or wrong credentials get `401`; a guest on an admin endpoint gets `403`; no
valid admin token stored gets `503` for everyone.

Failed guest attempts are counted globally: 8 failures pause *guest* login for 60 s (a fixed window, not extended
by further attempts). The admin comparison always happens first and is never throttled. A request with no header
is not counted. A guest password cannot equal the admin token.

Limits: the `.250` address is derived once at boot. If WiFi was unavailable at boot, the later background
reconnect uses plain DHCP and the address may differ until the next reboot. No duplicate-address check is
made; if `.250` is taken, reserve it on the router or choose another octet. Resilience beyond this
(captive portal provisioning, mDNS) is not implemented.

## Persistence (NVS namespace `relay`)

| Key | Type | Meaning |
|---|---|---|
| `r0`-`r3` | bool | Commanded relay state |
| `n0`-`n3` | string | Relay names |
| `se`, `soh`, `som`, `sfh`, `sfm` + `0`-`3` | bool / uint8 | Schedule: enabled, on hour, on minute, off hour, off minute |
| `alive` | uint32 | Last-alive epoch for catch-up |
| `wifi_ssid`, `wifi_pass` | string | Saved WiFi network (seeded from `secrets.h`, changeable over the API) |
| `ap_pass`, `api_token` | string | Fallback AP password and admin token (seeded from `secrets.h`) |
| `guest_pass` | string | Guest password, set by an admin |
| `seed_hash` | uint32 | Hash of the `secrets.h` values last seeded, so unchanged secrets never overwrite API changes |
| `ota_pend`, `ota_boots`, `ota_bad` | bool / uint8 / string | OTA trial state and last rolled-back version |

NVS is not encrypted. Anyone with physical access can read the credentials from flash.

## Status LED

| LED | Meaning |
|---|---|
| Solid on | Not connected to your WiFi (includes fallback-AP mode) |
| Short dim pulse about every 1.6 s | Connected to your WiFi |

The LED pin is `kStatusLedPin` (GPIO 2). Some boards have no onboard LED or a different colour.

## Watchdog

`platform::beginWatchdog()` tries to arm a 60 s task watchdog on the main loop and feeds it from the loop and
from every long-running wait. If it cannot be armed it logs a warning and the device runs without it. This
has not been verified on hardware.

## Known limits

- Single-threaded; one long operation blocks everything else (except OTA download, which pumps).
- One schedule per relay, daily, no weekdays.
- No relay position feedback; the API reports the commanded state.
- HTTP only, no TLS on the device's own server.

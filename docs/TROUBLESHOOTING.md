# Troubleshooting

Start with the serial monitor (`pio device monitor`, 115200 baud). The firmware logs every state change.

## The device does not boot or reboots in a loop

| Symptom | Likely cause |
|---|---|
| Brownout detector message, or flash errors at boot | A relay board input holding a strapping pin (especially GPIO 12) high. See [HARDWARE.md](HARDWARE.md) |
| Boot loop after an OTA update | Image crashes early. After 3 unconfirmed boots it rolls back; if it crashes before `setup()` it cannot, and needs a USB reflash |
| Wrong board profile | Use `esp32dev`, not `esp-wrover-kit` |

## Cannot reach the dashboard

| Symptom | Likely cause |
|---|---|
| Nothing at `http://<subnet>.250/` | WiFi not connected: check the LED (solid = not connected) and the serial log. `.250` is only used when the network mask is `255.255.255.0` and WiFi was up at boot. Check the router's client list |
| Works at another address | WiFi reconnected after a drop without a reboot, so DHCP assigned the address. Reboot to get `.250` |
| `http://192.168.4.1/` only | The device is in fallback-AP mode. Join `Relay-XXXX` with the AP password. Check the WiFi credentials |
| Static address conflict | Another device owns `.250`. Reserve it on the router or change `kStaticHostOctet` |

## API and dashboard errors

| Response | Cause |
|---|---|
| `401 unauthorized` | Missing or wrong credential. In the dashboard use "Sign out" and re-enter it |
| `403 admin access required` | You signed in with the guest password. Use the admin token for that action |
| `429 too many failed attempts` | Guest login is paused for 60 s after 8 wrong guesses. Wait, or use the admin token |
| `503 device not provisioned` | No valid token in the device's flash. Flash a development build with a real `include/secrets.h` |
| `400` with a message | A parameter failed validation; see [API_CAPABILITIES.md](API_CAPABILITIES.md) |
| `404` on `/api/update/check` | It is POST-only |
| Dashboard shows "disconnected" briefly | The device is busy (boot, WiFi connect, update check); it recovers by itself |

## Timeouts and slow or missing responses

First measure, then diagnose. From a computer on the same network:

```bash
curl -s -o /dev/null -w "%{time_total}s\n" -H "X-API-Key: $ADMIN" http://<device>/api/status
curl -s -H "X-API-Key: $ADMIN" http://<device>/api/info
```

| In `/api/info` | Meaning |
|---|---|
| `loopMaxMs` in the thousands | Something blocked the main loop for that long. Anything above about 2000 is worth investigating; WiFi connecting at boot and network changes legitimately take longer |
| `maxAllocHeap` below roughly 50000 | Not enough contiguous memory for a TLS connection; update checks will fail with a connection error. `minFreeHeap` shows the lowest total free memory since boot |
| `resetReason` is `watchdog`, `panic` or `brownout` | The device is restarting. A brownout usually means a weak power supply when relays and WiFi draw current together |

Causes, most to least likely:

| Symptom | Likely cause |
|---|---|
| The dashboard stalls for a second or two while scanning | Scanning takes the radio off your channel briefly. The dashboard keeps retrying through it |
| Requests time out with a weak or distant signal | `rssi` in `/api/info` is worse than about -80 dBm. Move the device or the router |
| Occasional single slow responses | The server handles one connection at a time and a browser can hold a spare idle connection open for a few seconds. This is a limit of the Arduino `WebServer`; it recovers by itself |
| Everything stops for tens of seconds after tapping "Check for updates" | Should no longer happen: checks run on a background task. If it still does, send `loopMaxMs` and the serial log |
| Dashboard says "The device did not answer in time" | The device took longer than 12 s. Retry; if it repeats, check `loopMaxMs` and power |

WiFi modem sleep is disabled to avoid latency spikes. These causes are hypotheses from how the hardware and
library behave; none has been confirmed on a device yet, so please report `/api/info` and the serial log if
problems persist.

## WiFi changes

| Symptom | Likely cause |
|---|---|
| The dashboard stopped responding right after connecting to a new network | Expected: the device left your network. Reconnect to the new network and open `http://<new-subnet>.250/` (check your router's client list). If the switch failed it returns to the previous network |
| "Could not connect to X; the previous network was restored" | Wrong password, weak signal, or the network blocks new devices. Nothing was saved |
| "...the previous network is unavailable" | The old network is gone too. The device retries it every 15 s and runs the fallback access point meanwhile |
| Scan returns nothing | The scan is asynchronous; poll `GET /api/wifi/scan` until `state` is `done`. Hidden networks and names that are not valid UTF-8 are not listed |
| "scan could not start (busy or radio unavailable)" | The radio refused (for example it was connecting). Try again in a few seconds |
| "a firmware update is in progress" when connecting | Wait for the update to finish; network changes are refused during a download |
| Cannot sign in on the fallback access point | Join `Relay-XXXX` with the AP password from `secrets.h`, then use `http://192.168.4.1/` and the admin token |
| Guest password forgotten | An admin sets a new one under Settings > Guest access, or via `POST /api/guest/password` |
| Re-flashing undid my WiFi change | You edited `secrets.h` since the last flash, so it was re-applied. Revert the edit, or set the WiFi again |

## Relays

| Symptom | Likely cause |
|---|---|
| Relays behave inverted | Set `kRelayActiveLow` to match your board |
| A relay does not follow a switch-on immediately | Switch-on is staggered, up to about 1.2 s for all four |
| A relay pulses at power-up | Hardware glitch before firmware runs; see [HARDWARE.md](HARDWARE.md) |
| A relay changed state on its own after a power cut | A scheduled event was missed while off and was caught up after time sync. See [ARCHITECTURE.md](ARCHITECTURE.md#scheduling) |

## Schedules do nothing

1. Check `GET /api/info`: `time.synced` must be `true`. Time needs internet access (NTP) and is lost on power cut.
2. Check the time zone (`kGmtOffsetSec`) and that the schedule is enabled.
3. Schedules fire only at the exact minute (or recover minutes skipped while the loop was busy); they do not
   enforce a state between the on and off times.

## Build problems

| Symptom | Cause |
|---|---|
| `Missing include/secrets.h` | Copy `include/secrets.example.h` to `include/secrets.h`, or build `-e release` |
| `API_TOKEN must be at least 16 characters` | Use a longer token |
| `ledcSetup` not found | Platform upgraded past Arduino-ESP32 2.x; keep `espressif32@6.5.0` or port the LED code |
| `mbedtls/pk.h: No such file` when running `make -C test/host` | The host tests need the mbedtls development headers; see the README. `make` prints the install command for your distribution |
| `mbedtls_sha256_ret` undeclared (host tests) | Use the current `src/mbedtls_compat.h`; mbedtls 3.x removed the `_ret` names. Do not rename functions by hand: the firmware's mbedtls 2.28 still needs them |
| `warning: 'return' with no value` in `esp32-hal-uart.c` | Inside the Arduino-ESP32 2.0.14 framework, harmless, not this project |
| Firmware too large for the slot | Reduce size or use a larger partition table |

## OTA

See the troubleshooting table in [OTA.md](OTA.md).

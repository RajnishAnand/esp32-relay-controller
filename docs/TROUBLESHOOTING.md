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
| `401 unauthorized` | Missing or wrong token. In the dashboard use "Forget access token" and re-enter it |
| `503 device not provisioned` | No valid token in the device's flash. Flash a development build with a real `include/secrets.h` |
| `400` with a message | A parameter failed validation; see [API_CAPABILITIES.md](API_CAPABILITIES.md) |
| `404` on `/api/update/check` | It is POST-only |
| Dashboard shows "disconnected" briefly | The device is busy (boot, WiFi connect, update check); it recovers by itself |

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
| `ledcSetup` / `mbedtls_sha256_ret` not found | Platform upgraded past Arduino-ESP32 2.x; keep `espressif32@6.5.0` or port the code |
| Firmware too large for the slot | Reduce size or use a larger partition table |

## OTA

See the troubleshooting table in [OTA.md](OTA.md).

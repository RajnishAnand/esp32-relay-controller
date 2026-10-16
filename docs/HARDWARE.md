# Hardware

> **Mains safety.** Relay modules often switch mains voltage. Wiring, isolation, fusing and enclosures for
> that must be done by a qualified person to your local rules. This document covers the low-voltage control side.

## Parts

- ESP32 WROOM-32 development board ("ESP-32S" 30/38-pin boards), PlatformIO board `esp32dev`.
  This is **not** an ESP-WROVER-KIT: do not select `esp-wrover-kit`, which describes different hardware and
  can break PSRAM-less boards at boot.
- 5 V 4-channel relay module with opto-isolated, active-LOW inputs.

## Wiring

| Relay (API index) | Relay board input | ESP32 GPIO |
|---|---|---|
| 0 | IN1 | 27 |
| 1 | IN2 | 26 |
| 2 | IN3 | 14 |
| 3 | IN4 | 25 |

Also connect: relay module VCC to a 5 V supply that can carry the coil current of all relays, and a common GND
between the ESP32 and the relay module. Check your module's input requirements (some have a `JD-VCC` jumper that
separates coil power from the logic side) and that it responds to 3.3 V logic from the ESP32.

Pins are set in `include/config.h` (`kRelayPins`).

## Active-LOW versus active-HIGH

Most modules switch a relay **on** when its input is pulled **low**. The firmware assumes that
(`kRelayActiveLow = true`) and the API always speaks logical on/off. If your relays behave inverted, set it to
`false`. Confirm with a multimeter on an input pin while commanding the relay.

## Pins to avoid

The ESP32 reads **strapping pins** at the instant it leaves reset, before any firmware runs, to choose its boot
mode and flash voltage.

| GPIO | Role at reset | Risk if something external drives it |
|---|---|---|
| 0 | Boot mode: high = run, low = download mode | Wrong mode; already wired to the BOOT button |
| 2 | Must be low or floating for normal boot | Boot problems; commonly the onboard LED, which is fine |
| 5 | SDIO timing | Low |
| 12 (MTDI) | Flash voltage: low = 3.3 V, **high = 1.8 V** | Boot loop or brownout on 3.3 V-flash modules |
| 15 (MTDO) | Boot log silencing | Low |
| 6-11 | Connected to the module's flash | Unusable as GPIO |

GPIO 12 deserves particular care. Relay boards commonly have an on-board pull-up on each input so relays default
to off. Such a pull-up on GPIO 12 can hold it high at reset and stop the board booting. That is why this project
uses GPIO 27, 26, 14 and 25, none of which are strapping pins.

To test an input for a pull-up: power the relay module alone, leave its input unconnected, and measure the input
against GND. A reading near the supply voltage means a pull-up exists.

## What happens to the relays at power-up

For a short time after reset the GPIOs are high-impedance and the relay board's own pull-ups decide the relay
state. The firmware then:

1. waits 1 s (`kBootSettleMs`) for the rails to settle;
2. writes the inactive level to each pin **before** enabling it as an output, to avoid a spurious pulse on
   active-LOW boards;
3. switches the saved-on relays on one at a time, 400 ms apart.

This minimises glitches but cannot remove them: firmware does not run during the first moments after reset. If a
spurious relay pulse at power-up is unacceptable, fix it in hardware (pull resistors matched to your board, or a
supervisor circuit).

## Status LED

GPIO 2 (`kStatusLedPin`). Solid while not connected to your WiFi; a short dim pulse about every 1.6 s while
connected. Colour and presence vary by board; some boards have no onboard LED.

## Boot-time notes

- A device in its fallback access-point mode shows the solid LED, because it is not connected to your network.
- Holding BOOT while pressing EN puts the board in download mode for USB flashing.

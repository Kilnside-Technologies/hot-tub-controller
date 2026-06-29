# Pinout reference — Balboa GS501Z J2 topside display bus

> The J2 RJ45 is **not** RS485. It is the proprietary **topside display bus**: a
> synchronous clock + data serial the mainboard uses to drive the VL260 panel's
> 7-segment display. The decoder taps it in parallel with the panel, and button
> presses are injected by directly driving the panel's button lines. See
> `git log` for the dead `balboa_spa` / JZK / LM2596 / PC817 history.

All values below are **verified on our actual hardware** — not from a datasheet.
The cable was continuity-tested 2026-06-23; the signal mapping is proven by the
live decoder reading correct water temperature off it.

## The cable — 568B (continuity-verified)

Our tap is a cut Cat5e patch lead wired to **TIA-568B**. Hold the RJ45 plug with
the **gold contacts facing you and the cable pointing down** (locking tab on the
back, hidden) — **pin 1 is on the far left**, pin 8 on the far right.

| RJ45 pin | 568B colour    | J2 signal | ESP32 | Handling |
|----------|----------------|-----------|-------|----------|
| 1 | white / orange | **+5 V** | 5V/VIN | Powers the ESP32 when untethered. Metered clean steady 5 V. **Never with USB connected.** |
| 2 | orange | **Light button** | GPIO33 | 1 kΩ series tap (bidirectional line) |
| 3 | white / green | **Jets button** | GPIO4 | 1 kΩ series tap (bidirectional line) |
| 4 | blue | **GND** | GND | mandatory common reference + divider ground |
| 5 | white / blue | **DATA** | GPIO34 | board→MCU, 5 V → 1k series / 2.2k-to-GND divider → GPIO34 (input-only) |
| 6 | green | **CLOCK** | GPIO35 | board→MCU, 5 V → 1k series / 2.2k-to-GND divider → GPIO35 (input-only). ISR samples DATA on the rising edge. |
| 7 | white / brown | **Blower button** | GPIO16 | 1 kΩ series tap (bidirectional line) |
| 8 | brown | **Temp button** | GPIO17 | 1 kΩ series tap (bidirectional line) |

> ⚠️ **Orange/green caution.** The clock wire is **green** (pin 6), not orange.
> Early notes mislabelled it — a green↔white-green misread that caused an
> hours-long clock/data swap bug. Confirm by the twisted pair: green (pin 6)
> twins with white/green (pin 3); orange (pin 2) twins with white/orange (pin 1).

### Why the read pins are certain

- **pin 4 = blue = GND** and **pin 5 = white/blue = DATA** are the 568B *blue
  pair* — unmistakable colour, fixes the plug orientation.
- **pin 1 = white/orange = +5 V** was metered directly.
- That forces **568B**, so **pin 6 = green = clock**.
- The decoder reads correct temperature with data on GPIO34 / clock on GPIO35,
  which closes the loop.

## ESP32 WROOM-32E — full GPIO map

| ESP32 pin | Function | Connects to |
|-----------|----------|-------------|
| GPIO34 | Display **DATA** in (input-only) | J2 pin 5 (white/blue) via 1k series / 2.2k-to-GND divider |
| GPIO35 | Display **CLOCK** in (input-only, ISR) | J2 pin 6 (green) via 1k series / 2.2k-to-GND divider |
| GPIO33 | **Light** button (write) | J2 pin 2 (orange) via 1 kΩ |
| GPIO4 | **Jets** button (write) | J2 pin 3 (white/green) via 1 kΩ |
| GPIO16 | **Blower** button (write) | J2 pin 7 (white/brown) via 1 kΩ |
| GPIO17 | **Temp** button (write) | J2 pin 8 (brown) via 1 kΩ |
| GPIO32 | 1-Wire DS18B20 (water; ambient disabled until probe #2 is wired) | shared data, 4.7 kΩ pull-up to 3V3 |
| GPIO13 | DHT11 cabinet temp/humidity | data (most modules carry their own pull-up) |
| GPIO14 | Reed lid switch | other side to GND; internal pull-up in software |
| GND | Common ground | J2 pin 4 (blue) + divider GND rail + all sensors |
| 5V/VIN | Power (untethered, parasitic) | J2 pin 1 (white/orange). Spa SMPS browns out under WiFi spikes — fit VIN decoupling caps (2×1000µF + 2×100nF), see wiring.md / NOTES.md. **Never with USB at the same time.** |
| USB | Power + flash + logs | bench / OTA |

GPIO34/35 are **input-only** (no internal pull-ups — the divider sets the level)
and are hard-coded in the vendored `components/inputs/` decoder.

## Button write — direct GPIO, no optocoupler

The four panel buttons are bidirectional lines on J2, each tapped through a
**1 kΩ series resistor** straight to a GPIO — **no PC817 / opto / relay /
transistor.** Discovered 2026-06-23 that the existing 1 kΩ tap is sufficient
current-limiting (≤3.3 mA into the board's input).

- **Idle:** the GPIO is held **INPUT (high-impedance)** so the board is free to
  drive its own echo pulses. The board echoes panel-button presses back on the
  same line as ~200 ms HIGH pulses; an active-LOW idle drive would suppress
  those echoes and make the physical panel unresponsive (diagnosed live
  2026-06-23 — the previous `output:` platform held the pins OUTPUT-LOW).
- **To press:** briefly set **OUTPUT HIGH for ~80 ms**, then release back to
  INPUT. Done with raw esp-idf GPIO calls (the `output:` platform can't switch
  pin mode dynamically).

The Temp button is the setpoint / heat-shed lever — driven closed-loop by the
"ride-the-bounce" walker (`walk_setpoint` in `hot-tub.yaml`): press Temp to wake
set-mode, read the revealed setpoint off the blank-flash, keep pressing until it
matches the target, then let set-mode time out to commit.

## Level shifting (read lines)

Both read lines use the same divider: **1k series** (J2 core → node) + **2.2k to
GND** (node → GND), node → GPIO. 5 V × 2.2/3.2 ≈ 3.4 V at the pin — within the
ESP32's tolerance, edges crisp enough for the ~10 µs bus bits. If clock framing
ever wanders, drop the **clock** divider to 470 Ω / 820 Ω for lower impedance.

> Note: pin 3 (white/green) is the **Jets button line**, not a spare. The earlier
> idea of grounding pin 3 to give the clock a paired return is therefore off the
> table — if clock edges ever get jittery, lower the divider impedance instead.

## Frame format (decode reference)

24-bit frames, ~50/s, data sampled on clock **rising** edge: p1(7) + p2(7) +
p3(7) + p4(3). Digits p2/p3 are 7-seg (water temp when steady; setpoint shows
during the blank-flash of set mode). Status bits (GS501Z): **heater = p1 bit2,
light = p4 bit0, pump = p4 bit1, blower = p4 bit2**. Startup shows "Pr"
(priming). Full decode logic lives in `components/inputs/esp32-spa.h`.

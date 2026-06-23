# Pinout reference — Balboa GS501Z J2 topside display bus

> **This supersedes the old RS485 pinout.** The J2 RJ45 is **not** RS485. It is
> the proprietary **topside display bus**: a synchronous clock + data serial the
> mainboard uses to drive the VL260 panel's 7-segment display. The decoder taps
> it in parallel with the panel. See `git log` for the dead `balboa_spa`/JZK/
> LM2596 history.

All values below are **verified on our actual hardware** — not from a datasheet.
The cable was continuity-tested 2026-06-23; the signal mapping is proven by the
live decoder reading correct water temperature off it.

## The cable — 568B (continuity-verified)

Our tap is a cut Cat5e patch lead wired to **TIA-568B**. Hold the RJ45 plug with
the **gold contacts facing you and the cable pointing down** (locking tab on the
back, hidden) — **pin 1 is on the far left**, pin 8 on the far right.

| RJ45 pin | 568B colour      | J2 signal | Handling |
|----------|------------------|-----------|----------|
| 1        | white / orange   | **+5 V**  | Powers the ESP32 (5V/VIN) when untethered. Metered clean steady 5 V. |
| 2        | orange           | unused    | dress off |
| 3        | white / green    | unused    | clock's twisted-pair twin — leave floating (or ground it, see note) |
| 4        | blue             | **GND**   | → ESP32 GND (mandatory common reference) |
| 5        | white / blue     | **DATA**  | board→MCU, 5 V → 1k/2.2k divider → **GPIO34** (input-only) |
| 6        | **green**        | **CLOCK** | board→MCU, 5 V → 1k/2.2k divider → **GPIO35** (input-only). ISR samples DATA on the rising edge. |
| 7        | white / brown    | unused    | dress off |
| 8        | brown            | unused    | dress off |

> ⚠️ **Orange/green caution.** The clock wire is **green**, not orange. Early
> notes mislabelled it "orange" — a green↔white-green misread that caused an
> hours-long clock/data swap bug. Confirm by the twisted pair: each solid core is
> twisted with its own white-striped twin (green ↔ white/green, orange ↔
> white/orange). The clock (green, pin 6) twins with **white/green (pin 3)**.

### Why these pins are certain

- **pin 4 = blue = GND** and **pin 5 = white/blue = data** are the 568B *blue
  pair* — unmistakable colour, fixes the plug orientation.
- **pin 1 = white/orange = +5 V** was metered directly.
- That forces **568B**, so **pin 6 = green = clock** (568A would put orange on
  pin 6, but then 5 V could not be on white/orange — the cable can't be both).
- The decoder reads correct temperature with data on GPIO34 / clock on GPIO35,
  which closes the loop: pin 5 → GPIO34 and pin 6 → GPIO35 are the right signals.

## ESP32 WROOM-32E — GPIO map (read-only decoder, current)

| ESP32 pin | Function | Connects to |
|-----------|----------|-------------|
| GPIO34    | Display DATA in | J2 pin 5 (white/blue) via 1k series / 2.2k-to-GND divider |
| GPIO35    | CLOCK in (ISR)  | J2 pin 6 (green) via 1k series / 2.2k-to-GND divider |
| GND       | Common ground   | J2 pin 4 (blue) **and** divider GND rail |
| 5V / VIN  | Power (untethered only) | J2 pin 1 (white/orange) — **never with USB connected at the same time** |
| USB       | Power + flash + logs | bench / OTA |

GPIO34/35 are **input-only** (no internal pull-ups — the divider sets the level)
and are hard-coded in the vendored `components/inputs/` decoder. The component
also drives some GPIOs as outputs for the future button-write path, so do **not**
move the clock onto GPIO26.

Phase-1 power is **USB only**. Pin 1 (+5 V) is the untethered option for later —
wire it to 5V/VIN *only* after confirming USB is unplugged (back-feed risk).

## Level shifting

Both read lines use the same divider: **1k series** (J2 core → node) + **2.2k to
GND** (node → GND), node → GPIO. 5 V × 2.2/3.2 ≈ 3.4 V at the pin — within the
ESP32's tolerance, edges crisp enough for the ~10 µs bus bits. If clock framing
ever wanders, drop the **clock** divider to 470 Ω / 820 Ω for lower impedance.

## Notes / future

- **Clock has no quiet return.** 568B twists data (pin 5) with GND (pin 4) — good
  — but clock (pin 6) twists with the floating white/green (pin 3). If clock edges
  ever get jittery, **grounding pin 3** gives the clock a paired return and should
  clean them up. Not needed today (decoding is stable).
- **Write path (phase 2):** the panel's single temp button is the setpoint/heat-
  shed lever — to be driven via a PC817 optocoupler, closed-loop (read the
  setpoint off the blank-flash, correct direction). Wires into the reserved
  output GPIOs; no re-layout of the read side.

## Frame format (decode reference)

24-bit frames, ~50/s, data sampled on clock **rising** edge: p1(7) + p2(7) +
p3(7) + p4(3). Digits p2/p3 are 7-seg (water temp when steady; setpoint shows
during the blank-flash of set mode). Status bits (GS501Z): **heater = p1 bit2,
light = p4 bit0, pump = p4 bit1, blower = p4 bit2**. Startup shows "Pr"
(priming). Full decode logic lives in `components/inputs/esp32-spa.h`.

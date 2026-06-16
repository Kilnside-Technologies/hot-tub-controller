# Pinout reference

All pin assignments derive from ccutrer's protocol reverse engineering.
**Canonical source**:
https://github.com/ccutrer/balboa_worldwide_app/wiki#physical-layer

⚠️ **Verify against your specific board** with a multimeter before powering up.
The Balboa GS series has been wired several different ways across revisions;
the table below is the most common GS501Z mapping but is **not** a guarantee
for your specific PCB rev.

## Balboa J2 RJ45 — connector pinout

Looking at the **RJ45 socket on the Balboa board** (clip down, contacts
facing you):

| Pin | Signal   | Notes |
|-----|----------|-------|
| 1   | GND      | |
| 2   | GND      | |
| 3   | +12 V    | ~12 V regulated; supplies topside panel + accessories |
| 4   | RS485 B− | Differential pair B |
| 5   | RS485 A+ | Differential pair A |
| 6   | GND      | |
| 7   | +12 V    | (Same rail as pin 3) |
| 8   | GND      | |

> **Confirm** against ccutrer's wiki — pin numbering for RJ45 can be drawn
> either "clip down, contacts toward you" or "clip up, looking into socket",
> which inverts the order. Double-check with a meter against pin 1 of a known
> good cable before crimping.

## Cable (RJ45 patch lead, 568B)

Standard 568B colour code in a patch lead carries the J2 signals as:

| RJ45 pin | 568B colour      | J2 signal |
|----------|------------------|-----------|
| 1        | White / Orange   | GND       |
| 2        | Orange           | GND       |
| 3        | White / Green    | +12 V     |
| 4        | Blue             | RS485 B−  |
| 5        | White / Blue     | RS485 A+  |
| 6        | Green            | GND       |
| 7        | White / Brown    | +12 V     |
| 8        | Brown            | GND       |

**Recommendation**: cut one end off a known-568B patch lead, strip the four
wires you need (pins 3 or 7 = +12 V, any GND pin, pin 4 = B−, pin 5 = A+),
and dress the rest. Don't use untested CCA (copper-clad aluminium) cable for
the power conductors — the 3 A topside loads will brown them out.

## ESP32 WROOM-32E — GPIO mapping

| ESP32 pin | Function          | Connects to               |
|-----------|-------------------|---------------------------|
| 5V        | Power in          | Buck converter OUT+       |
| GND       | Ground            | Buck converter OUT−, JZK GND |
| 3V3       | JZK Vcc           | JZK RS485 module VCC      |
| GPIO16    | UART RX           | JZK TXD (TTL side)        |
| GPIO17    | UART TX           | JZK RXD (TTL side)        |

GPIO16/17 are chosen because they're the default ESP32 secondary UART (UART2)
pins and don't conflict with flash/boot strapping.

**Do not** use GPIO1 / GPIO3 (UART0) — those are the USB programming UART, and
attaching RS485 to them will fight the bootloader on every boot.

## JZK RS485 module

The "auto flow control" variant has no DE/RE pin — direction is sensed from
the start bit on TXD. Pin labels vary by manufacturer but typically:

| Pin     | Connects to                       |
|---------|-----------------------------------|
| VCC     | ESP32 3V3 (module is 3.3 V tolerant) |
| GND     | ESP32 GND                         |
| TXD     | ESP32 GPIO17 (TX)                 |
| RXD     | ESP32 GPIO16 (RX)                 |
| A / A+  | Balboa J2 pin 5 (RS485 A+)        |
| B / B−  | Balboa J2 pin 4 (RS485 B−)        |

## LM2596 buck converter

| Pin     | Connects to                                                |
|---------|------------------------------------------------------------|
| IN+     | Balboa J2 +12 V (pin 3 or pin 7)                           |
| IN−     | Balboa J2 GND  (pin 1, 2, 6, or 8)                         |
| OUT+    | ESP32 5V pin **(only after trimming to 5.0 V on the bench)** |
| OUT−    | ESP32 GND, JZK GND                                         |

**Bench procedure**: power IN+/IN− from a 12 V supply, multimeter on OUT+/OUT−,
turn the trim pot until it reads 5.0 V ± 0.05 V, *then* wire it in. The pots
ship at random factory positions; some have shipped at ~30 V open-circuit.

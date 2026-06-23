# Handoff: Balboa GS501Z hot-tub controller — ESPHome decoder + button write-path

> Working notes for picking this up cold. Branch: `claude/j2-sniffer`.
> Last updated 2026-06-23.

## What this project is
ESP32 + ESPHome taps the hot tub's **Balboa GS501Z** mainboard via its **J2
topside-display bus** (NOT RS485 — it's a proprietary synchronous clock+data
display bus). Goal: read water temp/setpoint/status, then **write setpoint** to
shed/shift the 3 kW heater on electricity price. ESPHome configs in `esphome/`.

## Hardware facts that are LOCKED (verified)
- **Cable is 568B** (continuity-verified). Pin → colour → signal → GPIO:
  - pin 1 = white/orange = **+5 V**
  - pin 4 = blue = **GND** → ESP GND (mandatory common ref)
  - pin 5 = white/blue = **DATA** → GPIO34 (input-only)
  - pin 6 = **green** = **CLOCK** → GPIO35 (input-only) — *clock is green, NOT
    orange; the old "orange" label was a green↔white-green misread*
  - pins **2/3/7/8 = the four buttons** (2=orange, 3=white/green,
    7=white/brown, 8=brown)
- **Decoder is LIVE and committed** (`esphome/decoder.yaml`, vendored component
  `esphome/components/inputs/esp32-spa.h`). Reads Water Temp, Set Temp,
  Heater/Pump/Light/Blower, Error code. Device IP **10.20.0.34** on WiFi SSID
  **`Fitchett Folly IOT`** (IoT VLAN — NOT "Fitchett Folly").
- **Decode / status bit map (GS501Z):** heater = p1 bit2, **light = p4 bit0,
  pump/jets = p4 bit1, blower = p4 bit2**. Temp digits p2 (tens) / p3 (units).
  Setpoint shows during the set-mode **blank-flash** (0x00/0x00 frames
  alternating with the value); there is NO flag bit.

## ⚠️ The button / jets mapping problem — read this carefully
This is the hard part and where the confusion lives.

**Core difficulty: the panel is on a SEPARATE socket from J2.** So there are two
distinct mapping questions, needing different methods:

1. **panel button → function** ("the jets button turns jets on"): easy & passive.
   Press a panel button, watch the **decoder's display status bits**
   (pump/blower/light) or the setpoint. The display bus is shared/broadcast, so
   the *result* of any press is visible regardless of button-line wiring.
   **Use this freely — no injection.**
2. **J2 pin → function** ("pin 8 is the temp button"): this is what we actually
   need for the write path, and it is **fundamentally hard to do passively**.
   Pressing the *panel* button may NOT appear on J2's button pins if the button
   lines are port-isolated between the two sockets (unconfirmed). The only
   definitive way to map a *J2 pin* to a function is to **drive that pin and
   watch the display react** — but that means injecting +5 V, which **the user
   has explicitly refused** (safety; doesn't want 5 V down the board).

**Empirical anchors we DO have:**
- **Buttons are active-HIGH**: a press = momentarily short the pin to **+5 V**
  (pin 1). Established from an accidental tap.
- **The accidental tap**: user connected **white/green (pin 3) to +5 V and the
  JETS came on.** BUT this was days ago and the **user's memory is foggy** about
  which wire exactly — treat as a strong hint, not proof. This is "the jets bit"
  under active doubt.
- **The bus is SCANNED**: the board polls the button lines (~every 10 ms). So a
  plain `gpio` `binary_sensor` on a button line won't show a clean steady "idle
  low / press high" — it samples a *pulsed* line and reads can flicker/mislead.
  **This is probably why the passive sniff is frustrating.** Consider capturing
  the scan-pulse *pattern* (ISR / pulse_counter / fast sampling) and looking at
  how a press perturbs the duty/pattern, rather than reading a static level.

**A memory note records a full button map as if locked** —
`pin2=Light, pin3=Jets, pin7=Blower, pin8=Temp`, active-high, scanned (~10 ms),
"pulse don't hold (~80 ms)", and even claims a write test drove the setpoint
30→33 via a pin1→pin8 jumper. **Treat that map as an unverified HYPOTHESIS, not
fact** — the user is re-deriving it precisely because they don't trust the foggy
accidental-tap recollection. The `pin3=Jets` entry is the one under active doubt.

**Recommended path for the jets / button mapping:**
- First settle topology: press a panel button and check whether **any** J2 button
  pin changes at all. If nothing ever changes on J2 from panel presses → button
  lines are port-isolated → passive mapping of J2-pin→function is impossible, and
  you must either (a) get user consent for a **current-limited** drive (1 kΩ
  series resistor, brief tap, one pin at a time — same as the opto will do), or
  (b) park J2-pin mapping until the optocoupler arrives.
- Use the **display-status cross-check** (press panel button → watch decoder
  bits) to confirm panel-button→function safely meanwhile.
- Sniffer firmware: `esphome/button-sniffer.yaml` — passive, 4 × `gpio
  binary_sensor`, **series resistor only, no ground leg, no 5 V**, mapped
  **pin2→GPIO33, pin3→GPIO4, pin7→GPIO16, pin8→GPIO17** (chosen to dodge decoder
  pins GPIO34/35 and the component's reserved output pins 25/26/27/32; none are
  strapping pins). Logs all four levels every 2 s ("btnstate") plus state
  changes. Given the scanned-bus issue this likely needs upgrading to pulse/edge
  capture.

## Temp-button behaviour (characterised live — for the write loop)
- Step **1 °C** per value-changing press. Range **26–40 °C**. At a limit it
  **reverses (bounce), never wraps**.
- **First press of a session = reveal only** (shows current setpoint, no change).
  Confirmed.
- Direction **toggles** between sessions.
- Pressing keeps the session alive (a ~29-press bounce was one session); commits
  ~**2–3 s / ~4 flashes** after the last press, then reverts to water temp.
- **Decoder reads the setpoint reliably at ~1.5 s/press cadence** (verified:
  tracked 34→33→32), but NOT during a fast 0.5–1 s bounce (value never holds
  still). Minor wart: a spurious water-temp value can publish at the commit
  transition — read the setpoint *during* set-mode, not after.

## Simulation
`sim/setpoint_sim.py` (committed): models the button + a **ride-the-bounce**
controller that reaches any target from any start without predicting direction.
Typical shed ~5 presses / ~9 s; worst case 28 / ~37 s; always one session. Port
this loop into the component once the write pin is confirmed.

## Workflows
- **OTA from this box**: start tunnel as a background task —
  `ssh -i ~/.ssh/ha_kilnside -N -L 3232:10.20.0.34:3232 -L 6053:10.20.0.34:6053 root@62.238.41.183`
  — then a SEPARATE foreground call:
  `~/.local/bin/esphome run --device 127.0.0.1 --no-logs esphome/decoder.yaml`.
  **Do NOT pkill the tunnel mid-command (returns 144).**
- **Read live state**:
  `ssh -i ~/.ssh/ha_kilnside root@62.238.41.183 'curl -s --max-time 8 http://10.20.0.34/events'`.
  To capture during presses, run a longer `--max-time` curl as a background task
  and have the user press during the window.
- Compile: `~/.local/bin/esphome compile esphome/<file>.yaml`.
- Secrets in `esphome/secrets.yaml` (gitignored on the box) — **never commit
  secrets, never put creds in memory files.**

## Recent commits on `claude/j2-sniffer`
- `9a0be38` sim: setpoint controller + button model
- `30054fb` docs(pinout): cable lockdown — 568B, clock is green
- `aa0be1e` decoder: Blower sensor + "Pr" priming decode
- `82b68a4` GS501Z bit-map + checksum patch

## Net state / what's blocking
Read side + decode + control algorithm all validated. **Blocked on confirming
which J2 pin is the Temp button**, which is hard passively given separate-socket
topology + scanned bus + the user's no-5 V-injection constraint. The PC817
optocoupler is en route; once it's here (or with user consent to a
current-limited tap) the write pin can be confirmed by driving and watching the
decoder.

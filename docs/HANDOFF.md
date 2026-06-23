# Handoff: Balboa GS501Z hot-tub controller — ESPHome decoder + button write-path

> Working notes for picking this up cold. Branch: `claude/j2-sniffer`.
> Last updated 2026-06-23 (evening — major rewrite after a full RE session).

## What this project is
ESP32 + ESPHome taps the hot tub's **Balboa GS501Z** mainboard via its **J2
topside-display bus**. This is a **proprietary synchronous clock+data display
bus**, NOT RS485 (despite the README's original framing). Goal: read water
temp/setpoint/status and **write setpoint** to shed/shift the 3 kW heater on
electricity price.

ESPHome configs in `esphome/`. Decoder component vendored from kgstorm's GS100
work, patched for GS501Z/VL260 specifics (`esphome/components/inputs/`).

## Hardware facts — LOCKED & VERIFIED
- **Cable is 568B** (continuity-verified). Pin → colour → signal → GPIO:
  - pin 1 = white/orange = **+5 V** (NOT connected unless using as a write-test source)
  - pin 4 = blue = **GND** → ESP GND (mandatory common ref)
  - pin 5 = white/blue = **DATA** → 1 kΩ → GPIO34 (input-only)
  - pin 6 = **green** = **CLOCK** → 1 kΩ → GPIO35 (input-only) — *clock is green, NOT orange; old "orange" label was a green↔white-green misread*
  - pins **2/3/7/8 = the four panel buttons**. Confirmed map:
    - **pin 2 (orange)      = Light**   → 1 kΩ → GPIO33
    - **pin 3 (white/green) = Jets**    → 1 kΩ → GPIO4
    - **pin 7 (white/brown) = Blower**  → 1 kΩ → GPIO16
    - **pin 8 (brown)       = Temp**    → 1 kΩ → GPIO17
- **Polarity: active-HIGH.** Idle = 0 V; press = sustained HIGH; the panel button shorts the pin to +5 V (pin 1).
- **Decoder is LIVE and committed** (`esphome/decoder.yaml`, vendored component `esphome/components/inputs/esp32-spa.h`). Reads Water Temp, Set Temp, Heater, Light, Blower (with caveats — see Decoder issues), and Error code. Device IP **10.20.0.34** on WiFi SSID **`Fitchett Folly IOT`** (IoT VLAN — NOT "Fitchett Folly").

## Protocol decode — locked 2026-06-23 evening

### Display bus (read-only, clock + data on pins 5/6)
- 24-bit frames, layout `7+7+7+3` = p1 | p2 | p3 | p4. ~50 frames/sec, ~1200 clk/s. Data sampled on clock RISING edge.
- **p1 bit2 = heater**
- **p2, p3 = 7-seg digits** — water temp at idle; **setpoint replaces digits during set-mode**, which is detected by alternating `0x00/0x00` blank frames (no flag bit)
- **p4 bit0 = light** (clean ON/OFF — code mapping correct, no filter needed)
- **p4 bit1 = MOTOR RUNNING** (binary; **does NOT distinguish circ-low vs jets-high**). Once the circ pump auto-starts after boot, bit1 stays 1 indefinitely. The old comments labelling this "pump pressed" / "jets" are misleading — the bit reflects motor activity, not user intent.
- **p4 bit2 = blower** (mapping correct, but needs a stability filter — see Decoder issues)

### Button lines (BIDIRECTIONAL — pins 2/3/7/8)
Same wire carries both directions:
- **Idle: 0 V.** The board does NOT continuously scan or pull these lines. (The "scanned bus" theory we held all session was wrong — what looked like ~10 Hz scan pulses was actually ESPHome's `pulse_counter` forcing an internal pull-up, combined with capacitive crosstalk from the nearby clock/data wires, making `binary_sensor` inputs chatter.)
- **Read direction — board echoes panel-button presses to ALL panel ports.** When ANY panel button is pressed, the board emits a single **~200 ms HIGH pulse** (220 ms ± 20 ms) on the matching line on every connected panel port. Triple-validated 2026-06-23 with three consecutive Jets presses (OFF→circ, circ→jets, jets→OFF) — each produced one clean pulse, widths 218 / 201 / 241 ms.
- **Write direction — drive the line HIGH from the ESP.** Board interprets a sustained HIGH on a button pin as a press. **No opto / relay / transistor needed** — set GPIO to OUTPUT, write HIGH for ~80 ms, write LOW (or back to INPUT). The existing 1 kΩ series tap resistor is sufficient current-limiting (≤3.3 mA into the board's input, well below any damage threshold). Validated by manually walking the setpoint 30 → 33 °C earlier today, then re-validated by the entirely accidental "ESP was sending button presses" episode where the pull-up bug drove every button into cycling state.
- **Jets state semantics**: a single Jets press = one event. The PANEL maintains the cycle state locally — Jets cycles `OFF → circ-low → jets-high → OFF`. The display bus only tells us motor-on/motor-off via p4 bit1. To derive the current speed, **count echo pulses on pin 3 from a known starting state** (or read motor-on bit and assume circ-low when bit1=1, since we can't tell circ from jets-high from the bus alone).

## ESPHome wiring gotcha — the painful lesson
- **`pulse_counter` on ESP32 silently overrides the pin schema.** It talks to the PCNT peripheral directly and bypasses ESPHome's standard GPIO config. Setting `pullup: false, pulldown: true` inside the `pin:` block has NO EFFECT — internal pull-up stays ON. With 3.3 V on the GPIO and the 1 kΩ tap resistor connected to the cable, this drives every button pin HIGH continuously, causing the spa board to cycle each button's state on every detection (temp walks, blower clicks, lights flash). **Diagnostic**: multimeter the GPIO header to GND with wires disconnected — should read 0 V; if 3.3 V, the pin config didn't take.
- **Fix**: use `binary_sensor: platform: gpio` with explicit `mode: { input: true, pullup: false, pulldown: true }`. That DOES respect the config and gives clean 0 V idle. See `esphome/button-activity-sniffer.yaml` for the working pattern.

## Decoder issues still TODO (`esphome/components/inputs/esp32-spa.h`)
1. **Add stability filter on blower** (lines 587-594). Currently has none — comment says "blower toggles rarely, no stability needed", which is wrong; spurious bit2 flips during normal frames cause noisy ON/OFF events at idle. Mirror the per-bit threshold already used by pump and light.
2. **Rename/document p4 bit1 as "motor_running"** (not "pump"). The bit doesn't change when Jets are user-pressed — it only changes when the motor physically starts/stops. Comment at line 84 (`light_sensor_ derived from p4 bit1`) is also wrong (light is bit0, pump/motor is bit1).
3. **Optional**: add a Jets state-machine sensor that watches pin-3 echo pulses (needs a separate input GPIO from the button output, OR a fast input mode swap around each write pulse). Three-state output: OFF / circ-low / jets-high. Reset to OFF whenever p4 bit1 goes 1→0.

## Temp-button behaviour (characterised live — for the write loop)
- Step **1 °C** per value-changing press. Range **26–40 °C**. At a limit it **reverses (bounce), never wraps**.
- **First press of a session = reveal only** (shows current setpoint, no change). Confirmed.
- Direction **toggles** between sessions.
- Pressing keeps the session alive (a ~29-press bounce was one session); commits **~2-3 s / ~4 flashes** after the last press, then reverts to water temp.
- **Decoder reads the setpoint reliably at ~1.5 s/press cadence** (verified: tracked 34→33→32 today's session walked 30→33), but NOT during a fast 0.5-1 s bounce (value never holds still). Minor wart: a spurious water-temp value can publish at the commit transition — read the setpoint *during* set-mode, not after.

## Simulation
`sim/setpoint_sim.py` (committed): models the button + a **ride-the-bounce** controller that reaches any target from any start without predicting direction. Typical shed ~5 presses / ~9 s; worst case 28 / ~37 s; always one session. Port this loop into the component once the write driver is wired.

## Sniffer / diagnostic firmware (this session's tools)
- `esphome/button-sniffer.yaml` — 4× `binary_sensor` on the button lines, used to map panel buttons → J2 pins by passive observation. Each press creates a sustained HIGH on its line. **Wiring requirement**: explicit `mode: { input: true, pullup: false, pulldown: true }` on every pin (see the gotcha section).
- `esphome/button-activity-sniffer.yaml` — same wiring, fires on state changes (good for catching brief board-echo pulses).
- `esphome/rawcap.yaml` + `rawcap.h` — distinct-frame capture of the display bus. Use for protocol RE on a new panel variant.
- `esphome/decoder.yaml` — the production decoder.

## Workflows
- **OTA from this box**: open the tunnel as a background task —
  `ssh -i ~/.ssh/ha_kilnside -N -L 3232:10.20.0.34:3232 -L 6053:10.20.0.34:6053 root@62.238.41.183`
  — then a SEPARATE foreground call:
  `~/.local/bin/esphome run --device 127.0.0.1 --no-logs esphome/<file>.yaml`.
  **Do NOT pkill the tunnel mid-command (returns 144).**
- **Read live state**:
  `ssh -i ~/.ssh/ha_kilnside root@62.238.41.183 'curl -s --max-time 8 http://10.20.0.34/events'`.
- Compile only: `~/.local/bin/esphome compile esphome/<file>.yaml`.
- Secrets in `esphome/secrets.yaml` (gitignored on the box) — **never commit secrets, never put creds in memory files.**

## Net state / what's NEXT
Everything is unblocked. Read side ✓, full protocol decoded ✓, write path validated (no opto needed — direct GPIO through 1 kΩ) ✓, jets-echo mechanism understood ✓.

**Build the production firmware**:
1. Start from `esphome/decoder.yaml`.
2. Add the blower stability filter fix to the component.
3. Add four output GPIOs as ESPHome `switch` entities — one per button line — that pulse HIGH for ~80 ms when activated. Wire them through the same 1 kΩ resistors (or run dedicated 1 kΩ output pins parallel to the existing input taps).
4. Add the bounce-controller setpoint loop from `sim/setpoint_sim.py` as either a `script` in YAML or a `number` entity that drives the temp button automatically until target reached.
5. (Optional, for full state) Add a binary input on pin 3 to count Jets echo pulses → derive 3-state speed.

The PC817 optocoupler order can be cancelled.

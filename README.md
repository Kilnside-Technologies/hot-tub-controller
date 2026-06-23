# hot-tub-controller

ESPHome firmware that reverse-engineers and drives the **Balboa GS501Z** hot
tub mainboard (with **VL260 topside panel**) via its J2 panel port, exposing
the spa as native Home Assistant entities — water temperature, set
temperature, heater / pump-motor / light / blower state, four momentary
button entities (Light / Jets / Blower / Temp), and a closed-loop setpoint
controller that walks the temp button to any target.

The Balboa **J2 bus on GS501Z is NOT RS485** (despite a lot of community
speculation) — it's a proprietary 5 V synchronous clock+data display bus.
This repo decodes it, and adds a direct write path that needs no
optocoupler, relay, or transistor — just one ESP32 GPIO per button through
a 1 kΩ series resistor. Full protocol writeup in
[`docs/HANDOFF.md`](docs/HANDOFF.md).

**Status: working end-to-end** on a real GS501Z + VL260 panel.

## Protocol — short version

- **Display bus** (clock pin 6 = green, data pin 5 = white/blue, GND pin 4 =
  blue): 24-bit frames at ~50/s, layout `7+7+7+3`. Fields: heater bit,
  water/set-temp digits, light, motor running, blower. Setpoint shows during
  set-mode — detected by alternating blank frames mixed with the value, no
  flag bit.
- **Button lines** (pins 2/3/7/8 = Light, Jets, Blower, Temp): bidirectional,
  **active-HIGH**. Idle at 0 V; a press shorts to +5 V (pin 1). The board
  echoes any panel-button press as a ~200 ms HIGH pulse on the matching
  line of every connected panel port — so a second tap point can both write
  presses (drive HIGH for ~80 ms) and observe presses from other panels.
- **Write path**: a plain ESP32 GPIO driving HIGH through a 1 kΩ series
  resistor is enough to register a press (≤3.3 mA into the board's input).
  No optocoupler / relay / transistor needed.

Full decode, bit map, frame format, ESPHome wiring gotchas (the
`pulse_counter` PCNT pull-up trap that cost hours), and the press echo
mechanism: [`docs/HANDOFF.md`](docs/HANDOFF.md).

If you're working on GS501Z, VL260, or a similar topside-panel board and
want to redo the mapping cycle yourself, the diagnostic firmwares are in
`esphome/`: `rawcap.yaml` (distinct-frame capture), `button-sniffer.yaml`
(passive level read), `button-activity-sniffer.yaml` (state-change events).

## Hardware

| Item | Part | Notes |
|---|---|---|
| Hot tub controller | Balboa GS501Z (PN 54511-01) | 3 kW Incoloy element, VL260 topside |
| MCU | ESP32 WROOM-32E dev board | Powered from USB or from J2 pin 1 (+5 V) |
| Bus tap | 6× 1 kΩ–2.2 kΩ resistors | Series on each tapped pin (clock, data, 4 button lines). Limits current to ~3 mA worst case |
| Connection | Balboa J2 RJ45 (spare panel port) | 568B patch lead; the panel can stay in its own port, J2 taps in parallel |

The README's earlier hardware drafts called for an LM2596 buck converter,
JZK RS485 transceiver, and PC817 optocoupler — **none of these are needed**.
The bus is 5 V; the ESP32 is fine on USB or the J2 5 V rail; and the
direct-GPIO write path makes the opto redundant.

## Cable colours (568B)

| RJ45 pin | 568B colour | Signal | ESP32 |
|---|---|---|---|
| 1 | white/orange | +5 V | (not connected — or feed Vin if running off the bus) |
| 2 | orange | Light button | GPIO33 via 1 kΩ |
| 3 | white/green | Jets button | GPIO4 via 1 kΩ |
| 4 | blue | GND | ESP GND (mandatory common ref) |
| 5 | white/blue | DATA | GPIO34 via 1 kΩ (input-only pin) |
| 6 | green | CLOCK | GPIO35 via 1 kΩ (input-only pin) |
| 7 | white/brown | Blower button | GPIO16 via 1 kΩ |
| 8 | brown | Temp button | GPIO17 via 1 kΩ |

Older notes had clock labelled as the orange wire — that was a
green↔white-green misread; **clock is green**. Continuity-verified on a real
cable, end-to-end.

## Quick start

1. **Wire it up** per the table above. Common ground (pin 4 ↔ ESP GND)
   first, before anything else.
2. **Fill in secrets** — `cp secrets.yaml.template esphome/secrets.yaml`,
   fill in WiFi / API / OTA values.
3. **Flash the production firmware** — `esphome run esphome/hot-tub.yaml`.
   First flash is over USB; subsequent flashes are OTA. (For a read-only
   diagnostic build use `decoder.yaml` instead — same decoder, no button
   writes.) Verify it picks up water temperature within ~10 s.
4. **Add to HA** — the ESPHome integration auto-discovers the `hot-tub`
   device via mDNS; paste the `api_key` from `esphome/secrets.yaml` when
   prompted.

## HA entities exposed by the production firmware

Under device `hot-tub` (entity IDs prefixed `hot_tub_*` by default — if you
assign the device to an HA area, that area's slug will prefix instead, e.g.
`cabin_hot_tub_*`):

- **Sensors**: water temperature, set temperature (only valid during
  set-mode; otherwise unknown)
- **Binary sensors**: heater, pump (motor running — binary, doesn't
  distinguish circ-low vs jets-high), light, blower
- **Buttons (momentary)**: Press Light, Press Jets, Press Blower, Press
  Temp — each pulses the corresponding GPIO HIGH for 80 ms = one button
  press on the spa
- **Number**: Target Setpoint (26–40 °C, integer)
- **Button**: Walk Setpoint to Target Now — runs the bounce-controller
  loop until the decoded setpoint matches the target
- **Text sensor**: Error Code (Balboa fault display)
- **Diagnostics**: WiFi signal, uptime, ESP version, IP

The setpoint walker uses the **ride-the-bounce** algorithm
(`sim/setpoint_sim.py`): one wake press to reveal the current setpoint,
then 1.5 s-cadenced presses until the decoder reports the target; at a
range limit the panel reverses direction (it doesn't wrap), so any
start→target combination converges in one session. Worst case ~28 presses
/ ~45 s; typical ~5 presses / ~10 s.

## Repository layout

```
hot-tub-controller/
├── README.md
├── secrets.yaml.template
├── esphome/
│   ├── hot-tub.yaml                 # ★ Production firmware
│   ├── decoder.yaml                 # Read-only diagnostic build
│   ├── components/inputs/           # Vendored kgstorm decoder + GS501Z patches
│   ├── button-sniffer.yaml          # Diagnostic: passive button-line read
│   ├── button-activity-sniffer.yaml # Diagnostic: button-line state changes
│   └── rawcap.yaml + rawcap.h       # Diagnostic: distinct-frame capture
├── sim/
│   └── setpoint_sim.py              # Bounce-controller (ported into hot-tub.yaml)
└── docs/
    ├── HANDOFF.md                   # ★ Full protocol decode + dev notes
    ├── wiring.md
    ├── pinout.md
    └── commissioning.md
```

## Community references

This work builds on the community reverse-engineering of Balboa boards.
The GS501Z + VL260 J2 bus turned out to be NOT RS485 — it's the
topside-display bus, which earlier RS485-focused projects don't decode.
The decoder is a fork of kgstorm's GS100 component, patched for GS501Z
bit-map and frame quirks.

- **kgstorm GS100 + VL260 component** — [kgstorm/Balboa-GS100-with-VL260-topside](https://github.com/kgstorm/Balboa-GS100-with-VL260-topside)
  Closest existing implementation for this panel family. Our decoder
  vendors this component and patches GS501Z specifics (different p4
  bit map, blower stability filter, set-mode detection).
- **Canonical RS485-spa protocol RE** — [ccutrer/balboa_worldwide_app](https://github.com/ccutrer/balboa_worldwide_app)
  Standard reference for **RS485-capable** Balboa boards (BP series). Doesn't
  apply to GS501Z's J2 (different bus entirely).
- **ESPHome external component (RS485 path)** — [brianfeucht/esphome-balboa-spa](https://github.com/brianfeucht/esphome-balboa-spa)
  Most complete ESPHome component for Balboa **RS485** boards. Listed here so
  GS501Z users don't waste time trying to use it — it won't read this bus.
- **Maintained fork / RS485 power wiring** — [dhWasabi/M5Tough-BalboaSpa-esphome](https://github.com/dhWasabi/M5Tough-BalboaSpa-esphome)
- **Alternative MQTT gateway** — [NorthernMan54/esp32_balboa_spa](https://github.com/NorthernMan54/esp32_balboa_spa)
- **HA community thread** — https://community.home-assistant.io/t/balboa-hot-tub-spa-automation-and-power-savings/353032
- **GS501Z tech sheet** — https://www.balboawater.com/wp-content/uploads/2025/03/GS501Z.pdf

## Credits

- **kgstorm** — the GS100 + VL260 ESPHome decoder component that this work
  vendored and patched. Their bit-map analysis and decoder structure
  carried us most of the way.
- **Cameron Cutrer** ([@ccutrer](https://github.com/ccutrer)) — protocol RE
  for Balboa RS485 boards; useful context for the broader spa world even
  though GS501Z uses a different bus.
- **Brian Feucht** ([@brianfeucht](https://github.com/brianfeucht)) — RS485
  ESPHome component (not used here, but the natural starting point if you
  have a BP-series board).
- The **Home Assistant community** Balboa thread participants for
  real-world install notes and the photos that helped confirm cable
  colours.

## How this was built (AI disclosure)

Most of the code, docs, firmware configuration, and the protocol decode
writeups in this repo were written collaboratively with **Anthropic's
Claude (via Claude Code)**, driven by the repo owner doing all the
hands-on hardware work — multimeter checks, wiring, panel-button presses,
verification of every bit assertion against a real GS501Z + VL260.
Commits tagged `Co-Authored-By: Claude` are the literal product of those
pair-debugging sessions.

Treat anything in here as you would any AI-assisted work: **read it, test
it on the bench before pointing it at your own spa, verify any specific
decoder bit or pin assertion against your own hardware before trusting
it.** Spa hardware varies; even within the GS501Z line, panel variants
differ.

## Safety

- **5 V on the J2 bus is benign to the ESP32 through the 1 kΩ taps** —
  current is limited to ~3 mA worst case, well below any damage threshold.
  But never bridge pin 1 (+5 V) directly to pin 4 (GND); that's a board-side
  short.
- **Don't disturb the spa's mains side.** This firmware reads + writes the
  topside bus only. If you want price/solar-responsive switching of the
  whole tub, use an external contactor and the Shelly relay of your choice;
  in the UK that's Part P notifiable work (Building Regulations) — hire a
  competent person if you're not one. This repo deliberately stays out of
  that side.

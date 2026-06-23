# hot-tub-controller

Home Assistant + ESPHome integration for a **Balboa GS501Z** hot tub controller
(with a **VL260 topside panel**), with solar-surplus automation driven by a
Solis S6 hybrid inverter and a Shelly EM Gen3 contactor relay.

The ESP32 taps the Balboa board over its **J2 topside-display bus** (a
proprietary synchronous clock+data signalling format used by the panel — NOT
RS485, despite a lot of community speculation; see *Protocol* below). It
exposes water temperature, set temperature, heater, motor (circ/jets) and
blower state as native HA entities, plus four HA-callable buttons (Light,
Jets, Blower, Temp — physically presses the spa from HA) and a closed-loop
setpoint controller that walks the temp up/down to any target via the
ride-the-bounce algorithm. A Shelly EM Gen3 measures the hot tub's incoming
supply and switches a 40 A contactor that isolates the entire tub when there's
no surplus to burn. Decoder state is also streamed to BigQuery every 30 min
(`ktl-home-energy.meter_history.ha_hot_tub_30min`) for long-term cost-vs-state
analysis past HA's ~10-day recorder window.

**Status (2026-06-23): end-to-end working.** Production firmware deployed,
all four button entities tested from HA, closed-loop setpoint walk validated.
Full protocol decode in [`docs/HANDOFF.md`](docs/HANDOFF.md).

## Goals

1. **Burn solar surplus**, not gas-grid-priced electricity, into a 3 kW Incoloy
   heater element whenever the panels are exporting.
2. **Full HA integration** — temperature, setpoint, motor, blower, light,
   heater state, energy use — no Balboa cloud, no app, no subscription.
3. **Hard isolation** when not heating — the contactor cuts L+N to the tub so
   it's not idling at ~50 W keeping the board / pumps awake overnight.

## Protocol — short version

The GS501Z / VL260 combo doesn't speak RS485 on J2; it uses Balboa's
**topside-panel display bus** — a synchronous 24-bit clock+data frame format
that drives the panel's 7-segment digits and indicator LEDs. Reverse-engineered
in this repo on top of kgstorm's GS100 work (see Credits):

- **Display bus** (clock pin 6, data pin 5, GND pin 4): 24-bit frames at ~50/s,
  layout `7+7+7+3`. Fields: heater bit, water/set-temp digits, light, motor
  running, blower. Setpoint shows during "set-mode" — detected by alternating
  blank frames mixed with the value.
- **Button lines** (pins 2/3/7/8 = Light, Jets, Blower, Temp): **bidirectional,
  active-HIGH**. Idle at 0 V; a press shorts to +5 V (pin 1). The board echoes
  any panel-button press as a ~200 ms HIGH pulse on the matching line of every
  connected panel port — so a second tap point can both *write* presses (drive
  HIGH for ~80 ms) and *read* presses (catch the echo pulse).
- **Write path**: a plain ESP32 GPIO driving HIGH through a 1 kΩ series
  resistor is enough to register a press (≤3.3 mA into the board's input).
  **No optocoupler / relay / transistor needed**.

Full decode in [`docs/HANDOFF.md`](docs/HANDOFF.md). If you're working on
GS501Z, VL260, or a similar topside-panel board and want to do your own
mapping cycle, see the diagnostic firmware in `esphome/` (`rawcap.yaml`,
`button-sniffer.yaml`, `button-activity-sniffer.yaml`).

## Hardware

| Item | Part | Notes |
|---|---|---|
| Hot tub controller | Balboa GS501Z (PN 54511-01) | 3 kW Incoloy element, VL260 topside |
| MCU | ESP32 WROOM-32E dev board | Powered from USB or from J2 pin 1 (+5 V) |
| Bus tap | 7× 1 kΩ–2.2 kΩ resistors | Series on each tapped pin (data, clock, 4 button lines); current-limiting for both read and write |
| Connection | Balboa J2 RJ45 (spare panel port) | 568B cable: pin 1 +5 V, pin 4 GND, pin 5 data, pin 6 clock, pins 2/3/7/8 buttons |
| Energy monitoring | Shelly EM Gen3 + 50 A CT clamp | CT on hot tub incoming live |
| Switching | Schneider A9C20842 40 A contactor | Driven by Shelly EM dry contact |
| Solar | Solis S6 hybrid + Pylontech batteries | Existing MQTT/Modbus integration |

Note: an LM2596 buck converter, JZK RS485 transceiver, and PC817 optocoupler
all appeared in earlier hardware drafts; **none are needed**. The bus is 5 V
clock+data, the ESP32 is comfortably driven by USB or the J2 5 V rail, and the
write path is direct GPIO through the 1 kΩ. Simpler than expected.

## Architecture

```
              ┌──────────────┐         ┌──────────────────────┐
   Solis ────►│  Home        │◄────────│  Shelly EM Gen3      │
   MQTT       │  Assistant   │  MQTT   │  (CT + dry contact)  │
              │  (Proxmox)   │         └─────────┬────────────┘
              │              │                   │ 230 V
              │              │                   ▼
              │              │         ┌──────────────────────┐
              │   ESPHome    │         │  Schneider A9C20842  │
              │   API        │         │  40 A contactor      │
              │              │         └─────────┬────────────┘
              │              │                   │ 230 V switched
              │              │                   ▼
              │              │         ┌──────────────────────┐
              │              │◄────────│  ESP32 WROOM-32E     │
              │              │  API    │  (1 kΩ taps)         │
              └──────────────┘         └─────────┬────────────┘
                                                 │ J2 display bus
                                                 │ (clock + data
                                                 │  + 4 button lines)
                                                 ▼
                                       ┌──────────────────────┐
                                       │  Balboa GS501Z       │
                                       │  + 3 kW heater       │
                                       │  + VL260 topside     │
                                       └──────────────────────┘
```

## Quick start

1. **Wire it up** — see [docs/wiring.md](docs/wiring.md) and
   [docs/pinout.md](docs/pinout.md). Cable is 568B; pin 1 (white/orange) is
   +5 V; pin 4 (blue) is GND; pin 5 (white/blue) is data; pin 6 (green) is
   clock; pins 2/3/7/8 (orange / white-green / white-brown / brown) are the
   Light / Jets / Blower / Temp buttons. Each tapped pin goes through a
   1 kΩ–2.2 kΩ series resistor to its GPIO.
2. **Common ground first** — always tie pin 4 to ESP GND before anything else.
3. **Flash the production firmware** —
   `cp secrets.yaml.template esphome/secrets.yaml`, fill in WiFi / API / OTA
   values, then `esphome run esphome/hot-tub.yaml`. (For a read-only diagnostic
   build use `decoder.yaml` instead — same decoder, no button writes.) Verify
   it picks up water temperature within ~10 s.
4. **Add to HA** — the ESPHome integration auto-discovers the `hot-tub` device
   via mDNS; paste the `api_key` from `esphome/secrets.yaml` when prompted.
5. **Wire the contactor + Shelly** — see commissioning docs. Mains work is
   Part P notifiable in the UK; get a competent person.

### HA entities exposed by the production firmware
- **Sensors**: water temperature, set temperature (only valid during set-mode)
- **Binary sensors**: heater, pump (motor running), light, blower, plus
  WiFi/uptime diagnostics
- **Buttons (momentary)**: Press Light, Press Jets, Press Blower, Press Temp
  (each pulses the corresponding GPIO HIGH for 80 ms → one button press on
  the spa); Walk Setpoint to Target Now (runs the bounce controller)
- **Number**: Target Setpoint (26–40 °C, integer) — set this, then press the
  walk button to drive the temp button until the decoded setpoint matches
- **Text sensor**: Error Code (Balboa fault display)

Entity IDs use the `cabin_hot_tub_*` prefix in HA (Cabin-area assignment
disambiguates from the Shelly EM's `hot_tub_supply_*` energy entities).

## Repository layout

```
hot-tub-controller/
├── README.md
├── secrets.yaml.template
├── esphome/
│   ├── hot-tub.yaml                 # ★ Production firmware (decoder + 4 button
│   │                                #   writes + closed-loop setpoint walker)
│   ├── decoder.yaml                 # Read-only diagnostic build (no writes)
│   ├── components/inputs/           # Vendored kgstorm decoder + GS501Z patches
│   │                                #   (esp32-spa.h: bit map, stability filters)
│   ├── button-sniffer.yaml          # Diagnostic: passive button-line read
│   ├── button-activity-sniffer.yaml # Diagnostic: button-line state changes
│   │                                #   (caught the ~200ms board-echo pulses)
│   └── rawcap.yaml + rawcap.h       # Diagnostic: distinct-frame capture
├── home-assistant/
│   ├── automations/
│   │   ├── solar_surplus_on.yaml
│   │   └── solar_surplus_off.yaml
│   ├── scripts/
│   │   └── hot_tub_preheat.yaml
│   └── dashboards/
│       └── hot_tub.yaml             # Lovelace card
├── sim/
│   └── setpoint_sim.py              # Bounce-controller for the temp-button loop
│                                    #   (ported into hot-tub.yaml as a script)
└── docs/
    ├── HANDOFF.md                   # ★ Start here if picking this up cold
    ├── wiring.md
    ├── pinout.md
    └── commissioning.md
```

### Where the BigQuery ingest lives
The 30-min decoder state snapshot is written by the **kilnergy** repo
(`kilnergy/shadow/ha_rates_ingest.py`), not this one. It queries HA's REST API
for the entities listed above and appends a row to
`ktl-home-energy.meter_history.ha_hot_tub_30min`. Cron runs at `:01` and `:31`
on ktl-ci-runner via `~/ha_rates_cron.sh`.

## Community references

The protocol work here builds on, and significantly extends, the community
reverse-engineering effort for Balboa boards. The GS501Z / VL260 J2 bus turned
out to be NOT RS485 — it's the topside-display bus, which earlier RS485-focused
projects don't decode. The decoder component is forked from kgstorm's GS100 work
and patched for GS501Z bit-map and frame quirks.

- **kgstorm GS100 + VL260 component** — [kgstorm/Balboa-GS100-with-VL260-topside](https://github.com/kgstorm/Balboa-GS100-with-VL260-topside)
  The closest existing implementation for this panel family. Our decoder vendors
  this component and patches it for GS501Z specifics (different p4 bit-map,
  blower bit position, set-mode detection).
- **Canonical RS485-spa protocol RE** — [ccutrer/balboa_worldwide_app](https://github.com/ccutrer/balboa_worldwide_app)
  The standard reference for **RS485-capable** Balboa boards (BP series). Useful
  for context, but doesn't apply to GS501Z's J2 (which is the topside bus, not
  the BP RS485 bus).
- **ESPHome external component (RS485 path)** — [brianfeucht/esphome-balboa-spa](https://github.com/brianfeucht/esphome-balboa-spa)
  Most complete ESPHome component for Balboa **RS485** boards. Listed here so
  GS501Z users don't waste time trying to use it — it won't read this bus.
- **Maintained fork / RS485 power wiring** — [dhWasabi/M5Tough-BalboaSpa-esphome](https://github.com/dhWasabi/M5Tough-BalboaSpa-esphome)
- **Alternative MQTT gateway** — [NorthernMan54/esp32_balboa_spa](https://github.com/NorthernMan54/esp32_balboa_spa)
- **HA community thread** — https://community.home-assistant.io/t/balboa-hot-tub-spa-automation-and-power-savings/353032
- **GS501Z tech sheet** — https://www.balboawater.com/wp-content/uploads/2025/03/GS501Z.pdf

## Credits

- **kgstorm** — the GS100 + VL260 ESPHome decoder component that our work
  vendored and patched. Their bit-map analysis and decoder structure carried
  us most of the way.
- **Cameron Cutrer** ([@ccutrer](https://github.com/ccutrer)) — protocol RE for
  Balboa RS485 boards; useful context for the broader spa world even though
  GS501Z uses a different bus.
- **Brian Feucht** ([@brianfeucht](https://github.com/brianfeucht)) — RS485
  ESPHome component (not used here, but the natural starting point if you have
  a BP-series board).
- The **Home Assistant community** Balboa thread participants for real-world
  install notes and the photos that helped confirm cable colours.

## Safety

- **Mains voltage**: the contactor switching and Shelly EM install are on the
  hot tub's 230 V supply. In the UK this is **Part P notifiable work**
  (Building Regulations). If you are not a competent person, hire one. Get a
  certificate.
- **5 V on the J2 bus is benign to the ESP32 through the 1 kΩ taps** — current
  is limited to ~3 mA worst case. But never bridge pin 1 (+5 V) directly to
  pin 4 (GND); that's a board-side short.
- **RCBO**: hot tub final circuit must be RCBO-protected (typically Type A
  30 mA, B-curve). Verify before adding the contactor in series.
- **Isolation for service**: keep the existing rotary isolator. The contactor
  is *not* a substitute for a lockable isolator.

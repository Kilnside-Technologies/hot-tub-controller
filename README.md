# hot-tub-controller

Home Assistant + ESPHome integration for a **Balboa GS501Z** hot tub controller,
with solar-surplus automation driven by a Solis S6 hybrid inverter and a
Shelly EM Gen3 contactor relay.

The ESP32 talks to the Balboa board over RS485 (via the spare J2 RJ45 panel
port), exposing temperature / pump / jets / lights / heater as native HA
entities. A Shelly EM Gen3 measures the hot tub's incoming supply and switches
a 40 A contactor that isolates the entire tub when there's no surplus to burn.

## Goals

1. **Burn solar surplus**, not gas-grid-priced electricity, into a 3 kW Incoloy
   heater element whenever the panels are exporting.
2. **Full HA integration** — temperature, pumps, jets, lights, filter status,
   energy use — no Balboa cloud, no app, no subscription.
3. **Hard isolation** when not heating — the contactor cuts L+N to the tub so
   it's not idling at ~50 W keeping the board / pumps awake overnight.

## Hardware

| Item | Part | Notes |
|---|---|---|
| Hot tub controller | Balboa GS501Z (PN 54511-01) | 3 kW Incoloy element |
| MCU | ESP32 WROOM-32E dev board | |
| RS485 transceiver | JZK TTL→RS485 auto flow control module | 3.3 V / 5 V tolerant |
| Power | LM2596 buck converter | 12 V from J2 → **5 V** for ESP32 |
| Connection | Balboa J2 RJ45 (spare panel port) | 12 V, GND, RS485-A, RS485-B |
| Energy monitoring | Shelly EM Gen3 + 50 A CT clamp | CT on hot tub incoming live |
| Switching | Schneider A9C20842 40 A contactor | Driven by Shelly EM dry contact |
| Solar | Solis S6 hybrid + Pylontech batteries | Existing MQTT/Modbus integration |

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
              │              │  API    │  + RS485 transceiver │
              └──────────────┘         └─────────┬────────────┘
                                                 │ RS485 (J2)
                                                 ▼
                                       ┌──────────────────────┐
                                       │  Balboa GS501Z       │
                                       │  + 3 kW heater       │
                                       └──────────────────────┘
```

## Quick start

1. **Wire it up** — see [docs/wiring.md](docs/wiring.md) and
   [docs/pinout.md](docs/pinout.md).
   ⚠️ **Set the buck converter output to 5 V with a multimeter BEFORE
   connecting the ESP32.** A miswired LM2596 dumping 12 V into the ESP32 5 V
   pin will brick it.
2. **Commission the RS485 bus** — see [docs/commissioning.md](docs/commissioning.md).
   Verify data flow with a USB-RS485 dongle *before* flashing.
3. **Flash ESPHome** — `cp secrets.yaml.template esphome/secrets.yaml`, fill in
   values, then `esphome run esphome/hot-tub.yaml`.
4. **Add to HA** — entities should auto-discover via the ESPHome integration.
   Drop the YAML in `home-assistant/` into your HA config (or use packages).
5. **Enable automations** — review thresholds in
   `home-assistant/automations/solar_surplus_on.yaml` and switch them on.

## Repository layout

```
hot-tub-controller/
├── README.md
├── secrets.yaml.template
├── esphome/
│   └── hot-tub.yaml              # ESPHome device config
├── home-assistant/
│   ├── automations/
│   │   ├── solar_surplus_on.yaml
│   │   └── solar_surplus_off.yaml
│   ├── scripts/
│   │   └── hot_tub_preheat.yaml
│   └── dashboards/
│       └── hot_tub.yaml          # Lovelace card
└── docs/
    ├── wiring.md
    ├── pinout.md
    └── commissioning.md
```

## Community references

This project stands entirely on the shoulders of the community that
reverse-engineered the Balboa RS485 protocol. None of the protocol work here
is original; the value-add is the ESP32 + Shelly + solar-surplus integration.

- **ESPHome external component** — [brianfeucht/esphome-balboa-spa](https://github.com/brianfeucht/esphome-balboa-spa)
  Most complete ESPHome component for Balboa; supports GS-series boards.
  This repo's `esphome/hot-tub.yaml` consumes it directly.
- **Maintained fork / power-wiring reference** — [dhWasabi/M5Tough-BalboaSpa-esphome](https://github.com/dhWasabi/M5Tough-BalboaSpa-esphome)
  Good reference for the J2 power wiring (all 4 wires: 12 V, GND, A+, B-).
- **Canonical protocol reverse engineering** — [ccutrer/balboa_worldwide_app](https://github.com/ccutrer/balboa_worldwide_app)
  Everything else derives from this. Pinout / physical layer:
  https://github.com/ccutrer/balboa_worldwide_app/wiki#physical-layer
- **Alternative non-ESPHome MQTT gateway** — [NorthernMan54/esp32_balboa_spa](https://github.com/NorthernMan54/esp32_balboa_spa)
  Reference for MQTT topic structure if going non-ESPHome.
- **HA community thread** — https://community.home-assistant.io/t/balboa-hot-tub-spa-automation-and-power-savings/353032
  Real-world wiring photos, troubleshooting, GS-series-specific advice in later
  pages.
- **GS501Z tech sheet** — https://www.balboawater.com/wp-content/uploads/2025/03/GS501Z.pdf
  Official Balboa doc: DIP switch settings, connector locations, heater specs.

## Credits

- **Cameron Cutrer** ([@ccutrer](https://github.com/ccutrer)) — original
  protocol reverse engineering.
- **Brian Feucht** ([@brianfeucht](https://github.com/brianfeucht)) — the
  ESPHome external component this repo consumes.
- **@NorthernMan54** — alternative ESP32 MQTT gateway reference.
- **dhWasabi** — power wiring documentation.
- The **Home Assistant community** Balboa thread participants for the
  real-world install notes.

## Safety

- **Mains voltage**: the contactor switching and Shelly EM install are on the
  hot tub's 230 V supply. In the UK this is **Part P notifiable work**
  (Building Regulations). If you are not a competent person, hire one. Get a
  certificate.
- **Buck converter**: set output to 5 V with a multimeter on the bench, with
  the load disconnected, before wiring to the ESP32. Don't trust the trim pot
  factory setting.
- **RCBO**: hot tub final circuit must be RCBO-protected (typically Type A
  30 mA, B-curve). Verify before adding the contactor in series.
- **Isolation for service**: keep the existing rotary isolator. The contactor
  is *not* a substitute for a lockable isolator.

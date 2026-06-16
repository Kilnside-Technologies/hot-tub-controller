# Commissioning

Step-by-step from a complete kit on the bench to a working solar-surplus
automation. Don't skip the bench-side bus sniff in step 1 — it's the
cheapest way to catch a swapped A+/B− pair or a bad J2 cable.

---

## 1. Sniff the RS485 bus (before flashing)

**Goal**: confirm the Balboa is talking and you've identified the bus
correctly, with **no** ESP32 in the loop yet.

What you need:
- USB-RS485 dongle (FTDI / CH340-based, dirt cheap)
- PuTTY (Windows) or `screen` / `picocom` (Linux/macOS)
- The J2 patch lead you crimped (see [wiring.md](wiring.md))

Procedure:

1. Connect the USB-RS485 dongle's A+ to J2 pin 5 (white/blue), B− to J2
   pin 4 (blue). Leave the +12 V and GND lines disconnected on this end —
   the dongle is bus-powered from USB.
2. Plug the J2 patch lead into the Balboa GS501Z. Power up the tub (just
   the control board, not the heater).
3. On the PC, open the serial port at **115 200 8N1**. PuTTY → Serial,
   pick the COM port, baud 115200. Linux: `picocom -b 115200 /dev/ttyUSB0`.
4. You should see **constant bursts of binary garbage** on screen. That's
   Balboa frames. If you see nothing, swap A+ and B− and try again
   (the labels on cheap RS485 dongles lie).
5. Once you have a data stream, you've confirmed:
   - The Balboa board is alive and talking.
   - Your J2 patch lead carries the bus.
   - The A+ / B− orientation you'll wire to the JZK module.

Unplug everything.

---

## 2. Verify the data stream is sane (optional but recommended)

If you want to be sure the bus is Balboa frames and not noise, the
canonical sniffer is [ccutrer/balboa_worldwide_app](https://github.com/ccutrer/balboa_worldwide_app):

```bash
gem install balboa_worldwide_app
bwa_monitor /dev/ttyUSB0
```

This decodes the live frames into human-readable temps and status. If
`bwa_monitor` is happy, ESPHome will be too.

---

## 3. Flash the ESPHome firmware

1. Install ESPHome locally: `pip install esphome` or use the HA add-on.
2. `cp secrets.yaml.template esphome/secrets.yaml` and fill in real values.
   `esphome/secrets.yaml` is gitignored.
3. **First flash over USB** (not OTA — you have no IP yet):
   ```bash
   esphome run esphome/hot-tub.yaml --device /dev/ttyUSB0
   ```
4. Wait for the device to compile, upload, boot, and report:
   ```
   [I][app:102]: ESPHome version 20XX.X.X
   ...
   [I][api:138]: Boot is done
   ```
5. The device should auto-discover in HA under
   *Settings → Devices & services → ESPHome*. Click "Configure" and accept
   the API key from `secrets.yaml`.

---

## 4. Verify entities

In Home Assistant, the following entities should now exist:

| Entity                                    | Expected state           |
|-------------------------------------------|--------------------------|
| `sensor.hot_tub_water_temperature`        | a sensible °C value      |
| `climate.hot_tub_climate`                 | current + target temp    |
| `binary_sensor.hot_tub_heater_active`     | on/off                   |
| `binary_sensor.hot_tub_filter_cycle`      | on/off                   |
| `switch.hot_tub_pump1`                    | on/off, toggleable       |
| `switch.hot_tub_jets1`                    | on/off, toggleable       |
| `switch.hot_tub_lights`                   | on/off, toggleable       |
| `sensor.hot_tub_wifi_signal`              | dBm                      |
| `sensor.hot_tub_uptime`                   | seconds                  |

**Cross-check the temperature** against the Balboa topside panel. If the
ESPHome value is in °F when the panel is °C (or vice versa), check
`spa_temp_scale` in `esphome/hot-tub.yaml` — for UK installs it must be `C`.

Toggle a pump from HA and verify it spins up on the tub. If the switch
flips back to "off" immediately, the Balboa rejected the command — most
common cause is a bad RS485 wire (works for RX but not TX). Re-seat A+ /
B− or shorten the lead.

---

## 5. Test the contactor manually

(Skip until the mains wiring in [wiring.md](wiring.md) step 2 is done and
certified.)

1. Make sure the tub is full and primed.
2. In HA, toggle `switch.hot_tub_contactor` ON. Listen for the
   contactor "clack". Confirm tub powers up.
3. Toggle OFF. Confirm tub powers down.
4. Watch `sensor.hot_tub_em_power` — should read ~0 W with the contactor
   open, and rise to ~50 W idle / ~3050 W when the heater is firing.

---

## 6. Verify Shelly EM Gen3 readings

1. Turn the contactor on, turn on a pump (high draw of a few hundred watts).
2. `sensor.hot_tub_em_power` should track the change within a second.
3. Disable the pump, enable the heater (climate target > current).
   Power should jump to ~3050 W. If it reads ~−3050 W, the CT clamp is
   wired backwards — flip it on the conductor.

---

## 7. Enable the solar-surplus automations

1. Open `home-assistant/automations/solar_surplus_on.yaml` and
   `solar_surplus_off.yaml`.
2. Confirm all entity IDs match your install. The placeholders are:
   - `sensor.solis_grid_exported_power`
   - `switch.hot_tub_contactor`
   - `climate.hot_tub_climate`
   - `sensor.hot_tub_water_temperature`
3. Tune thresholds for your setup:
   - The 2 kW / 5 min ON threshold matches a system that can comfortably
     sustain the 3 kW heater after a 1 kW base load. Smaller arrays should
     drop this.
   - The 500 W / 10 min OFF threshold prevents cloud-edge chatter.
4. Reload automations: *Developer Tools → YAML → Automations*.
5. Watch the automation traces over the next sunny day.

---

## 8. Day-two operations

- **Telnet logger**: while the device is in the cabinet and you're
  debugging, uncomment the `stream_server` block in `esphome/hot-tub.yaml`
  and reflash OTA. Then `nc <device-ip> 23` from indoors to stream logs.
  Comment it back out once stable — it pins the UART and consumes RAM.
- **OTA updates**: once the device has an IP, all future updates are
  `esphome run esphome/hot-tub.yaml` (no `--device` flag).
- **MQTT consumers**: topics are published under `home/hot_tub/...`.
  If you wire up an external dashboard, point it at that prefix.

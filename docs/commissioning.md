# Commissioning

Step-by-step from a complete kit on the bench to a working solar-surplus
automation. Don't skip the bench-side bus sniff in step 1 — it's the cheapest
way to catch a swapped DATA/CLOCK pair or a bad J2 cable before you commit.

---

## 1. Sniff the display bus (before the production flash)

**Goal**: confirm the J2 tap is correct and the decoder reads sane values,
*before* committing the full firmware. The J2 is the **topside display bus**, not
RS485 — so there's no RS485 dongle here; you flash a bench decoder and read it.

What you need:
- The ESP32 wired to J2 per [wiring.md](wiring.md) (read dividers on GPIO34/35,
  common GND), powered over **USB** on the bench.
- A USB cable to the ESP32.

Procedure:

1. With the ESP32 tapped onto a powered Balboa (control board only, not the
   heater), flash the USB-only bench decoder over USB:
   ```bash
   esphome run esphome/decoder-usb.yaml --device /dev/ttyUSB0
   ```
   `decoder-usb.yaml` has no WiFi/secrets — pure serial, made for exactly this.
2. Watch the logs. Within a few seconds you should see decoded frames and a
   plausible **water temperature** that matches the topside panel.
3. If you see nothing or garbage:
   - Swap **DATA (GPIO34) ↔ CLOCK (GPIO35)** — the classic green/white-green
     mislabel (see [pinout.md](pinout.md)). Clock is **green** (pin 6).
   - Check the common **GND** (pin 4) is connected.
   - Check the 1k/2.2k dividers.
4. Once the decoder shows correct temperature, the tap is proven. Unplug.

---

## 2. Deeper frame inspection (optional)

If decode is flaky and you want to see the raw bus, this repo ships bench tools:

- `esphome/sniffer.yaml` — streams display-bus activity over WiFi/web.
- `esphome/rawcap.yaml` — one-flash raw capture (edge rate + frames + raw bits),
  useful when chasing clock/data framing.

Flash one of those instead of `decoder-usb.yaml` to inspect timing and raw
frames. If the decoder reads correct temperature, you don't need this.

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
   Then watch for the DS18B20 discovery lines and copy each 64-bit address into
   the matching `address:` field in `hot-tub.yaml` (see the comments there) so
   the probes bind stably across reboots.
5. The device should auto-discover in HA under
   *Settings → Devices & services → ESPHome*. Click "Configure" and accept
   the API key from `secrets.yaml`. Device name is `hot-tub` → entities `hot_tub_*`.

---

## 4. Verify entities

In Home Assistant, the following entities should now exist:

| Entity | Expected state |
|--------|----------------|
| `sensor.hot_tub_water_temperature` | a sensible °C value (decoded off the panel) |
| `sensor.hot_tub_set_temperature` | current setpoint (populates after first Temp press / boot capture) |
| `number.hot_tub_target_setpoint` | 26–40, your target |
| `binary_sensor.hot_tub_heater` | on/off |
| `binary_sensor.hot_tub_pump_motor_running` | on/off |
| `binary_sensor.hot_tub_light` | on/off |
| `binary_sensor.hot_tub_blower` | on/off |
| `binary_sensor.hot_tub_lid_open` | on (open) / off (closed) |
| `sensor.hot_tub_water_temperature_probe` | DS18B20 bulk water temp |
| `sensor.hot_tub_ambient_temperature` | **disabled** until a 2nd DS18B20 is wired |
| `sensor.hot_tub_cabinet_temperature` / `_cabinet_humidity` | DHT11 |
| `sensor.hot_tub_error_code` | fault text (blank when healthy) |
| `button.hot_tub_press_light` / `_jets` / `_blower` / `_temp` | momentary |
| `button.hot_tub_walk_setpoint_to_target_now` | momentary |
| `sensor.hot_tub_wifi_signal` / `_uptime` | diagnostics |

**Cross-check the temperature** against the Balboa topside panel — the decoder
reads the panel's own 7-seg digits, so it should match exactly. If they differ,
the decode is wrong (re-check DATA/CLOCK, see step 1), not a unit-scale setting.
The displayed unit is just the `UNITS` substitution label in `hot-tub.yaml`.

**Test a button:** press `button.hot_tub_press_jets` in HA and confirm the jets
react on the tub (and the physical panel still works afterwards). If the panel
goes unresponsive, the button pins aren't returning to high-impedance idle —
see the button-write notes in [pinout.md](pinout.md).

**Test the setpoint walk:** set `number.hot_tub_target_setpoint`, press
`button.hot_tub_walk_setpoint_to_target_now`, and watch `sensor.hot_tub_set_temperature`
converge to the target over the next few presses.

---

> 🚧 **Steps 5–7 are PROPOSED / NOT INSTALLED.** They cover the Shelly contactor +
> Shelly EM + solar-surplus automations, which are not fitted and reference
> entities that don't exist yet (`switch.hot_tub_contactor`, `sensor.hot_tub_em_power`).
> The solar-surplus heater-control use case hasn't been firmed — it may be doable
> with the display-bus setpoint control alone. Skip 5–7 unless/until that's decided
> and the hardware is in.

## 5. Test the contactor manually (proposed)

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
3. Disable the pump, then raise the setpoint above current water temp (set
   `number.hot_tub_target_setpoint` and press the walk button) so the heater
   fires. Power should jump to ~3050 W. If it reads ~−3050 W, the CT clamp is
   wired backwards — flip it on the conductor.

---

## 7. Enable the solar-surplus automations

1. Open `home-assistant/automations/solar_surplus_on.yaml` and
   `solar_surplus_off.yaml`.
2. Confirm all entity IDs match your install. The placeholders are:
   - `sensor.solis_grid_exported_power`
   - `switch.hot_tub_contactor`
   - `number.hot_tub_target_setpoint`
   - `button.hot_tub_walk_setpoint_to_target_now`
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

- **Live logs / readout**: the device runs a web server — open
  `http://hot-tub.local/` from the LAN for entity state, or stream logs with
  `esphome logs esphome/hot-tub.yaml` over the network.
- **OTA updates**: once the device has an IP, all future updates are
  `esphome run esphome/hot-tub.yaml` (no `--device` flag). Note that OTA must
  come from a host that can reach the device's VLAN.
- **Setpoint after boot**: the firmware fires one Temp press ~45 s after boot to
  reveal the current setpoint (otherwise `set_temperature` stays unknown until
  someone interacts), and refreshes it every 30 min.

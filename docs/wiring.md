# Wiring

Two distinct wiring jobs:

1. **Low voltage** — ESP32 tapped onto the Balboa J2 topside-display bus and
   powered from the J2 +5 V. Reversible, safe to experiment with.
2. **Mains** — Shelly EM Gen3 + 40 A contactor inline with the tub's incoming
   230 V supply. **Part P notifiable in the UK** — get a competent person.

Pin assignments (J2 RJ45, ESP32 GPIO) live in [pinout.md](pinout.md); this file
is the install procedure.

---

## 1. Low voltage — ESP32 on the J2 display bus

> The J2 RJ45 is the **topside display bus**, not RS485. There is **no buck
> converter, no RS485 transceiver, and no optocoupler** — the earlier design
> that used all three was the wrong-protocol theory (see `git log`). The ESP32
> taps the bus directly through resistors and is powered from J2's own +5 V.

### Parts laid out

```
   Balboa GS501Z
   ┌───────────┐
   │   J2 ─────┼─ RJ45 patch lead (568B) ─ 6 wires used:
   └───────────┘
        pin 1  +5 V  ───────────────────────────► ESP32 5V/VIN
        pin 4  GND   ───────────────────────────► ESP32 GND + divider GND
        pin 5  DATA  ─[1k]─┬───────────────────► GPIO34   (2.2k node→GND)
        pin 6  CLOCK ─[1k]─┬───────────────────► GPIO35   (2.2k node→GND)
        pin 2  Light ─[1k]──────────────────────► GPIO33
        pin 3  Jets  ─[1k]──────────────────────► GPIO4
        pin 7  Blower─[1k]──────────────────────► GPIO16
        pin 8  Temp  ─[1k]──────────────────────► GPIO17

                 ┌──────────────┐  + DS18B20 ×1 water → GPIO32 (4.7k pull-up to 3V3)
                 │   ESP32       │  + DHT11           → GPIO13
                 │  WROOM-32E    │  + reed (lid)      → GPIO14 ↔ GND
                 │  +VIN decoupl │  + 2×1000µF + 2×100nF at VIN (brownout, see step 5)
                 └──────────────┘
```

### Step by step

1. **Crimp / strip the J2 patch lead.** Cut one end off a Cat5e patch lead.
   Strip back ~50 mm and identify the wires by 568B colour (see
   [pinout.md](pinout.md)). Hold the plug gold-contacts-toward-you, cable down:
   pin 1 is far left.
2. **Build the two read dividers.** For DATA (pin 5) and CLOCK (pin 6): a 1 kΩ
   in series from the J2 core to the GPIO node, plus a 2.2 kΩ from that node to
   GND. This drops the bus's 5 V swing to ~3.4 V — safe for the ESP32 inputs.
   - pin 5 (white/blue) → 1k → **GPIO34**, with 2.2k node→GND
   - pin 6 (green) → 1k → **GPIO35**, with 2.2k node→GND
3. **Wire the four button taps.** Each through a single 1 kΩ series resistor
   straight to its GPIO (no divider, no opto — see pinout.md for why):
   - pin 2 (orange) → 1k → **GPIO33** (Light)
   - pin 3 (white/green) → 1k → **GPIO4** (Jets)
   - pin 7 (white/brown) → 1k → **GPIO16** (Blower)
   - pin 8 (brown) → 1k → **GPIO17** (Temp)
4. **Wire ground:** pin 4 (blue) → ESP32 GND, and tie the divider GND rail to
   the same point. A solid common ground is mandatory.
5. **Power + decoupling.** On the bench, power over **USB** (flash + logs). In
   the cabinet the ESP is **parasitically powered from J2 pin 1 (+5 V)** → ESP
   **5V/VIN**. ⚠️ The spa's internal 5 V SMPS is sized for the panel
   (~50–100 mA) and is overloaded by the ESP's WiFi spikes (200–500 mA) →
   boot brownouts at the 2.43 V detector. The firmware already trims WiFi
   current (`output_power: 14dB`, `fast_connect`, `power_save_mode: light`);
   on the hardware side **fit decoupling caps at VIN**:
   - **2× 1000 µF** low-ESR electrolytic + **2× 100 nF** X7R ceramic, split
     across two points on the VIN/GND rail (≤25 mm leads, each 100 nF right
     beside its 1000 µF). Optional **1 Ω 0.5 W** inrush limiter in series from
     J2 +5 V *only if* the ESP won't start with the caps fitted. Every cap
     ground shares the **same node as J2 pin 4**.
     ```
     J2 +5V ──[opt 1Ω]──┬───────────┬── ESP VIN
                       1000µF      1000µF
                       +100nF      +100nF
     J2 GND ────────────┴───────────┴── ESP GND   (= J2 pin 4 node)
     ```
   - **Long-term fix** (see `NOTES.md`): drop J2 pin 1, run a small dedicated
     5 V PSU, keep J2 pin 4 GND tied as the common signal reference.
   **Never have USB and the J2 +5 V connected at the same time** (back-feed).
6. **Sensors (in the firmware, wire if fitted):**
   - DS18B20 water probe: data → **GPIO32** with a 4.7 kΩ pull-up to 3V3;
     VCC → 3V3, GND → GND. (The firmware's *ambient* DS18B20 is currently
     **disabled** — only one probe is wired; otherwise both entries bind to the
     same sensor. Re-enable + pin both addresses once probe #2 is on the bus.)
   - DHT11 (cabinet): data → **GPIO13**, VCC → 3V3, GND → GND.
   - Reed lid switch: one side → **GPIO14**, other → GND (internal pull-up in
     software; magnet on the lid, reed body on the cabinet rim).
7. **Confirm before plugging into J2.** Sanity-check the bus with the bench
   decoder first — see [commissioning.md](commissioning.md) step 1.

### Mounting

The ESP32 wants to live somewhere dry and warm-but-not-hot inside the cabinet.
Common pattern:

- 3D-printed box on a rail or velcro'd to a cabinet wall.
- Cable gland or RJ45 keystone for the J2 lead.
- Keep the antenna **clear of the equipment plate** — these are metal and kill
  2.4 GHz. Measured in this cabinet: a bare ESP32-C3 Super Mini read ~−80 dBm
  (marginal, drops), a WROOM-32E ~−62 dBm (solid) — use the WROOM, and if a
  metal enclosure still hurts it, a pigtail to a small external SMA antenna on
  the cabinet panel works well.

---

## 2. Mains — Shelly EM Gen3 + contactor (PROPOSED — not installed)

> 🚧 **PROPOSED / NOT INSTALLED.** No Shelly or contactor is fitted. The
> solar-surplus heater-shedding use case hasn't been firmed up — it may be
> achievable with the display-bus setpoint control alone (no contactor at all).
> Everything below is a design reference, not a current install.
>
> ⚠️ **UK Part P notifiable work.** If you are not a competent person under
> BS 7671, hire one. Have the install certified.

### What goes where

```
   ┌──────────┐    ┌──────────┐    ┌──────────────────┐    ┌──────────┐    ┌──────────┐
   │ Consumer │ L  │ Existing │ L  │  Schneider       │ L  │ Existing │ L  │  Hot     │
   │ unit     ├───►│ RCBO     ├───►│  A9C20842        ├───►│ rotary   ├───►│  tub     │
   │ (busbar) │    │ (Type A, │    │  40 A contactor  │    │ isolator │    │          │
   │          │ N  │  30 mA)  │ N  │  (main contacts) │ N  │          │ N  │          │
   │          ├───►│          ├───►│                  ├───►│          ├───►│          │
   └──────────┘    └──────────┘    └──────────────────┘    └──────────┘    └──────────┘
                                          ▲    ▲
                                          │A1  │A2  (coil — 230 V)
                                          │    │
                                   ┌──────┴────┴──────┐
                                   │ Shelly EM Gen3    │   CT clamp on incoming L
                                   │ dry-contact OUT   │── (between RCBO and contactor,
                                   │ + L/N for self-   │     or anywhere downstream of RCBO)
                                   │ power             │
                                   └───────────────────┘
```

### Step by step (for the competent person doing the install)

1. **Isolate** the tub circuit at the consumer unit. Lock off. Prove dead.
2. **Mount the Schneider A9C20842** on the DIN rail next to the RCBO. It's a
   2-pole device; both L and N pass through the main contacts.
3. **Wire main contacts** in series with the tub's L and N feed, downstream
   of the RCBO, upstream of the existing rotary isolator. Leave the isolator
   in place — the contactor is *not* a service isolator.
4. **Mount the Shelly EM Gen3.** Source power from L+N downstream of the
   RCBO (same side as the contactor coil supply).
5. **Wire the CT clamp** around the incoming **live** conductor to the tub
   (downstream of the contactor). Observe the arrow direction on the CT —
   reversed CTs report negative power.
6. **Wire the Shelly dry-contact output** to the contactor coil:
   - Contactor A1 → Shelly output common
   - Contactor A2 → Shelly output NO (normally open)
   - Coil supply: 230 V L and N from the same RCBO feed (Schneider coil is
     230 V AC on this part).
   - CONFIRM the contactor coil voltage on the actual part — A9C20842 is
     sold in multiple coil voltages. The dry contact must be rated for the
     coil's inrush.
7. **Energise, test isolation**: with the Shelly relay OFF the tub must be
   completely dead at the tub's input. With the Shelly relay ON, normal
   power.
8. **Test the RCBO trip** with the test button after install. Don't
   short live to earth to test it.
9. **Certify**.

### Notes & gotchas

- The contactor's coil current is small (a few VA) but the inrush can spike;
  the Shelly Gen3 dry contact handles this fine, but don't substitute a
  bare-bones 5 A relay module.
- If the existing tub final circuit is a single Type AC RCBO, **replace it
  with Type A** before commissioning — modern hot tub VFDs / inverter pumps
  produce DC residual currents that Type AC won't detect, defeating the
  RCBO. This is now mandatory for new installs under BS 7671 Amd 2.
- The contactor goes through ~30,000 operations under solar-surplus on/off
  cycling per year if you're not careful. A 40 A AC1 contactor is rated for
  millions of cycles at this duty, but consider tightening the hysteresis
  in `solar_surplus_off.yaml` if you see it chattering.

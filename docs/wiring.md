# Wiring

Two distinct wiring jobs:

1. **Low voltage** — ESP32 + RS485 + buck converter, all powered from the
   Balboa J2 RJ45 panel port. Reversible, safe to experiment with.
2. **Mains** — Shelly EM Gen3 + 40 A contactor inline with the tub's incoming
   230 V supply. **Part P notifiable in the UK** — get a competent person.

Pin assignments (J2, ESP32 GPIO, JZK module) live in [pinout.md](pinout.md);
this file is the install procedure.

---

## 1. Low voltage — ESP32 + RS485

### Parts laid out

```
   Balboa GS501Z
   ┌───────────┐
   │   J2 ─────┼─── RJ45 patch lead ─── (4 wires used: 12 V, GND, A+, B−)
   └───────────┘                                │
                                                ▼
                                        ┌──────────────┐      ┌──────────┐
                                        │ LM2596 buck  │      │   JZK    │
                                  12 V─►│ IN  →  OUT   │─5 V─►│  RS485   │
                                  GND──►│              │─GND─►│ module   │
                                        └──────────────┘      └────┬─────┘
                                                                   │ TTL: TXD/RXD
                                                                   ▼
                                                            ┌──────────────┐
                                                            │   ESP32      │
                                                            │  WROOM-32E   │
                                                            └──────────────┘
```

### Step by step

1. **Crimp / strip the J2 patch lead.** Cut one end off a Cat5e patch lead.
   Strip back ~50 mm and identify the four pairs by 568B colour
   (see [pinout.md](pinout.md)). You need:
   - Pin 3 or 7 (white/green or white/brown) — **+12 V**
   - Any of pins 1/2/6/8 — **GND**
   - Pin 5 (white/blue) — **RS485 A+**
   - Pin 4 (blue) — **RS485 B−**
2. **Bench-trim the LM2596 to 5.0 V** with a 12 V supply and a multimeter
   on OUT+. Lock the trim pot with a dab of nail varnish once set. **Do not
   skip this step** — factory trim pots have been observed at >20 V.
3. **Wire the buck**: J2 +12 V → LM2596 IN+, J2 GND → LM2596 IN−.
4. **Wire the ESP32 power**: LM2596 OUT+ → ESP32 5V, LM2596 OUT− → ESP32 GND.
   Power on with the multimeter still on the 5 V rail — confirm the ESP32
   boot lights come on and the rail stays at 5 V (no sag).
5. **Wire the JZK module**:
   - VCC → ESP32 3V3
   - GND → ESP32 GND
   - TXD → ESP32 GPIO16 (RX)
   - RXD → ESP32 GPIO17 (TX)
   - A / A+ → J2 RS485 A+ (pin 5)
   - B / B− → J2 RS485 B− (pin 4)
6. **Confirm orientation** before plugging into J2. The RJ45 lead from the
   Balboa must be the correct way round — see commissioning.md step 1 for
   the bus-sniff sanity check.

### Mounting

The ESP32 + buck + JZK want to live somewhere dry and warm-but-not-hot
inside the cabinet. Common pattern:

- 3D-printed box on a rail or velcro'd to a cabinet wall.
- Cable gland or RJ45 keystone for the J2 lead.
- Keep the antenna **clear of the equipment plate** — these are metal and
  kill 2.4 GHz. A pigtail to a small external SMA antenna on the cabinet
  panel works well.

---

## 2. Mains — Shelly EM Gen3 + contactor

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

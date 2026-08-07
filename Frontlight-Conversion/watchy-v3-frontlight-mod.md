# Watchy v3 — Frontlight Mod

Replacing the GDEY0154D67 with a GDEY0154D67-FL04, with MOSFET-driven frontlight.

Status: all datasheet values verified against GDEY0154D67-FL04-Specification
(rev. 2023-07-20) and Watchy `src/config.h` (lib 1.4.14).

---

## 1. Starting point

Watchy v3 already uses the **GDEY0154D67**. The FL04 is the same panel with a bonded
light guide — SSD1681, 200×200, 24-pin FPC at 0.5 mm pitch, same active area of
27.00 × 27.00 mm.

**Nothing changes on the display software side.** `GxEPD2_154_D67` stays as it is, and so
does the display connector. All of the work is in the frontlight.

Availability: buy-lcd.com, USD 8.80, last seen as "0 in stock" with a notify option.

---

## 2. Electrical design

### 2.1 Frontlight characteristics

| Parameter | Value | Source |
|---|---|---|
| LED dies | 3, in parallel | circuit diagram |
| Forward voltage | 2.9 V typ. at IF = 60 mA | circuit diagram |
| Operating voltage | 2.8–3.3 V | spec table |
| Operating current | ≤ 45 mA | spec table |
| FL connector | 6-pin FPC, 0.5 mm pitch | spec table |
| FPC thickness | 0.30 ± 0.05 mm with stiffener | mechanical drawing |
| Pad width | 0.35 mm | mechanical drawing |
| Contact side | top (display viewing side) | confirmed |

The 60 mA in the circuit diagram is the test condition for the Vf figure, not an
operating recommendation. **Design limit is the 45 mA from the spec table.**

### 2.2 FL FPC pinout

| Pin | Symbol | Function | Net |
|---|---|---|---|
| 1 | A1 | anode | `3V3` |
| 2 | A2 | anode | `3V3` |
| 3 | NC | — | leave open |
| 4 | NC | — | leave open |
| 5 | K1 | cathode | `LED_K` |
| 6 | K2 | cathode | `LED_K` |

Both anode and cathode are brought out twice (current sharing across the FPC traces).
Bridge pins 1+2 and 5+6 on the adapter board — roughly 22 mA per trace.

There are no separate cathodes, so **one** shared series resistor it is.

### 2.3 Circuit topology

Low-side switch. The MOSFET sits in the return path, so no gate driver is needed.

```
3V3 (LDO) ──┬── J1.1 ──┐
            └── J1.2 ──┤ 3× LED in parallel
                       │
            ┌── J1.5 ──┤
            └── J1.6 ──┘
            │
           R1 (22 Ω)
            │
         Q1.3 (drain)
       ┌────┴────┐
GPIO44 ─ R2 ─ Q1.1 (gate)
            │        │
           R3      Q1.2 (source)
        (4.7 kΩ)     │
            └────────┴──── GND
```

### 2.4 Netlist

| Net | Connections |
|---|---|
| `3V3` | Watchy TP4 · J1 pin 1 · J1 pin 2 |
| `LED_K` | J1 pin 5 · J1 pin 6 · R1-A |
| `SW` | R1-B · Q1 pin 3 (drain) |
| `GATE` | Q1 pin 1 · R2-B · R3-A |
| `GND` | Watchy TP5 · Q1 pin 2 (source) · R3-B |
| `PWM` | Watchy TP2 (GPIO44) · R2-A |

Three wires run to the mainboard: `3V3`, `GND`, `PWM`.

### 2.5 Component values

**Q1 — SI2302 or AO3400A, SOT-23**

| Pin | Function | Position (marking readable, pins at bottom) |
|---|---|---|
| 1 | gate | bottom left |
| 2 | source | bottom right |
| 3 | drain | top, single pin |

Logic-level is mandatory, Rds(on) below 100 mΩ at Vgs = 2.5 V. Not a 2N7002 — its
5–7 Ω would eat 0.3 V of an already tight voltage budget.

**R1 — series resistor, 0603**

Start at **22 Ω**. Rough guide (modelled Vf, must be measured):

| R1 | Approx. current | Character |
|---|---|---|
| 68 Ω | 10 mA | very subtle |
| 39 Ω | 15 mA | |
| 27 Ω | 20 mA | |
| 22 Ω | 25 mA | likely sweet spot |
| 15 Ω | 30 mA | |
| 10 Ω | 45 mA | datasheet limit, do not go below |

Dissipation at 45 mA: 20 mW. A 0603 (100 mW) has plenty of margin.

**R2 — 100 Ω, 0402/0603.** Gate series resistor, damps switching edges. Optional.

**R3 — 4.7 kΩ, 0402/0603.** Gate pulldown. **Mandatory.**

The value is deliberately low: GPIO44 (U0RXD) has an internal pull-up of roughly 45 kΩ
at reset. A 100 kΩ pulldown would hold the gate at 2.3 V and turn the MOSFET on. With
4.7 kΩ the gate sits at 0.31 V, safely below threshold.

Cost: an extra 0.7 mA while the light is on. Zero in deep sleep.

**J1 — FPC ZIF, 6-pin, 0.5 mm pitch, top contact, for 0.3 mm FPC.**

### 2.6 Why measurement is unavoidable

With a 3.3 V rail and 2.9 V forward voltage, only about 0.4 V is left for R1. Two effects
hit that budget hard:

- **LDO tolerance:** the RT9080-33 delivers 3.3 V ± 2 %, so 3.23–3.37 V. That alone
  swings the current by ±20 %.
- **Thermal drift:** Vf falls as the LEDs warm up, current rises, more heat. The small
  series resistor ballasts this only weakly.

**Consequence:** determine R1 on the assembled board, not on a breadboard. And cap the
duty cycle in firmware at 80 % rather than running 100 % — it costs almost no brightness
and takes the thermal effect out of the equation.

### 2.7 Power budget

Battery: 200 mAh. Assuming 25 mA frontlight current.

| Usage | Consumption |
|---|---|
| Continuous | 8 h to empty |
| 20× 3 s per day | 0.42 mAh/day — negligible |
| 20 min reading session | 8.3 mAh = 4.2 % of the battery |

The RT9080-33 handles 600 mA; an extra 25–45 mA alongside the WiFi peaks is uncritical.

---

## 3. Mechanical

### 3.1 Stack-up (total thickness 1.85 mm)

| Layer | Thickness |
|---|---|
| AG film | 0.175 mm |
| LGP (light guide plate) | 0.425 mm |
| EDP (panel) | 1.00 mm |
| DCA (adhesive) | 0.25 mm |

Compared to the standard panel (1.00 mm) that is **+0.85 mm**.

### 3.2 Footprint

| Layer | Dimensions |
|---|---|
| EDP | 37.32 × 31.80 mm |
| LGP | 39.60 × 30.40 mm |
| AG film / DCA | 30.16 mm |
| Active area | 27.00 × 27.00 mm |

The light guide is **2.28 mm longer but 1.40 mm narrower** than the panel. So width-wise
there is more room than before.

### 3.3 Case modification

Two changes to the front bezel:

1. **+0.85 mm depth** across the whole panel area.
2. **Shallow pocket for the LGP overhang:** 2.3 mm long, roughly 0.6 mm deep
   (LGP + AG film). No need to remove material through the full build height.

Base STLs are available from sqfmi. Budget two to three print iterations. **Check fit
with the old display**, not the new one.

### 3.4 Adapter board

12 × 10 mm, two-layer, HASL. Populated with J1, Q1, R1, R2, R3 plus three solder pads
for the wires to the mainboard. Under EUR 5 at JLCPCB including minimum order.

Alternative without a PCB: solder the components directly to the legs of the FPC
connector (dead-bug) and wrap in Kapton. Works, but mechanically riskier inside a watch
case.

Connection to the mainboard: 0.1 mm enamelled copper wire, strain relief with Kapton.

---

## 4. Connection points on the Watchy v3

All three connections sit on labelled test pads. No soldering to chip pins required.

| Signal | Test point | Net |
|---|---|---|
| `3V3` | **TP4** | 3V3 |
| `GND` | **TP5** | GND |
| `PWM` | **TP2** | RX (GPIO44) |

### 4.1 All v3 test points

| TP | Net | Pin |
|---|---|---|
| TP1 | TX | GPIO43 |
| TP2 | RX | GPIO44 |
| TP3 | BOOT | GPIO0 (= BTN3) |
| TP4 | 3V3 | — |
| TP5 | GND | — |
| TP6 | SCL | GPIO11 |
| TP7 | SDA | GPIO12 |

Source: `sqfmi/watchy-hardware`, `WatchySchematic.pdf` (branch v3.0).

### 4.2 Why not GPIO5 or GPIO18

In the v3 schematic the following ESP32-S3 pins carry **no net label** and are therefore
unconnected — no trace exists that could be soldered to:

```
GPIO1, 2, 3, 4, 5, 18, 37, 38, 39, 40, 41, 42, 45, 46
```

Also unavailable: 15/16 (32 kHz crystal), 19/20 (native USB), 26–32 (flash).

### 4.3 Why TP2 (RX) rather than TP1 (TX)

GPIO43 (U0TXD) is driven push-pull by the ROM bootloader at reset, idling high. The
frontlight would flash briefly on every wake, and no pulldown can prevent that.

GPIO44 (U0RXD) is an input with an internal pull-up at reset — manageable via the
4.7 kΩ pulldown from section 2.5.

**Cost:** UART0 is taken afterwards. Not a problem on v3, since programming and serial
output run over native USB CDC.

**Limitation:** GPIO44 is outside the RTC domain (0–21 on the S3), so `gpio_hold_en()`
does not work there — use `gpio_deep_sleep_hold_en()` instead.

### 4.4 Check before soldering

The schematic proves the test points *exist*. Whether they are accessible as exposed
pads on the finished board (not under the battery, not covered by solder mask) is
something a look at your own watch will answer. Test point symbols almost always become
open pads, but checking costs two minutes.

### 4.5 Correction to the parts list

The v3 uses an **RT9080-33** LDO, not the ME6211C33 — the latter is in the BOM on
watchy.sqfmi.com and applies to v1/v2. Thanks to TP4 this makes no difference.

The v3 already carries an **AO3400A** (Q3, EPD boost section), so the MOSFET
recommendation matches SQFMI's own choice of part.

---

## 5. Bill of materials

### 5.1 Electronics

| Ref | Part | Specification | Qty |
|---|---|---|---|
| — | Display | GDEY0154D67-FL04 | 1 |
| Q1 | MOSFET | SI2302 or AO3400A, SOT-23, logic-level | 2 |
| R1 | Resistor | assortment 10/15/22/27/39/68 Ω, 0603 | 5 each |
| R2 | Resistor | 100 Ω, 0402 or 0603 | 1 |
| R3 | Resistor | 4.7 kΩ, 0402 or 0603 | 1 |
| J1 | FPC connector | 6-pin, 0.5 mm, ZIF, **top contact**, 0.3 mm FPC | 2 |
| — | Adapter board | 12 × 10 mm, two-layer | 5 (MOQ) |
| — | Enamelled copper wire | 0.1 mm | 1 m |
| — | Kapton tape | 5 mm | 1 roll |

Two each of Q1 and J1 — a 0.5 mm connector does not always forgive the first attempt.

### 5.2 Tools

| Item | Why |
|---|---|
| Bench supply with current limit | Do not touch the FL module without one |
| Soldering tip 0.2–0.4 mm | For J1 |
| No-clean flux gel | Makes 0.5 mm pitch feasible in the first place |
| Multimeter with mA range | Sizing R1, verifying tap points |
| Loupe or USB microscope | Finding solder bridges |

### 5.3 Cost

Display USD 8.80, small parts under EUR 10, PCB under EUR 5. Roughly EUR 25–30 total
plus shipping from Dalian.

---

## 6. Firmware

Arduino core 3.x (the LEDC API changed from 2.x):

```cpp
#define FL_PIN      44    // TP2 / U0RXD
#define FL_MAX_DUTY 204   // 80 % of 255, thermal headroom

void flInit() {
  pinMode(FL_PIN, OUTPUT);       // releases the pin's UART0 function
  digitalWrite(FL_PIN, LOW);
  ledcAttach(FL_PIN, 2000, 8);   // pin, 2 kHz, 8 bit
  ledcWrite(FL_PIN, 0);
}

void flSet(uint8_t duty) {
  ledcWrite(FL_PIN, duty > FL_MAX_DUTY ? FL_MAX_DUTY : duty);
}

void flOffBeforeSleep() {
  ledcWrite(FL_PIN, 0);
  ledcDetach(FL_PIN);
  pinMode(FL_PIN, OUTPUT);
  digitalWrite(FL_PIN, LOW);
  gpio_deep_sleep_hold_en();     // GPIO44 is not an RTC pin
}
```

2 kHz is above the flicker threshold and far below anything the BMA423 would pick up as
interference. `flOffBeforeSleep()` must run before **every** `esp_deep_sleep_start()`.

No `gpio_hold_en()` — on the S3 that only exists for GPIO 0–21. The real safeguard
against unwanted illumination is the 4.7 kΩ pulldown anyway.

`flInit()` must run early in `setup()` so the pin loses its UART0 assignment.

---

## 7. Sequence

### Step 0 — open the watch, find the test points

Locate TP2, TP4 and TP5 on the board and check they are accessible as exposed solder
pads. Two minutes of work, resolves the last unknown.

### Step 1 — order

Display from buy-lcd (check availability), small parts in parallel from LCSC or Mouser.

### Step 2 — test the display outside the watch

**The most important step.**

- EPD side on an ESP32 devkit or DESPI-C02
- FL side on the bench supply: 2.9 V, current limit 50 mA
- Verify the pinout against the table in section 2.2
- Judge brightness at 15 / 25 / 45 mA

This is where you decide whether the mod is worth it. If brightness at a reasonable
current is not good enough, you have lost USD 9 rather than a working Watchy.

### Step 3 — populate the adapter board

Order: Q1, then R1/R2/R3, J1 last. Check with no FPC fitted:

- Gate to GND: 4.7 kΩ
- Drain to 3V3 pad: high impedance
- Source to GND: 0 Ω

### Step 4 — determine R1

Adapter board on the bench supply (3.3 V), FPC inserted, gate at 3.3 V. Measure the
current and adjust R1 up or down from 22 Ω until brightness is right.

### Step 5 — case

Print, check fit **with the old display**. Iterate.

### Step 6 — installation

Swap the display, wire up the adapter board, flash the firmware, and only then close
the case.

---

## 8. Open items

| Item | Status |
|---|---|
| Test points physically accessible | visual check, step 0 |
| FPC routing to the adapter board | determines top vs bottom contact |
| R1 value | measurement, step 4 |
| Actual brightness | only assessable in step 2 |

**On FPC routing:** "contacts on top" applies to the unbent FPC. If the cable is folded
180° inside the case, the pads face down and you need bottom contact instead. Decide the
routing before ordering — or order one of each type.

# Appendix: Frontlight mod for Watchy v2 / USB-C clone

Supplement to `watchy-v3-frontlight-mod.md`. All chapters of the main document apply
**except 2 (circuit), 4 (connection points), 5.1 (BOM) and 6 (firmware)** — those are
replaced here.

Sources: `Watchy.sch` / `Watchy.kicad_pcb` rev 2.0 (official SQFMI layout), cross-checked
against `WatchySchematic.pdf`.

---

## A1. Why the v2 needs a different circuit

On the v3, TP2 (GPIO44) provides a free, switchable GPIO on a test pad. The v2 has no
equivalent:

| GPIO | Status in the layout |
|---|---|
| 32 | unconnected, **no trace** |
| 2 | unconnected, **no trace** |
| 33 | taken — net `32KHZ` (CLKO of the PCF8563, pin 8) |
| 15 | tied to GND |
| 0 | brought out to TP1, but a **strapping pin** |

GPIO16/17 are internally wired to the embedded flash on the ESP32-PICO-D4 and are ruled
out entirely.

TP1 (GPIO0) would be the only candidate. A pulldown there would force the watch into the
bootloader at power-up; the only way around that is a P-channel high-side switch, and
GPIO0 also hangs off the CP2102 auto-reset circuit. Most clones omit TP1 anyway.

**Consequence: I²C LED driver instead of a MOSFET on a GPIO.**

---

## A2. Connection points

Four pads, 1 mm diameter, round, on the component side, arranged as a 2×2 field at
2.54 mm pitch. Everything the circuit needs sits in that one square.

| TP | Net | Position in the original layout (mm) |
|---|---|---|
| TP2 | SCL | 91.38 / 105.82 |
| TP3 | SDA | 91.38 / 108.36 |
| TP4 | +3V3 | 93.92 / 105.82 |
| TP5 | GND | 93.92 / 108.36 |

Arrangement seen from the component side:

```
SCL   3V3
SDA   GND
```

Not part of the block and usually omitted on clones: TP1 (GPIO0) at 86.90 / 91.61 and
TP6 (GND) at 82.85 / 92.16.

### A2.1 Identifying the pads

Battery removed, powered over USB. Black probe on the shell of the USB connector.

| Measurement | Pad |
|---|---|
| 0.00 V | **GND** |
| 3.23–3.37 V, steady | **+3V3** |
| approx. 4.7 kΩ to the 3V3 pad | **SCL or SDA** |

The pull-ups are R2 and R3, 4.7 kΩ each, to +3V3.

**You do not need to know the orientation of the block.** Once GND and 3V3 are
identified, the remaining two are necessarily SCL and SDA. Swapping them does no harm —
the I²C scanner simply will not find the driver, you swap the two wires, done. Unlike
confusing 3V3 with battery voltage, this has no consequences.

---

## A3. Circuit

Replaces sections 2.3 to 2.5 of the main document.

### A3.1 Topology

```
3V3 (TP4) ──┬── J1.1 ──┐
            └── J1.2 ──┤ 3× LED in parallel
                       │
            ┌── J1.5 ──┤
            └── J1.6 ──┘
            │
           R1
            │
       ┌────┴────┬─────────┐
    LED0      LED1      LED2      ← outputs in parallel
       └─── U1 PCA9633 ───┘
              │  │
   SCL (TP2) ─┘  └─ SDA (TP3)
              │
             GND (TP5)
```

### A3.2 Netlist

| Net | Connections |
|---|---|
| `3V3` | TP4 · J1 pin 1 · J1 pin 2 · U1 VDD · C1-A |
| `LED_K` | J1 pin 5 · J1 pin 6 · R1-A |
| `SW` | R1-B · U1 LED0 · U1 LED1 · U1 LED2 |
| `SCL` | TP2 · U1 SCL |
| `SDA` | TP3 · U1 SDA |
| `GND` | TP5 · U1 VSS · C1-B |

Four wires to the mainboard instead of three.

### A3.3 Why three outputs in parallel

The PCA9633 channels are current sinks rated **25 mA each**. The frontlight draws up to
45 mA, and the three LED dies share a common cathode net inside the module — driving them
individually is not possible.

Three channels in parallel work out to 15 mA per channel at 45 mA total. Even with
uneven sharing between the sinks, every channel stays well inside its rating. Channel
LED3 is left unused.

### A3.4 I²C address

| Device | Address |
|---|---|
| BMA423 | 0x18 or 0x19 |
| PCF8563 | 0x51 |
| **PCA9633** | **0x62** |

No collision. The v2 bus is comfortably loaded with two devices and nowhere near
capacitance limits.

### A3.5 R1

Unchanged from sections 2.5 and 2.6 of the main document: **22 Ω as a starting value**,
approach from above, never below 10 Ω. The voltage headroom is the same, because the v2
also runs a 3.3 V LDO (ME6211C33M5G-N, 500 mA).

The PCA9633 adds a small extra voltage drop — all the more reason to measure the current
on the finished assembly rather than calculating it.

---

## A4. Bill of materials

Replaces section 5.1.

| Ref | Part | Specification | Qty |
|---|---|---|---|
| — | Display | GDEY0154D67-FL04 | 1 |
| U1 | LED driver | PCA9633, I²C, 4× 25 mA | 2 |
| C1 | Capacitor | 100 nF, 0402 | 2 |
| R1 | Resistor | assortment 10/15/22/27/39/68 Ω, 0603 | 5 each |
| J1 | FPC connector | 6-pin, 0.5 mm, ZIF, top contact, 0.3 mm FPC | 2 |
| — | Adapter board | approx. 15 × 12 mm, two-layer | 5 (MOQ) |
| — | Enamelled copper wire | 0.1 mm | 1 m |

Dropped compared to variant A: Q1, R2, R3.

**On the PCA9633 package:** the TSSOP-16 measures 5.0 × 4.4 mm and hand-solders well.
The HVQFN-16 at 3 × 3 mm saves space but has a thermal pad and needs hot air. It is worth
checking whether the **PCA9632** is available — same functionality in an 8-pin package,
noticeably smaller. Read the datasheet: channel current and address range need to match.

---

## A5. Firmware

Replaces chapter 6. No LEDC, no `gpio_hold` — the driver holds its own state.

```cpp
#include <Wire.h>

#define PCA_ADDR    0x62
#define FL_MAX_DUTY 204   // 80 %, thermal headroom

static void pcaWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void flInit() {
  Wire.begin(SDA, SCL);
  pcaWrite(0x00, 0x00);   // MODE1: clear SLEEP bit
  pcaWrite(0x08, 0x2A);   // LEDOUT: LED0..2 to PWM, LED3 off
  flSet(0);
}

void flSet(uint8_t duty) {
  if (duty > FL_MAX_DUTY) duty = FL_MAX_DUTY;
  pcaWrite(0x02, duty);   // PWM0
  pcaWrite(0x03, duty);   // PWM1
  pcaWrite(0x04, duty);   // PWM2
}

void flOffBeforeSleep() {
  flSet(0);
  pcaWrite(0x00, 0x10);   // MODE1: set SLEEP bit, ~1 µA
}
```

### A5.1 The deep sleep trap in this variant

The 3.3 V rail stays up during deep sleep, and so does the PCA9633. **It holds its PWM
value indefinitely.** Forget `flOffBeforeSleep()` and the frontlight keeps burning while
the ESP32 sleeps — the battery is flat in eight hours.

This is the same risk as a missing gate pulldown in variant A, except without a hardware
safeguard. `flOffBeforeSleep()` must run before **every** `esp_deep_sleep_start()`.

---

## A6. Additional open items for the clone

The layout analysed here is the official SQFMI design. SQFMI never published layout files
for the v2, so the USB-C clone is a redraw. To resolve before soldering:

| Item | How |
|---|---|
| Whether SCL/SDA/3V3/GND really sit on the four pads | measurement per A2.1 |
| Whether the clone follows the v2 pinmap | flash the Watchy library with `ARDUINO_WATCHY_V20` — if display, all four buttons, vibration, BMA423 and RTC work, the mapping matches |
| Which ESP32 is fitted | `esptool.py chip_id` or `ESP.getChipModel()` |
| GDEH0154D67 or GDEY | read the marking on the display FPC |

The last item affects chapter 3 of the main document: the dimensions given there apply to
the GDEY. If the clone carries a GDEH, the case modification has to be re-measured against
the actual display. Electrically the two are identical (SSD1681, 24-pin,
`GxEPD2_154_D67`).

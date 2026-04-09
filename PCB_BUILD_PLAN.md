# MidiGuitar — PCB Build Plan v1.0

> Locked-in build plan for the MidiGuitar PCB. All decisions sourced from the FabLab inventory (Helsinki) and confirmed with the user. Execute as-is.

---

## Architecture

- **PCB** (40 × 55 mm, single-sided FR1, F.Cu only) — sits hidden in the guitar control cavity, foam-taped to the floor. Contains the XIAO ESP32-S3, the level shifter, the NeoPixel power conditioning, and 8 cable headers.
- **Pickguard** (3D-printed replacement) — holds every human-touchable component (OLED, encoder, slide switch, button, NeoPixel ring, both ToF sensors). Cables run from the pickguard down into the cavity to the PCB.
- **Power**: USB-C only (battery provisioned but not populated). 5 V → NeoPixel rail (dropped to ~4.5 V via Schottky). 3.3 V → everything else.
- **Transport**: USB MIDI (default) or BLE MIDI (selected by SW1 at boot).
- **Mode**: CC / Pitch Bend (selected via OLED menu, encoder driven).

---

## Final BOM

### On the PCB

| Ref | Inventory # | Qty | Description |
|---|---|---|---|
| U1 | 113991114 | 1 | Seeed XIAO ESP32-S3 (no headers) |
| U2 | TXB0104DR | 1 | Bidirectional level shifter SOIC-14 |
| D1 | CDBM1100-G | 1 | Schottky 100 V 1 A MiniSMA — drops 5 V → ~4.5 V for NeoPixel rail |
| C1 | EEE-FN1E101UL | 1 | 100 µF 25 V aluminum SMD — NeoPixel bulk |
| C2 | GRT31CC8YA106ME01L | 1 | 10 µF 1206 — NeoPixel HF bulk |
| C3 | C3216X7R2A104K160AA | 1 | 100 nF 1206 — TXB0104 VCCB decoupling |
| C4 | C3216X7R2A104K160AA | 1 | 100 nF 1206 — XIAO 5 V rail decoupling |
| C5 | C3216X7R2A104K160AA | 1 | 100 nF 1206 — TXB0104 VCCA / 3V3 decoupling |
| R1 | RC1206FR-07499RL | 1 | 499 Ω 1206 — NeoPixel data series |
| R2, R3, R4 | RC1206FR-0710KL | 3 | 10 kΩ 1206 — TXB0104 unused-input pull-downs |
| R6, R7 | RC1206FR-074K99L | 2 | 4.99 kΩ 1206, **DNP** — I²C pull-up reserve |
| J_NEO | from GBC36SGSN-M89 | 1 | 3-pin male SMD header |
| J_VL53 | from GBC36SGSN-M89 | 1 | 5-pin male SMD header |
| J_VL6 | from GBC36SGSN-M89 | 1 | 5-pin male SMD header |
| J_OLED | 95278-101-04LF | 1 | 4-pin male SMD header |
| J_I2C | 95278-101-04LF | 1 | 4-pin male SMD header (future I²C expansion) |
| J_UI | from GBC36SGSN-M89 | 1 | 6-pin male SMD header |
| XIAO sockets | 2× NPTC051KFXC-RC + 2× NPTC021KFXC-RC | 4 | Stacked 5+2 pin = 7-pin female socket per side, ×2 sides |
| Jumpers (reserve) | RC1206FR-070RL | ~3 | 0 Ω 1206 — single-sided routing crossovers |
| Pads | bare 1.5 mm round | 10 | `BAT+`, `BAT−`, `SPARE_GPIO`, `TP1` 5V, `TP2` 4V5_LED, `TP3` 3V3, `TP4` GND, `TP5` SDA, `TP6` SCL, `TP7` NEO_DIN |

### Cable side (for crimping)

| Cable | Plug | Strain relief | Wire |
|---|---|---|---|
| J_NEO (3p, 22 AWG) | 3-640620-3 | 643077-3 | 22 AWG ribbon |
| J_VL53 (5p, 28 AWG) | 3-640623-5 | 643077-5 | 28 AWG ribbon |
| J_VL6 (5p, 28 AWG) | 3-640623-5 | 643077-5 | 28 AWG ribbon |
| J_OLED (4p, 28 AWG) | 3-640623-4 | 643077-4 | 28 AWG ribbon |
| J_I2C (4p, 28 AWG) | 3-640623-4 | 643077-4 | 28 AWG ribbon (don't crimp until needed) |
| J_UI (6p, 28 AWG) | 3-640623-6 | 643077-6 | 28 AWG ribbon |

Crimp tool: **`59803-1` MTA-100 T-handle** (in inventory).

### On the pickguard (off-PCB, your existing parts)

- 1× Adafruit VL53L4CD breakout (long-range ToF)
- 1× Adafruit VL6180X breakout (close-range ToF)
- 1× WS2812B 16-LED NeoPixel ring
- 1× SSD1306 128×64 OLED I²C module
- 1× SPDT slide switch (`JS102011SCQN`)
- 1× large tactile button (12 mm or 6×6 with tall stem)
- 1× rotary encoder with push (generic EC11-style, metal body, plastic shaft)

---

## GPIO allocation (locked, all 11 pins used)

| XIAO pin | GPIO | Direction | Function | Connects to |
|---|---|---|---|---|
| D0 | 1 | — | **SPARE** | `SPARE_GPIO` test pad |
| D1 | 2 | OUT | NeoPixel data (3.3 V logic) | TXB0104 A1 (pin 3) |
| D2 | 3 | IN, pull-up | SW1 slide — Transport USB/BLE | J_UI pin 2 |
| D3 | 4 | IN, pull-up | BTN1 — Bypass (latched in firmware) | J_UI pin 3 |
| D4 | 5 | I²C | SDA (3V3) | J_VL53.3, J_VL6.3, J_OLED.4, J_I2C.3 |
| D5 | 6 | I²C | SCL (3V3) | J_VL53.4, J_VL6.4, J_OLED.3, J_I2C.4 |
| D6 | 43 | OUT | VL53L4CD XSHUT | J_VL53 pin 5 |
| D7 | 44 | OUT | VL6180X XSHUT | J_VL6 pin 5 |
| D8 | 7 | IN, IRQ | Encoder A | J_UI pin 4 |
| D9 | 8 | IN, IRQ | Encoder B | J_UI pin 5 |
| D10 | 9 | IN, pull-up | Encoder push | J_UI pin 6 |

All inputs use `INPUT_PULLUP`. Switches/buttons short to GND. Encoder A/B use interrupts.

---

## Power tree

| Net | Source | Destinations |
|---|---|---|
| **5V** | XIAO 5V pin (USB VBUS) | C4 (100 nF decoupling), D1 anode |
| **4V5_LED** | D1 cathode | C1 (100 µF), C2 (10 µF), C3 (100 nF), TXB0104 pin 14 (VCCB), J_NEO pin 1 |
| **3V3** | XIAO 3V3 pin | C5 (100 nF), TXB0104 pin 1 (VCCA), TXB0104 pin 2 (OE), J_VL53.1, J_VL6.1, J_OLED.1, J_I2C.1, R6/R7 (DNP) |
| **GND** | XIAO GND | Ground pour, all decoupling cap returns, all connector GND pins, R2/R3/R4 |

---

## TXB0104 wiring (driving WS2812 — the trick)

The TXB0104 normally can't drive WS2812 because its push-pull is too weak for the WS2812's ~3.5 V VIH at 5 V. We drop the NeoPixel rail to ~4.5 V with a Schottky, which lowers the WS2812 threshold to ~3.15 V — well within TXB0104's drive capability.

```
XIAO 3V3 ──┬── TXB0104 pin 1 (VCCA)
           ├── TXB0104 pin 2 (OE)        [HIGH = enabled]
           └── 100 nF (C5) to GND, right at pin 1

XIAO 5V ── D1 (CDBM1100-G, anode→cathode) ──┬── 4V5_LED net
                                              ├── TXB0104 pin 14 (VCCB)
                                              ├── 100 nF (C3) to GND at pin 14
                                              ├── 100 µF (C1) to GND
                                              ├── 10 µF (C2) to GND
                                              └── J_NEO pin 1

XIAO D1 (GPIO2) ── TXB0104 pin 3 (A1)
TXB0104 pin 13 (B1) ── R1 (499 Ω) ── J_NEO pin 2 (DIN)

A2/A3/A4 (pins 4/5/6) ── 10 kΩ (R2/R3/R4) ── GND
B2/B3/B4 (pins 11/10/9) ── leave open
TXB0104 pin 7 ── GND
```

> ⚠ **Verify TXB0104DR pin numbering against the TI datasheet (SCES650)** when placing the symbol in KiCad — KiCad's built-in symbol has it correct, just hover over each pin.

---

## All 8 PCB connectors — silkscreen-ready pinouts

### J_NEO — NeoPixel ring (3 pin, 22 AWG)

| Pin | Net | Silkscreen |
|---|---|---|
| 1 | 4V5_LED | `+4V5` |
| 2 | NEO_DIN (post-499 Ω) | `DIN` |
| 3 | GND | `GND` |

### J_VL53 — VL53L4CD long-range ToF (5 pin, 28 AWG)

| Pin | Net | Silkscreen |
|---|---|---|
| 1 | 3V3 | `3V3` |
| 2 | GND | `GND` |
| 3 | SDA | `SDA` |
| 4 | SCL | `SCL` |
| 5 | GPIO43 | `XSH` |

### J_VL6 — VL6180X close-range ToF (5 pin, 28 AWG)

| Pin | Net | Silkscreen |
|---|---|---|
| 1 | 3V3 | `3V3` |
| 2 | GND | `GND` |
| 3 | SDA | `SDA` |
| 4 | SCL | `SCL` |
| 5 | GPIO44 | `XSH` |

### J_OLED — SSD1306 128×64 (4 pin, 28 AWG)

| Pin | Net | Silkscreen |
|---|---|---|
| 1 | 3V3 | `3V3` |
| 2 | GND | `GND` |
| 3 | SCL | `SCL` |
| 4 | SDA | `SDA` |

> ⚠ Cheap SSD1306 modules ship in at least 3 different pin orders. **Verify your module's silkscreen before crimping.**

### J_I2C — Future I²C expansion (4 pin, 28 AWG)

| Pin | Net | Silkscreen |
|---|---|---|
| 1 | 3V3 | `3V3` |
| 2 | GND | `GND` |
| 3 | SDA | `SDA` |
| 4 | SCL | `SCL` |

Same pinout as J_OLED — cables interchangeable.

### J_UI — Slide + button + encoder combined (6 pin, 28 AWG)

| Pin | Net | Silkscreen |
|---|---|---|
| 1 | GND (shared return) | `GND` |
| 2 | GPIO3 (SW1) | `SW` |
| 3 | GPIO4 (BTN1) | `BTN` |
| 4 | GPIO7 (ENC_A) | `EA` |
| 5 | GPIO8 (ENC_B) | `EB` |
| 6 | GPIO9 (ENC_PUSH) | `EP` |

---

## Cable wiring tables (the soldering instructions)

### Cable 1 — NeoPixel ring (3 wires, 22 AWG)

| Wire color | PCB pin | Ring pad |
|---|---|---|
| Red | 1 (`+4V5`) | `5V` / `VCC` |
| Yellow | 2 (`DIN`) | `DIN` (data input) |
| Black | 3 (`GND`) | `GND` |

Don't connect `DOUT` of the ring.

### Cable 2 — VL53L4CD breakout (Adafruit 5396)

| Ribbon | PCB pin | Adafruit pin |
|---|---|---|
| Red | 1 | `VIN` |
| 2 | 2 | `GND` |
| 3 | 3 | `SDA` |
| 4 | 4 | `SCL` |
| 5 | 5 | `SHDN` |

Leave `INT` and `GP1` unconnected.

### Cable 3 — VL6180X breakout (Adafruit 3316)

| Ribbon | PCB pin | Adafruit pin |
|---|---|---|
| Red | 1 | `VIN` |
| 2 | 2 | `GND` |
| 3 | 3 | `SDA` |
| 4 | 4 | `SCL` |
| 5 | 5 | `SHDN` (often labeled `GPIO1` or `CE`) |

Leave `GPIO0` unconnected.

### Cable 4 — OLED SSD1306

| Ribbon | PCB pin | OLED label (verify!) |
|---|---|---|
| Red | 1 | `VCC` |
| 2 | 2 | `GND` |
| 3 | 3 | `SCL` |
| 4 | 4 | `SDA` |

### Cable 5 — UI control plate (slide + button + encoder)

| Ribbon | PCB pin | Connects to (component end) |
|---|---|---|
| Red | 1 (`GND`) | • SW1 throw  • BTN1 leg A  • Encoder C (rotary common)  • Encoder push leg 1 |
| 2 | 2 (`SW`) | SW1 common (center) |
| 3 | 3 (`BTN`) | BTN1 leg B (diagonal opposite of leg A) |
| 4 | 4 (`EA`) | Encoder pin A |
| 5 | 5 (`EB`) | Encoder pin B |
| 6 | 6 (`EP`) | Encoder push leg 2 |

> ⚠ **Encoder verification (do this before crimping)**:
> 1. Set DMM to continuity. Probe the 3 rotary pins.
> 2. Find the middle pin that beeps to either neighbor as you slowly turn the shaft. That's **C (common)** → wire to Red (GND).
> 3. The other two are A and B (interchangeable; if direction is wrong in firmware, swap them).
> 4. Probe the 2 pins on the push side — they beep when pressed. One leg → Red (GND), other → wire 6.

> ⚠ **Tactile button**: 4-leg buttons have legs wired in pairs internally. Use a multimeter to find the two unconnected pairs, then pick one leg from each pair as A and B.

---

## PCB mechanical specs

| Spec | Value |
|---|---|
| Outline | **40 × 55 mm** rectangle |
| Layer | F.Cu only (Helsinki single-sided FR1, Roland mill) |
| Trace / clearance / drill | 0.4 mm / 0.4 mm / 0.8 mm |
| End mill | 1/64" for traces, 1/32" for outline |
| Mounting | 3M VHB foam tape primary; 2× M3 holes (3.2 mm drill) at opposite corners as backup |
| Antenna keepout | 6 mm × full board width strip at the XIAO antenna end (no copper, no traces, no pour) |
| Component side | All on top (single-sided assembly) |
| Connector edge | All 6 cable headers + JI2C along ONE long edge for clean cable routing |
| Silkscreen | Every header pin labeled, every test pad labeled, version `v1.0`, project URL |

---

## Pickguard layout (3D-printed, suggested)

```
   ╔══════════════════════════════════════════╗
   ║                                          ║
   ║   ⊙ VL6180X        [SLIDE]  [BTN] [ENC] ║   ← control area
   ║                                          ║
   ║                                          ║
   ║   ✺ NeoPixel ring   ┌─────────────┐     ║
   ║      around        │  OLED 128x64 │     ║
   ║   ⊙ VL53L4CD        └─────────────┘     ║
   ║                                          ║
   ╚══════════════════════════════════════════╝
```

- **VL53L4CD inside the NeoPixel ring** — the ring becomes a halo around the long-range sensor. The visual feedback bar graph is centered on the gesture target.
- **VL6180X near the neck** for fingertip work.
- **Control panel** (OLED + encoder + button + slide) clustered where you'd reach with a free hand between phrases.
- PCB foam-taped to cavity floor underneath.

### Pickguard cavity sizes

| Component | Mount | Hole / cavity |
|---|---|---|
| NeoPixel 16-LED ring | Recessed flush, hot glue | Ø 50 mm circle, 2 mm deep |
| VL53L4CD breakout | Hot glue under pickguard | Ø 4 mm sensor through-hole, breakout recess |
| VL6180X breakout | Hot glue under pickguard | Ø 3 mm sensor through-hole, breakout recess |
| OLED 128×64 | Glue from underside, screen flush | Rectangular cutout slightly smaller than the screen window |
| SW1 slide switch | Glue from underside | 9 × 4 mm slot |
| BTN1 tactile button | Glue from underside | Ø 5 mm hole for stem |
| Encoder | Bolt through pickguard with its own nut | Ø 7 mm hole (verify against your encoder's bushing) |

Route the cables through small channels in the pickguard underside down into the cavity to the PCB.

---

## Future expansion (do not add now)

The J_I2C header lets any of these plug in later with **zero PCB changes**:

| Inventory # | Device | What it adds |
|---|---|---|
| `ADXL343BCCZ` | 3-axis accelerometer | Tilt → CC (rotate guitar = mod wheel) |
| `LSM6DSV16XTR` | 6-axis IMU | Tilt + rotation rate → multiple CCs |
| `4554` (ICM-20948) | 9-axis IMU breakout | Full orientation tracking |
| `TLE493DA2B6HTSA1` | 3-axis Hall I²C | Magnet on a finger ring → continuous CC |
| `VEML6040A3OG` | RGB light sensor | Stage lighting reactive |
| `DPS310XTSA1` | Barometric pressure | Breath / ambient → CC |
| `3295` (PCF8523) | RTC | Timestamps for MIDI logging |

The `SPARE_GPIO` test pad on GPIO1 is also available if you need a digital input/output later.

---

## Helsinki single-sided routing notes

- **All traces on F.Cu**, ground pour on F.Cu (the same layer) filling around the traces.
- **No vias, no B.Cu**.
- **Crossovers handled by 0 Ω 1206 jumpers** — budget 2–3 around the XIAO where I²C has to cross GPIO traces.
- KiCad: Board Setup → only F.Cu, F.SilkS, F.Mask, Edge.Cuts enabled.
- Net classes: default 0.4 / 0.4 mm. Power class: 0.6 mm trace, 0.4 mm clearance.
- Pour ground on F.Cu after routing, with a clearance carve-out under the antenna keepout.

---

## Execution checklist

### Schematic (KiCad)
- [ ] Open `midiguitar.kicad_sch`
- [ ] **Delete**: AZ1117 regulator, its 2 caps, the battery Schottky, the USB-A connector, the second SW2 slide switch
- [ ] **Add symbols**: TXB0104DR, CDBM1100-G (D1), 100 µF EEE-FN1E101UL (C1), 10 µF (C2), 3× 100 nF (C3, C4, C5), 499 Ω 1206 (R1), 3× 10 kΩ 1206 (R2/R3/R4), 2× 4.99 kΩ DNP (R6/R7)
- [ ] **Add 6 cable headers**: J_NEO (3p), J_VL53 (5p), J_VL6 (5p), J_OLED (4p), J_I2C (4p), J_UI (6p)
- [ ] **Add 10 bare pads**: BAT+, BAT-, SPARE_GPIO, TP1–TP7
- [ ] **Wire** with net labels: `5V`, `4V5_LED`, `3V3`, `GND`, `SDA`, `SCL`, `NEO_DIN_3V3`, `NEO_DIN_4V5`, `NEO_DIN_OUT`, `VL53_XSHUT`, `VL6_XSHUT`, `SW1_IN`, `BTN1_IN`, `ENC_A`, `ENC_B`, `ENC_PUSH`, `SPARE`
- [ ] Wire per the GPIO allocation table and the TXB0104 wiring block above
- [ ] **ERC clean** — no errors, no exclusions
- [ ] **Footprint assignment** — verify each against `PCM_fab` lib

### PCB (KiCad)
- [ ] Switch to PCB editor → Update PCB from Schematic
- [ ] Board Setup: F.Cu only, default net class 0.4/0.4/0.8, power class 0.6/0.4
- [ ] Draw 40 × 55 mm rectangle on Edge.Cuts
- [ ] Place 2× M3 mounting holes (3.2 mm drill) at opposite corners
- [ ] Define 6 mm antenna keepout zone at XIAO antenna end (no copper / no tracks / no pour)
- [ ] Place XIAO first, USB-C facing the cavity opening, antenna end pointing inward toward the keepout
- [ ] Place TXB0104 + R1 + D1 + C1/C2/C3 cluster on the opposite end from the XIAO antenna
- [ ] Place all 6 cable headers along ONE long edge in this order: `J_OLED`, `J_VL53`, `J_VL6`, `J_NEO`, `J_I2C`, `J_UI`
- [ ] Place test point pads, BAT+/BAT-, SPARE_GPIO
- [ ] Add silkscreen: pin labels on every header per the silkscreen column above, version `v1.0`, date, project URL
- [ ] **Route**: power first (0.6 mm), then I²C (0.4 mm), then NEO_DIN, then GPIO
- [ ] Add 0 Ω jumpers (R_JP1, R_JP2, …) where single-sided routing forces a crossover
- [ ] Pour GND on F.Cu, carve out under antenna keepout
- [ ] **DRC clean**
- [ ] 3D viewer sanity check — confirm nothing taller than 15 mm (clearance to 20 mm cavity), USB-C aligns with cavity edge
- [ ] Export PNG for `mods` (Helsinki workflow): top traces, top mask, edge cuts, drills

### Mill (Helsinki FabLab)
- [ ] Mill traces with 1/64" end mill
- [ ] Mill outline with 1/32" end mill
- [ ] Drill mounting holes (0.8 mm)
- [ ] Inspect under microscope, clean copper

### Assembly
- [ ] Solder XIAO sockets (2× 7-pin = NPTC051 + NPTC021 stacked) — DON'T solder XIAO yet
- [ ] Solder TXB0104, R1, R2/R3/R4, D1, C1, C2, C3, C4, C5
- [ ] Solder all 6 male pin headers
- [ ] Solder 0 Ω jumpers if any
- [ ] Visual + multimeter continuity check on power rails
- [ ] Plug XIAO into sockets, USB-C in, verify 3.3 V and 5 V test points
- [ ] Crimp cables (use MTA-100 T-handle `59803-1`)
- [ ] Mount components on 3D-printed pickguard
- [ ] Plug all 5 cables into PCB headers
- [ ] Foam-tape PCB to cavity floor
- [ ] Upload firmware, test

---

## After PCB is done — firmware port

Update `code/tof_midi_v3/tof_midi_v3.ino`:

1. Replace VL53L1X library with VL53L4CD library (STMicro or Pololu)
2. Update XSHUT pin assignments: VL53 on D6 (GPIO43), VL6180 on D7 (GPIO44)
3. Add `Adafruit_SSD1306` + `Adafruit_GFX` libs and OLED display rendering
4. Add encoder library + interrupt handlers (D8/D9 = A/B, D10 = push)
5. Add menu state machine driven by encoder + push
6. Add NVS save/load for: mode (CC/PB), channel, CC numbers, distance ranges, curve, brightness
7. Add latched bypass on BTN1 (D3) — when bypassed, run sensor reads but don't send MIDI; dim NeoPixel ring to half
8. OLED display layout:

```
┌────────────────────────┐
│ MidiGuitar  USB  CC    │  ← top: transport, mode
│ Ch 01                  │
│ ────────────────────── │
│ CC20: ████████░░  102  │  ← live bars
│ CC21: ███░░░░░░░   38  │
│ CC22: █░░░░░░░░░   12  │
│                  BYPASS│  ← shows when bypassed
└────────────────────────┘
```

---

## Notes / decisions / why

- **TXB0104 driving WS2812**: only works because of the Schottky-on-5V trick (drops to ~4.5V). User had no other level shifter available.
- **No AZ1117 regulator**: XIAO already provides 3V3 — redundant.
- **No battery Schottky**: XIAO charger doesn't tolerate the 0.3 V drop.
- **No USB-A**: use XIAO USB-C directly; the XIAO has the only path to USB data lines.
- **Direct-soldered XIAO via sockets**: lets you swap to ESP32-C6 XIAO or other pin-compatible variants.
- **Both ToF connectors are 5 pins** with dedicated XSHUT GPIOs: future-proof for any ST ToF combo.
- **Latched bypass**: momentary makes no sense for live use; firmware tracks state, button is mechanically momentary.
- **Single-sided** (Helsinki constraint): expect 2–3 zero-Ω jumpers around the XIAO.
- **3D-printed pickguard**: full freedom on hole positions; PCB doesn't need to align to anything.
- **Battery deferred**: solder pads provisioned, no connector. FabLab has no LiPo connectors anyway.
- **J_I2C added**: zero GPIO cost, zero risk, maximum future-proofing for IMU / pressure / Hall / etc.

---

## File index

- `midiguitar.kicad_sch` — main schematic
- `midiguitar.kicad_pcb` — PCB layout
- `midiguitar.kicad_pro` — KiCad project
- `HARDWARE.md` — original specification (some sections superseded by this plan)
- `code/tof_midi_v3/tof_midi_v3.ino` — current firmware (needs porting to VL53L4CD + OLED + encoder)

# U1 Swap: SN74LVC1T45DBVR → TXB0104DR

Level shifter replacement because the SN74LVC1T45DBVR order is delayed. Uses parts on hand.

## BOM (matched to available stock)

Existing (keep in place): C1 (100 nF, 3V3), C2 (100 nF, PWR_5V), C3 (100 µF bulk, PWR_5V), R1 (330 Ω, DIN series).

New parts to add:

| Ref | Qty | Part number | Spec | Role |
|---|---|---|---|---|
| U1 | 1 | TXB0104DR | SOIC-14 (TI D package) | Replaces SN74LVC1T45 on U1 pads |
| D1 | 1 | CDBM1100-G | Schottky, 1 A, MiniSMA | Drops PWR_5V → 4V5_NEO for WS2812 rail |
| C4 | 1 | C3216X7R2A104K160AA | 0.1 µF 1206 X7R | VCCA decoupling at U1 pin 1 |
| C5 | 1 | C3216X7R2A104K160AA | 0.1 µF 1206 X7R | VCCB decoupling at U1 pin 14 |
| R2 | 1 | RC1206FR-0710KL | 10 kΩ 1206 | A2 pull-down |
| R3 | 1 | RC1206FR-0710KL | 10 kΩ 1206 | A3 pull-down |
| R4 | 1 | RC1206FR-0710KL | 10 kΩ 1206 | A4 pull-down |

Note on bulk caps: the existing C2 (100 nF) and C3 (100 µF) were originally on PWR_5V. In the swap they move to the 4V5_NEO rail (post-Schottky), where they serve as the NeoPixel/VCCB decoupling + bulk. C3 (100 µF) replaces the originally-planned separate C6, so no extra 10 µF part is needed.

## New net: `4V5_NEO`

Break the existing `PWR_5V` net into two:

- `PWR_5V` (unchanged): XIAO 5V pin, PWR_FLAG. Schottky anode.
- `4V5_NEO` (new): Schottky cathode → J3 pin 2 (NeoPixel VCC), U1 pin 14 (VCCB), C4, C5.

## TXB0104 pin map (TI D/PW-14 package — matches the KiCad symbol)

```
         VCCA ┤1   14├ VCCB
           A1 ┤2   13├ B1
           A2 ┤3   12├ B2
           A3 ┤4   11├ B3
           A4 ┤5   10├ B4
           NC ┤6    9├ NC
          GND ┤7    8├ OE
```

| Pin | Net | Notes |
|---|---|---|
| 1 VCCA | PWR_3V3 | Place C4 right here |
| 2 A1 | Net-(M1-D10) | Input from XIAO GPIO9 |
| 3 A2 | GND via R2 (10 kΩ) | Unused — TI-required pull-down |
| 4 A3 | GND via R3 (10 kΩ) | Unused |
| 5 A4 | GND via R4 (10 kΩ) | Unused |
| 6 NC | — | Leave floating |
| 7 GND | PWR_GND | |
| 8 OE | PWR_3V3 | **Tie HIGH to enable** (NOT pin 2!) |
| 9 NC | — | Leave floating |
| 10 B4 | — | Unused output, leave floating |
| 11 B3 | — | Unused output, leave floating |
| 12 B2 | — | Unused output, leave floating |
| 13 B1 | Net-(U1-B) | → R1 → J3 pin 1 (DIN) |
| 14 VCCB | 4V5_NEO | Place C5 right here |

## Schematic changes

- [ ] Delete U1 symbol (SN74LVC1T45DBVR)
- [ ] Place TXB0104DR symbol (KiCad: `Interface_Expansion:TXB0104D` or similar — verify pin numbering matches the table above)
- [ ] Wire the 8 connected pins per pin map above
- [ ] Mark pins 8, 9, 10, 11, 12 as "Not Connected" flags (no ERC warnings)
- [ ] Place D1 (CDBM1100-G) between PWR_5V and new 4V5_NEO net
- [ ] Add new net label `4V5_NEO`
- [ ] Reconnect J3 pin 2 from PWR_5V to 4V5_NEO
- [ ] Place C4 (100 nF) across U1 pin 1 ↔ GND — **both pads, one on PWR_3V3, one on PWR_GND, in parallel with the chip (NOT in series with it)**
- [ ] Place C5 (100 nF) across U1 pin 14 ↔ GND — **both pads, one on 4V5_NEO, one on PWR_GND, in parallel**
- [ ] Move existing C3 (100 µF bulk) to the 4V5_NEO rail
- [ ] Place R2, R3, R4 (10 kΩ) from pins 4, 5, 6 to GND
- [ ] Run ERC — should be clean

## PCB changes

Board is single-sided F.Cu only. Plan jumpers if needed.

- [ ] Delete old U1 SOT-23-6 footprint
- [ ] Assign SOIC-14 footprint to new U1 (Package_SO:SOIC-14_3.9x8.7mm_P1.27mm)
- [ ] Update PCB from schematic
- [ ] Place U1 — needs ~10 × 7 mm courtyard. Push away from the XIAO antenna
- [ ] Place C4 immediately adjacent to U1 pin 1 (C4 pad 1 to PWR_3V3, pad 2 to PWR_GND)
- [ ] Place C5 immediately adjacent to U1 pin 14 (C5 pad 1 to 4V5_NEO, pad 2 to PWR_GND)
- [ ] Move existing C3 (100 µF bulk) to sit near J3 on the 4V5_NEO rail
- [ ] Place D1 in the 5 V feed path — anode toward USB/5V source, cathode toward 4V5_NEO. Short trace after D1
- [ ] Place R2/R3/R4 close to their respective pins (4/5/6)
- [ ] Reroute Net-(M1-D10) to new U1 pin 2
- [ ] Reroute Net-(U1-B) from new U1 pin 13 → R1 (existing 330 Ω) → J3 pin 1
- [ ] Cut the old PWR_5V trace to J3 pin 2; reroute it to come off D1 cathode (4V5_NEO)
- [ ] Verify XIAO 5V pin is still on raw PWR_5V (pre-Schottky)
- [ ] Run DRC — should be clean
- [ ] Visual check: no dangling nets, all pads connected

## Bring-up checklist

Before powering full USB 5 V:

- [ ] Visual inspection under magnification: solder bridges, especially on SOIC-14 pitch
- [ ] Continuity check: U1 pin 1 to 3V3, pin 8 to 3V3, pin 7 to GND, pin 14 to D1 cathode
- [ ] Diode check: D1 anode → cathode reads ~0.25–0.4 V in diode mode
- [ ] Resistance check: 4V5_NEO to GND should NOT be a short
- [ ] Power up with current-limited supply (500 mA limit, watch for smoke)

Once powered:

- [ ] Measure PWR_5V: should read ~5.0 V
- [ ] Measure 4V5_NEO: should read ~4.4–4.6 V (no LED load)
- [ ] Measure U1 pin 1: 3.3 V
- [ ] Measure U1 pin 14: ~4.5 V
- [ ] Measure U1 pin 8 (OE): 3.3 V
- [ ] Load firmware, boot animation
- [ ] Scope U1 pin 13 (B1) if available — should see ~4.5 Vpp square waves during animation
- [ ] All 16 LEDs light correctly through full boot animation
- [ ] No flickering or color glitches through 30 seconds of runtime

## If the ring is flaky

Symptoms → fix:

- **First LED OK, later LEDs random colors**: signal integrity degrading down the chain. Shorten wire from PCB to ring, or drop 4V5_NEO further (add second CDBM1100-G in series → ~4.0 V).
- **All LEDs wrong color**: data polarity/wiring issue. Confirm J3 pin 1 = DIN, not DOUT.
- **Nothing lights**: check VCCB voltage, check OE is actually HIGH, confirm B1 is pin 13 and not mirrored.
- **LEDs occasionally glitch once per second**: XIAO GPIO9 state during init. Irrelevant once firmware loop starts.

## Tradeoffs (reminder)

Dimmer LEDs (~10% at startup, invisible at firmware's brightness=40 cap). Weaker drive than LVC1T45 — keep DIN wire short (< 20 cm ideally). Known-working topology from the original build plan.

## References

- Current U1 in [finalMidiGuitar.kicad_sch](finalMidiGuitar/finalMidiGuitar.kicad_sch) and [finalMidiGuitar.kicad_pcb](finalMidiGuitar/finalMidiGuitar.kicad_pcb)
- Firmware NeoPixel pin at [tof_midi_v4.ino:30](code/tof_midi_v4/tof_midi_v4.ino#L30) — GPIO9, no change needed
- Original TXB0104 plan: [PCB_BUILD_PLAN.md:100-124](PCB_BUILD_PLAN.md#L100-L124)
- TXB0104 datasheet: TI SCES710
- CDBM1100-G datasheet: Comchip

---

# Pin-by-pin pinout (post-swap)

Complete connection map for every component on the board after the U1 swap. Use this as the wiring reference during schematic edit and during bring-up probing.

## Nets on the board

| Net | Meaning | Sourced from |
|---|---|---|
| PWR_5V | USB 5 V, raw | XIAO pin 14 (5V), D1 anode, C2, C3, PWR_FLAG |
| **4V5_NEO** (new) | ~4.5 V NeoPixel rail | D1 cathode |
| PWR_3V3 | XIAO regulated 3.3 V | XIAO pin 12 (3V3) |
| PWR_GND | Ground | XIAO pin 13 (GND) |
| Net-(M1-D10) | NeoPixel data (3.3 V logic) | XIAO D10 (GPIO9) |
| Net-(U1-B) | NeoPixel data (4.5 V logic) | U1 pin 13 (B1) |
| Net-(J3-Pin_1) | NeoPixel DIN after series R | R1 pad 2 |
| Net-(M1-D2) | Button signal | XIAO D2 (GPIO3) |
| Net-(M1-D3) | Encoder push | XIAO D3 (GPIO4) |
| Net-(M1-D8) | Encoder CH_A | XIAO D8 (GPIO7) |
| Net-(M1-D9) | Encoder CH_B | XIAO D9 (GPIO8) |
| SDA | I2C data | XIAO D4 (GPIO5) |
| SCL | I2C clock | XIAO D5 (GPIO6) |
| XSHUT | ToF shutdown | XIAO D6 (GPIO43) |
| INT | ToF interrupt | XIAO D0 (GPIO1) |

---

## M1 — XIAO ESP32-S3 (14 pads, SMD socket)

| Pad | Silkscreen | ESP32-S3 GPIO | Net | Connected to |
|---|---|---|---|---|
| 1 | D0 | GPIO1 | INT | J1 pin 2 (ToF INT) |
| 2 | D1 | GPIO2 | — | **Unconnected** (spare) |
| 3 | D2 | GPIO3 | Net-(M1-D2) | SW1 pin 2 (button) |
| 4 | D3 | GPIO4 | Net-(M1-D3) | S1 switch pin 2 (encoder push) |
| 5 | D4 | GPIO5 | SDA | J1 pin 4, J4 pin 4 (I2C data) |
| 6 | D5 | GPIO6 | SCL | J1 pin 3, J4 pin 3 (I2C clock) |
| 7 | D6 | GPIO43 | XSHUT | J1 pin 1 (ToF XSHUT) |
| 8 | D7 | GPIO44 | — | **Unconnected** (spare) |
| 9 | D8 | GPIO7 | Net-(M1-D8) | S1 pin A (encoder CH_A) |
| 10 | D9 | GPIO8 | Net-(M1-D9) | S1 pin B (encoder CH_B) |
| 11 | D10 | GPIO9 | Net-(M1-D10) | **U1 pin 2 (A1)** — NeoPixel data |
| 12 | 3V3 | — | PWR_3V3 | C1, U1 pins 1 & 8, J1 pin 5, J4 pin 2 |
| 13 | GND | — | PWR_GND | Everything (common ground) |
| 14 | 5V | — | PWR_5V | C2, C3, **D1 anode** |

## U1 — TXB0104DR (SOIC-14) [NEW]

| Pin | Name | Net | Connected to |
|---|---|---|---|
| 1 | VCCA | PWR_3V3 | XIAO 3V3, C4 pad 1 |
| 2 | A1 | Net-(M1-D10) | XIAO D10 (GPIO9) |
| 3 | A2 | — | R2 pad 1 (pull-down) |
| 4 | A3 | — | R3 pad 1 (pull-down) |
| 5 | A4 | — | R4 pad 1 (pull-down) |
| 6 | NC | — | No connection |
| 7 | GND | PWR_GND | Ground |
| 8 | OE | PWR_3V3 | Tied to VCCA to enable chip |
| 9 | NC | — | No connection |
| 10 | B4 | — | No connection (unused output) |
| 11 | B3 | — | No connection (unused output) |
| 12 | B2 | — | No connection (unused output) |
| 13 | B1 | Net-(U1-B) | R1 pad 1 (to NeoPixel DIN via 330 Ω) |
| 14 | VCCB | 4V5_NEO | D1 cathode, C5 pad 1 |

## D1 — CDBM1100-G Schottky (MiniSMA) [NEW]

| Terminal | Net | Connected to |
|---|---|---|
| Anode (marked end opposite the stripe) | PWR_5V | XIAO 5V, C2, C3 |
| Cathode (stripe end) | 4V5_NEO | U1 pin 14, J3 pin 2, C5, C6 |

## R1 — 330 Ω 1206 (existing, unchanged)

| Pad | Net | Connected to |
|---|---|---|
| 1 | Net-(U1-B) | U1 pin 13 (B1) |
| 2 | Net-(J3-Pin_1) | J3 pin 1 (NeoPixel DIN) |

## R2, R3, R4 — 10 kΩ 1206 pull-downs [NEW]

| Ref | Pad 1 | Pad 2 | Role |
|---|---|---|---|
| R2 | U1 pin 3 (A2) | PWR_GND | Pulls unused A2 low |
| R3 | U1 pin 4 (A3) | PWR_GND | Pulls unused A3 low |
| R4 | U1 pin 5 (A4) | PWR_GND | Pulls unused A4 low |

## C1 — 100 nF 1206 (existing, unchanged)

| Pad | Net | Role |
|---|---|---|
| 1 | PWR_3V3 | 3V3 decoupling near XIAO |
| 2 | PWR_GND | Ground |

## C2 — 100 nF 1206 (existing, moved to 4V5_NEO)

| Pad | Net | Role |
|---|---|---|
| 1 | 4V5_NEO | NeoPixel rail HF decoupling |
| 2 | PWR_GND | Ground |

## C3 — 100 µF 1206 (existing, moved to 4V5_NEO — serves as bulk for NeoPixel rail)

| Pad | Net | Role |
|---|---|---|
| 1 | PWR_GND | Ground |
| 2 | 4V5_NEO | NeoPixel rail bulk — covers LED current transients |

## C4 — 100 nF 1206 [NEW] — U1 VCCA decoupling

| Pad | Net | Role |
|---|---|---|
| 1 | PWR_3V3 | Place within ~2 mm of U1 pin 1 |
| 2 | PWR_GND | Ground |

## C5 — 100 nF 1206 [NEW] — U1 VCCB decoupling

| Pad | Net | Role |
|---|---|---|
| 1 | 4V5_NEO | Place within ~2 mm of U1 pin 14 |
| 2 | PWR_GND | Ground |

## J1 — 6-pin header (ToF sensors: VL53L4CD + VL6180)

| Pin | Silk | Net | Connected to |
|---|---|---|---|
| 1 | XSHUT | XSHUT | XIAO D6 (GPIO43) |
| 2 | INT | INT | XIAO D0 (GPIO1) |
| 3 | SCL | SCL | XIAO D5 (GPIO6), J4 pin 3 |
| 4 | SDA | SDA | XIAO D4 (GPIO5), J4 pin 4 |
| 5 | 3V3 | PWR_3V3 | XIAO 3V3 |
| 6 | GND | PWR_GND | Ground |

## J3 — 3-pin header (NeoPixel ring)

| Pin | Silk | Net | Connected to |
|---|---|---|---|
| 1 | DIN | Net-(J3-Pin_1) | R1 pad 2 (from U1 B1 via 330 Ω) |
| 2 | VCC | **4V5_NEO** (was PWR_5V) | D1 cathode, U1 VCCB, C2, C3, C5 |
| 3 | GND | PWR_GND | Ground |

## J4 — 4-pin header (OLED I2C display)

| Pin | Silk | Net | Connected to |
|---|---|---|---|
| 1 | GND | PWR_GND | Ground |
| 2 | VDD | PWR_3V3 | XIAO 3V3 |
| 3 | SCL | SCL | XIAO D5, J1 pin 3 |
| 4 | SDA | SDA | XIAO D4, J1 pin 4 |

## SW1 — Tactile button (Omron)

| Pad | Net | Connected to |
|---|---|---|
| 1 | PWR_GND | Ground |
| 2 | Net-(M1-D2) | XIAO D2 (GPIO3) — firmware uses internal pull-up |

## S1 — PEC12R-4015F-S0024 rotary encoder with push-switch

| Pad | Label | Net | Connected to |
|---|---|---|---|
| 1 | Switch | PWR_GND | Ground |
| 2 | Switch | Net-(M1-D3) | XIAO D3 (GPIO4) — encoder push button |
| A | Channel A | Net-(M1-D8) | XIAO D8 (GPIO7) |
| B | Channel B | Net-(M1-D9) | XIAO D9 (GPIO8) |
| C | Common | PWR_GND | Ground |
| SH1 | Shield | PWR_GND | Ground |
| SH2 | Shield | PWR_GND | Ground |

---

## Signal flow for the NeoPixel data path (what the swap changes)

```
XIAO D10 (3.3V logic)
  │
  ▼  Net-(M1-D10)
U1 pin 2 (A1, 3.3V domain)
  │
  ▼  [TXB0104 internal level shift, VCCA=3.3V → VCCB=4.5V]
U1 pin 13 (B1, 4.5V push-pull)
  │
  ▼  Net-(U1-B)
R1 (330 Ω series, edge-rate/reflection damper)
  │
  ▼  Net-(J3-Pin_1)
J3 pin 1 → NeoPixel ring DIN
```

## Power flow (what the swap changes)

```
USB 5V ──┬── PWR_5V ──┬── XIAO pin 14 (5V)
         │            └── D1 anode
         │                     │
         │                     ▼  (Schottky, ~0.5 V drop)
         │                4V5_NEO (~4.5 V) ──┬── U1 pin 14 (VCCB)
         │                                    ├── C5 (100 nF decoupling)
         │                                    ├── C2 (100 nF HF decoupling)
         │                                    ├── C3 (100 µF bulk)
         │                                    └── J3 pin 2 (NeoPixel VCC)
         │
         └── PWR_FLAG (ERC tag only)

XIAO 3V3 pin 12 ── PWR_3V3 ──┬── U1 pin 1 (VCCA)
                              ├── U1 pin 8 (OE, tied HIGH)
                              ├── C1 (100 nF)
                              ├── C4 (100 nF, NEW)
                              ├── J1 pin 5 (ToF VCC)
                              └── J4 pin 2 (OLED VDD)
```

## Quick verification checklist (multimeter, board unpowered)

- [ ] Continuity from **XIAO 3V3** to U1 pin 1, U1 pin 8, C4 pad 1
- [ ] Continuity from **XIAO 5V** to D1 anode (PWR_5V net)
- [ ] Continuity from **D1 cathode** to U1 pin 14, J3 pin 2, C2 pad 1, C3 pad 2, C5 pad 1
- [ ] Continuity from **GND** to U1 pin 7, C1/C2/C3/C4/C5 (GND pads), R2/R3/R4 (GND pads), SW1 pin 1, S1 pin 1/C/SH
- [ ] Continuity **XIAO D10 → U1 pin 2** (NeoPixel data path input)
- [ ] Continuity **U1 pin 13 → R1 pad 1** and **R1 pad 2 → J3 pin 1**
- [ ] **4V5_NEO** NOT shorted to PWR_5V (Schottky must be in between)
- [ ] **4V5_NEO** NOT shorted to PWR_GND
- [ ] U1 pins 6, 9, 10, 11, 12 have no continuity to any other net
- [ ] R2 pad 1 only touches U1 pin 3 (not pins 4 or 5)
- [ ] R3 pad 1 only touches U1 pin 4
- [ ] R4 pad 1 only touches U1 pin 5

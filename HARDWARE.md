# MIDI Guitar — Open Source Hardware Specification

> A gesture-controlled MIDI interface embedded in a Squier Mustang electric guitar. Two time-of-flight sensors convert hand movements into MIDI messages over USB or Bluetooth LE, giving guitarists real-time control over effects in Max/MSP, Ableton, or any MIDI host.

---

## Table of contents

- [Overview](#overview)
- [MIDI specification](#midi-specification)
- [Bill of materials](#bill-of-materials)
- [Pinout reference](#pinout-reference)
- [Net connections](#net-connections)
- [I2C address map](#i2c-address-map)
- [Physical layout](#physical-layout)
- [Firmware](#firmware)
- [Build guide](#build-guide)
- [KiCad project](#kicad-project)
- [License](#license)

---

## Overview

### What it does

Two infrared time-of-flight distance sensors are mounted on the pickguard of a Squier Mustang guitar. A player waves their hand over the sensors to continuously modulate effects — like having a theremin built into the guitar body. The digital MIDI system is completely independent from the guitar's analog audio signal chain.

### Key features

- Dual ToF sensors: long-range arm sweeps + close-range finger gestures
- USB MIDI or BLE MIDI (hardware switch selects)
- CC mode or 14-bit Pitch Bend mode (hardware switch selects)
- Hand velocity tracking as a separate CC axis
- 16-LED NeoPixel ring for visual feedback
- Selectable response curves (linear, logarithmic, exponential)
- LiPo battery support with USB charging
- All parameters configurable via `#define` — no recompilation menu needed

### Signal flow

```
DIGITAL (MIDI control):
  Hand ── VL53L1X (10-500mm) ── ESP32-S3 ── USB/BLE MIDI ── Max/MSP / DAW
  Hand ── VL6180X (5-100mm)  ──┘

ANALOG (guitar audio — fully separate):
  Pickups ── Switches ── Vol/Tone ── Output Jack ── Amp / Interface
```

---

## MIDI specification

### Messages

| MIDI Message | CC/Channel | Source | Input Range | Output Range | Description |
|-------------|------------|--------|-------------|-------------|-------------|
| Control Change 20 | CC20, Ch 1 | VL53L1X distance | 10–500 mm | 0–127 | Main expression: wah, filter, delay mix |
| Control Change 21 | CC21, Ch 1 | VL53L1X velocity | 0–30 mm/frame | 0–127 | Hand speed: resonance, attack, feedback |
| Control Change 22 | CC22, Ch 1 | VL6180X distance | 5–100 mm | 0–127 | Fine control: vibrato depth, subtle modulation |
| Pitch Bend | Ch 1 | VL53L1X distance | 10–500 mm | 0–16383 | 14-bit smooth pitch shifting (mode B) |

### Operating modes

| Mode | Switch position | VL53L1X sends | VL6180X sends |
|------|----------------|---------------|---------------|
| CC (default) | SW2 → 3V3 | CC20 + CC21 | CC22 |
| Pitch Bend | SW2 → GND | Pitch Bend + CC21 | CC22 |

### Transport

| Transport | Switch position | Protocol | Latency |
|-----------|----------------|----------|---------|
| USB MIDI (default) | SW1 → 3V3 | USB 2.0 Full Speed | < 1 ms |
| BLE MIDI | SW1 → GND | Bluetooth LE 5.0 | ~10–20 ms |

BLE device name: `MidiGuitar`

### Signal processing

- **EMA smoothing**: Exponential moving average filter on all distance readings
  - VL53L1X alpha: 0.2 (heavier smoothing for large gestures)
  - VL6180X alpha: 0.3 (lighter smoothing for fast finger movements)
- **Velocity dead zone**: 2.0 mm — ignores sensor noise
- **Velocity saturation**: 30.0 mm/frame → CC 127
- **Response curves** (compile-time selection):
  - `CURVE_LINEAR` — 1:1 mapping (default)
  - `CURVE_LOG` — fast rise, gradual plateau (natural for volume/filter)
  - `CURVE_EXP` — slow start, aggressive end (wah pedal feel)
- **Change detection**: MIDI messages only sent when value changes (no flooding)

---

## Bill of materials

### Core electronics

| Qty | Part | Value/Model | Package | Source | Est. cost | Notes |
|-----|------|-------------|---------|--------|-----------|-------|
| 1 | MCU module | Seeed XIAO ESP32-S3 | 21 x 18 mm | Seeed Studio | $8 | USB-C, BLE 5.0, native USB OTG |
| 1 | ToF sensor (long) | ST VL53L1X breakout | ~13 x 18 mm | SparkFun / Pololu | $12 | I2C, 10–4000 mm range, has XSHUT pin |
| 1 | ToF sensor (close) | ST VL6180X breakout | ~13 x 18 mm | Adafruit / generic | $8 | I2C, 0–200 mm range. Optional |
| 1 | LED ring | WS2812B 16-LED NeoPixel ring | ~45 mm diameter | Adafruit / generic | $8 | Runs on 5V, needs level-shifted data |
| 1 | Level shifter | TI TXB-series (35ATQDX) | SOT / breakout | TI / generic | $1 | 3.3V → 5V on NeoPixel data line |

### Switches

| Qty | Part | Value/Model | Package | Function | Wired to |
|-----|------|-------------|---------|----------|----------|
| 1 | SPDT slide switch | CnK JS102011SCQN | 8.5 x 3.5 mm SMD | SW1: USB / BLE | ESP32 GPIO3 |
| 1 | SPDT slide switch | CnK JS102011SCQN | 8.5 x 3.5 mm SMD | SW2: CC / Pitch Bend | ESP32 GPIO4 |
| 1 | SPDT slide switch | CnK JS102011SCQN | 8.5 x 3.5 mm SMD | SW3: Neck pickup on/off | Analog audio only |
| 1 | SPDT slide switch | CnK JS102011SCQN | 8.5 x 3.5 mm SMD | SW4: Bridge pickup on/off | Analog audio only |

### Power

| Qty | Part | Value/Model | Package | Notes |
|-----|------|-------------|---------|-------|
| 1 | LiPo battery | 3.7V single cell, any capacity | JST-PH | XIAO charges via USB-C. Size limited by cavity |
| 1 | Battery connector | JST-PH 2-pin | Through-hole | On PCB for easy plug/unplug |
| 1 | Voltage regulator | AZ1117I 3.3V 1A | SOT-223 | Clean 3.3V rail |
| 1 | Protection diode | Schottky | SOD-123 | Reverse polarity protection |
| 2 | Decoupling capacitor | 100 nF ceramic | 1206 SMD | Near voltage regulator |

### Connectors and mechanical

| Qty | Part | Notes |
|-----|------|-------|
| 1 | USB-A plug connector | PCB edge mount — MIDI data + power to host laptop |
| 2 | 7-pin female header (2.54 mm) | Socket for XIAO module (allows swap/replacement) |
| 1 | 3-pin female header or JST-XH | Socket for NeoPixel ring cable (allows replacement) |
| 1 | JST-PH 2-pin | Battery connector |
| — | 22–24 AWG hookup wire | Sensor wires, switch wires, XSHUT |

### Not needed

| Part | Why |
|------|-----|
| I2C pull-up resistors | Breakout boards include them |
| MOSFET for LED | NeoPixels have built-in drivers |
| TTP223 touch module | Replaced by slide switches |
| OLED display | Removed — NeoPixel ring provides sufficient feedback |
| Audio codec / I2S DAC | ESP32 handles MIDI only, not audio processing |

---

## Pinout reference

### XIAO ESP32-S3 pin assignments

```
                    XIAO ESP32-S3 (top view, USB-C facing up)
                   ┌──────────────────────┐
                   │        USB-C         │
                   ├──────────────────────┤
         (unused)  │ D0  (GPIO1)    (5V)  │  5V → NeoPixel VCC, LevelShifter VCCB
                   │                      │
  NeoPixel DIN ◄── │ D1  (GPIO2)   (GND)  │  GND bus
                   │                      │
    SW1 common ──► │ D2  (GPIO3)   (3V3)  │  3V3 → sensors, LevelShifter VCCA
                   │                      │
    SW2 common ──► │ D3  (GPIO4)   (D10)  │  (unused)
                   │                      │
     I2C SDA ◄──► │ D4  (GPIO5)    (D9)  │  (unused)
                   │                      │
     I2C SCL ◄──► │ D5  (GPIO6)    (D8)  │  (unused)
                   │                      │
 VL53L1X XSHUT ◄── │ D6  (GPIO43)   (D7)  │  (unused)
                   ├──────────────────────┤
                   │  BAT+          BAT-  │  LiPo battery (underside pads)
                   └──────────────────────┘
```

### Pin detail table

| XIAO Pin | GPIO | Direction | Function | Electrical | Notes |
|----------|------|-----------|----------|------------|-------|
| D1 | GPIO2 | Output | NeoPixel data | 3.3V logic → level shifter → 5V | Through TXB level shifter |
| D2 | GPIO3 | Input | SW1 Transport select | INPUT_PULLUP. HIGH=USB, LOW=BLE | Read once at boot |
| D3 | GPIO4 | Input | SW2 Mode select | INPUT_PULLUP. HIGH=CC, LOW=PitchBend | Read every poll cycle (live) |
| D4 | GPIO5 | Bidir | I2C SDA | 3.3V, 400 kHz, shared bus | VL53L1X + VL6180X |
| D5 | GPIO6 | Bidir | I2C SCL | 3.3V, 400 kHz, shared bus | VL53L1X + VL6180X |
| D6 | GPIO43 | Output | VL53L1X XSHUT | Active LOW = sensor shutdown | Used at boot for I2C address reassignment |
| 5V | — | Power | 5V output | From USB bus | NeoPixel power, level shifter high side |
| 3V3 | — | Power | 3.3V output | Regulated | Sensors, level shifter low side |
| GND | — | Power | Ground | Common | All components |
| BAT+ | — | Power | Battery + | 3.7V LiPo | On underside solder pads |
| BAT- | — | Power | Battery - | Ground | On underside solder pads |

### Unused pins available for expansion

D0 (GPIO1), D7 (GPIO44), D8 (GPIO7), D9 (GPIO8), D10 (GPIO9)

---

## Net connections

### Power distribution

```
USB 5V ─┬── XIAO 5V pin
        ├── NeoPixel VCC
        └── Level shifter VCCB (high side)

XIAO 3V3 ─┬── VL53L1X VCC
           ├── VL6180X VCC
           ├── Level shifter VCCA (low side)
           ├── SW1 throw A
           └── SW2 throw A

GND ─┬── XIAO GND
     ├── VL53L1X GND
     ├── VL6180X GND
     ├── NeoPixel GND
     ├── Level shifter GND
     ├── SW1 throw B
     ├── SW2 throw B
     └── JST battery GND

LiPo BAT+ ── XIAO BAT+ pad
LiPo BAT- ── XIAO BAT- pad
```

### I2C bus (400 kHz, 3.3V)

```
XIAO D4 (SDA) ─┬── VL53L1X SDA
                └── VL6180X SDA

XIAO D5 (SCL) ─┬── VL53L1X SCL
                └── VL6180X SCL
```

No external pull-ups needed — breakout boards include them.

### XSHUT (I2C address management)

```
XIAO D6 ── VL53L1X XSHUT
```

At boot: hold LOW → init VL6180X at 0x29 → release HIGH → reassign VL53L1X to 0x30.

### NeoPixel data (level shifted)

```
XIAO D1 (3.3V) ── Level shifter A1 (input)
                   Level shifter B1 (output) ── NeoPixel DIN (5V)
```

### Digital switches

```
SW1 (transport):     common ── XIAO D2     throw A ── 3V3     throw B ── GND
SW2 (mode):          common ── XIAO D3     throw A ── 3V3     throw B ── GND
```

### Analog switches (guitar audio — NOT connected to ESP32)

```
Neck pickup ── SW3 ─┬── Volume pot ── Tone pot ── Output jack ── Amp
Bridge pickup ── SW4 ─┘
```

---

## I2C address map

| Address | Device | Configuration |
|---------|--------|---------------|
| 0x29 | VL6180X | Default, unchanged |
| 0x30 | VL53L1X | Reassigned at boot via XSHUT sequence |

Both sensors share the same I2C bus (SDA/SCL). The XSHUT pin on the VL53L1X allows the firmware to put it to sleep, initialize the VL6180X first at the default address, then wake and reassign the VL53L1X to a different address.

---

## Physical layout

### Guitar cavity placement

```
    SQUIER MUSTANG BODY (top view, pickguard removed)
    ┌────────────────────────────────────────────┐
    │                                            │
    │   SWITCH CAVITY (40x25mm)                  │
    │   ┌─────────────┐                          │
    │   │ Wiring only │ ◄── route sensor/switch  │
    │   └─────────────┘     wires through here   │
    │                                            │
    │                  ○ VL6180X                  │
    │                  3mm hole in pickguard      │
    │                  (close-range gestures)     │
    │                                            │
    │                                            │
    │                  ○ VL53L1X                  │
    │                  3mm hole in pickguard      │
    │                  (long-range gestures)      │
    │                                            │
    │   CONTROL CAVITY (90x50mm)                 │
    │   ┌────────────────────────────────┐       │
    │   │                                │       │
    │   │  ┌──────────────────┐          │       │
    │   │  │  XIAO ESP32-S3  │          │       │
    │   │  │  (on headers)    │          │       │
    │   │  └──────────────────┘          │       │
    │   │                                │       │
    │   │  Level shifter  JST battery    │       │
    │   │  Regulator      Caps/diode     │       │
    │   │                                │       │
    │   └────────────────────────────────┘       │
    │                                            │
    └────────────────────────────────────────────┘
```

### Pickguard mounting

| Component | Mount method | Hole in pickguard |
|-----------|-------------|-------------------|
| VL53L1X | Hot glue breakout to underside of pickguard | 3–4 mm drill hole, aligned with sensor window |
| VL6180X | Hot glue breakout to underside of pickguard | 3–4 mm drill hole |
| NeoPixel ring | Mounted on top surface or behind translucent section | Cutout or translucent window |
| 4x slide switches | Through slots in pickguard | 4 rectangular slots (~9 x 4 mm each) |
| Sensors wires | Routed under pickguard to control cavity | Small routing channel or existing cavity gap |

### Drilling tips

- Drill from the back side of the pickguard to avoid chipping the top surface
- Start with a 1 mm pilot hole, enlarge to 3–4 mm
- Test sensor readings with serial monitor before permanently gluing
- A slightly larger hole (5 mm) won't affect sensor performance

---

## Firmware

### Versions

| Version | Directory | Features | Lines |
|---------|-----------|----------|-------|
| v1 | `code/tof_midi/` | Single VL53L1X, CC20 only, USB only | ~115 |
| v2 | `code/tof_midi_v2/` | Single sensor, CC20+CC21+PitchBend, USB/BLE, NeoPixel 8-strip | ~460 |
| **v3** | **`code/tof_midi_v3/`** | **Dual sensor, CC20+CC21+CC22+PitchBend, USB/BLE, NeoPixel 16-ring** | **~450** |

### Build instructions

**Arduino IDE settings:**

| Setting | Value |
|---------|-------|
| Board | XIAO_ESP32S3 |
| USB Mode | USB-OTG (TinyUSB) |
| Upload Speed | 921600 |

**Required libraries (install via Library Manager):**

| Library | Author | Used for |
|---------|--------|----------|
| SparkFun VL53L1X | SparkFun | Long-range ToF sensor |
| Adafruit VL6180X | Adafruit | Close-range ToF sensor |
| Adafruit NeoPixel | Adafruit | 16-LED ring |

Built-in (no install needed): Wire, USB, USBMIDI, BLE stack.

### Configuration reference

All parameters are `#define`s at the top of `tof_midi_v3.ino`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `DEBUG` | 1 | Serial output (1=on, 0=off) |
| `MIDI_CH` | 1 | MIDI channel (1–16) |
| `CC_DIST` | 20 | CC number for VL53L1X distance |
| `CC_VEL` | 21 | CC number for VL53L1X velocity |
| `CC_CLOSE` | 22 | CC number for VL6180X distance |
| `BLE_NAME` | "MidiGuitar" | BLE advertising name |
| `DIST_MIN_MM` | 10 | VL53L1X near limit (mm) |
| `DIST_MAX_MM` | 500 | VL53L1X far limit (mm) |
| `CLOSE_MIN_MM` | 5 | VL6180X near limit (mm) |
| `CLOSE_MAX_MM` | 100 | VL6180X far limit (mm) |
| `POLL_MS` | 20 | Sensor poll interval (~50 Hz) |
| `EMA_ALPHA` | 0.2 | VL53L1X smoothing (0=heavy, 1=raw) |
| `EMA_ALPHA_CLOSE` | 0.3 | VL6180X smoothing |
| `VEL_DEAD_MM` | 2.0 | Velocity noise floor (mm) |
| `VEL_MAX_MM` | 30.0 | Velocity saturation point (mm) |
| `CURVE` | CURVE_LINEAR | Response curve (LINEAR, LOG, EXP) |
| `NUM_PIXELS` | 16 | NeoPixel LED count |
| `STRIP_BRIGHT` | 40 | NeoPixel brightness (0–255) |

### Boot sequence

```
1.  Serial init (115200 baud, 2s delay for USB enumeration)
2.  Read SW1 → TRANSPORT_USB or TRANSPORT_BLE
3.  Read SW2 → MODE_CC or MODE_PITCHBEND
4.  Init USB stack (always, for DFU + Serial)
5.  If USB mode: init USBMIDI
6.  If BLE mode: init BLE MIDI service, start advertising
7.  Init I2C bus (SDA=D4, SCL=D5, 400 kHz)
8.  Init NeoPixel ring (brightness 40/255)
9.  Hold VL53L1X XSHUT LOW (sleep)
10. Init VL6180X at default address 0x29
11. Release VL53L1X XSHUT HIGH (wake)
12. Reassign VL53L1X to address 0x30
13. Configure VL53L1X: 20ms timing budget, start ranging
14. Wait for first VL53L1X measurement (200ms timeout)
15. Print status to Serial
16. Enter 50 Hz poll loop
```

### NeoPixel feedback

| State | Color | Pattern |
|-------|-------|---------|
| USB + CC mode | Green | Bar graph (0–16 LEDs = CC value) |
| USB + Pitch Bend | Purple | Bar graph |
| BLE + CC mode | Blue | Bar graph |
| BLE + Pitch Bend | Cyan | Bar graph |
| BLE advertising | Blue | Chase animation (scanning) |
| Sensor error | Red | Flash |

---

## Build guide

### Step 1 — Test on breadboard

Before installing in the guitar, verify everything works on a breadboard:

1. Mount XIAO on breadboard via headers
2. Wire VL53L1X (SDA, SCL, 3V3, GND, XSHUT)
3. Wire NeoPixel ring (5V, GND, DIN through level shifter)
4. Upload v3 firmware
5. Open Serial Monitor at 115200 — should print `Ready`
6. Wave hand over sensor — serial shows CC values, ring lights up green
7. Verify in a MIDI monitor app (or Max/MSP) that CC20/CC21 arrive
8. Add VL6180X (SDA, SCL, 3V3, GND) — verify CC22 appears
9. Wire switches and test mode/transport switching

### Step 2 — Prepare pickguard

1. Remove pickguard from guitar
2. Mark sensor positions (VL53L1X near bridge, VL6180X near neck)
3. Drill 3–4 mm holes from the back side
4. Cut 4 rectangular slots for slide switches (~9 x 4 mm each)
5. Plan NeoPixel ring placement (cutout or translucent window)

### Step 3 — Solder and assemble PCB

1. Solder female headers for XIAO onto PCB
2. Solder level shifter, regulator, diode, caps
3. Solder JST-PH battery connector
4. Solder USB-A connector
5. Solder 4 slide switches directly (no headers)
6. Solder sensor wires (4 wires each + XSHUT for VL53L1X)
7. Solder NeoPixel header/connector (3-pin)

### Step 4 — Install in guitar

1. Hot glue sensor breakouts to underside of pickguard, aligned with holes
2. Mount switches through pickguard slots
3. Place PCB in control cavity
4. Route wires from sensors/switches to PCB
5. Connect NeoPixel ring
6. Connect battery (optional)
7. Reassemble pickguard onto guitar body
8. Upload firmware and test

### Step 5 — Calibrate

1. Open Serial Monitor
2. Move hand through your playing range — note the min/max distance values
3. Adjust `DIST_MIN_MM` / `DIST_MAX_MM` to match your comfortable gesture range
4. Try different `CURVE` settings (LOG feels natural for filter sweeps)
5. Adjust `EMA_ALPHA` if readings feel too laggy (increase) or too jittery (decrease)

---

## KiCad project

| File | Description |
|------|-------------|
| `midiguitar.kicad_pro` | Project settings (KiCad 9) |
| `midiguitar.kicad_sch` | Schematic (components placed, not wired) |
| `midiguitar.kicad_pcb` | PCB layout (empty, not routed) |

Symbol library: `PCM_fab` (custom, included in project).

**Status:** Components are placed on the schematic. Wiring and PCB routing are not yet complete. See [Net connections](#net-connections) for the complete wiring specification.

---

## License

MIT License

Copyright (c) 2026

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.

---

## Contributing

Pull requests welcome. If you build one, open an issue with photos.

## Repository

`https://github.com/Alejo-end/midiguitar`

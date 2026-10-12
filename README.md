# MIDI Guitar

A gesture-controlled MIDI rig in two pieces:

- **Transmitter — the guitar.** A Squier Mustang with a time-of-flight distance sensor near the pickups. Wave your hand over the body and it streams MIDI.
- **Receiver — the dongle ("Heat").** A small custom-PCB box that receives the guitar's MIDI wirelessly, re-emits it as class-compliant USB-MIDI for a host, drives hardware synths over DIN/TRS MIDI, and provides a second gesture zone of its own.

Together they let you play effects and instruments in Max/MSP, Ableton, a Eurorack, or any MIDI gear — with no laptop required in the signal path.

## System architecture — transmitter + receiver

```
        GUITAR  (transmitter)                         RECEIVER DONGLE  ("Heat")
   ┌────────────────────────────┐                ┌─────────────────────────────────┐
   │ VL53L1X ToF (hand distance)│                │ VL53L5CX 8×8 ToF (2nd zone)     │
   │ XIAO ESP32-S3 Sense        │   ESP-NOW      │ XIAO ESP32-S3 Plus              │
   │ OLED · NeoPixel ring · enc │   2.4 GHz      │ OLED · NeoPixel stick · enc     │
   │                            │   5-byte pkt   │                                 │
   │ USB-MIDI · BLE-MIDI        │──── ~2–4 ms ──▶│ USB-MIDI ───────────▶ host      │
   │ (ESP-NOW TX — planned)     │                │ DIN + TRS MIDI ─────▶ synths    │
   └────────────────────────────┘                │ MCP4561 digipot ────▶ EXP pedal │
                                                  └─────────────────────────────────┘
```

The two halves talk over **ESP-NOW** — a connectionless 2.4 GHz Espressif protocol with no pairing dance and no BLE-GATT timestamp gymnastics. One-way latency is ~2–4 ms (vs. ~7–15 ms with jitter for BLE-MIDI). Both ends pin to **WiFi channel 1** and exchange a fixed **5-byte packet**:

```
magic (0xCB) | seq | status | data1 | data2     // __attribute__((packed)), sizeof == 5
```

`magic` rejects RF noise of the right length; `seq` is a rolling counter so the receiver can detect dropped packets via gaps. There is **no Bluetooth on the receiver** — it was removed wholesale in the v6 plan in favour of ESP-NOW.

| | **Transmitter — the guitar** | **Receiver — the dongle ("Heat")** |
|---|---|---|
| Board | XIAO ESP32-S3 Sense | XIAO ESP32-S3 Plus |
| Sensor | VL53L1X single-point ToF | VL53L5CX 8×8 multizone ToF |
| Senses | hand distance over the guitar body | hand gesture over a floor/amp-mounted pad |
| MIDI out | USB-MIDI, BLE-MIDI, (ESP-NOW TX — planned) | USB-MIDI (host), DIN + TRS (synths) |
| Extra out | NeoPixel ring (16), OLED | MCP4561 expression pot, NeoPixel stick (8), OLED |
| Firmware | `code/tof_midi_v5/` | `code/tof_midi_v6/` (full), `code/tof_midi_v6_rx/` (minimal) |

> **Current status.** The receiver's ESP-NOW RX path and its local VL53L5CX gesture zone are working — so the dongle is usable standalone today. The guitar's ESP-NOW **transmit** path is the remaining piece: the stock v5 guitar still sends BLE-MIDI, so the wireless guitar→dongle link stays silent until the guitar-side ESP-NOW TX lands (see `code/tof_midi_v5/CLAUDE.md §4 B5`).

## The guitar (transmitter)

Wave your hand over the VL53L1X sensor mounted near the pickups. The firmware converts distance and hand speed into MIDI CC and pitch bend messages, sent over USB or Bluetooth LE (and, on the v6 roadmap, ESP-NOW to the receiver). A rotary encoder navigates the OLED menu; a bypass button mutes output; a DIP switch selects the default transport. The sections below document the guitar in detail.

## MIDI output

| Message    | Default CC | Source        | Use case                                  |
|------------|-----------|---------------|-------------------------------------------|
| CC         | CC20      | Hand distance | Continuous modulation (wah, filter, delay) |
| CC         | CC21      | Hand velocity | Speed-based effects (resonance, attack)    |
| Pitch Bend | --        | Hand distance | 14-bit smooth pitch shifting               |

MIDI channel, CC numbers, distance range, and response curve are all adjustable at runtime via the Settings page. Changes persist across power cycles (saved to flash).

## Hardware

| Component | Part | Purpose |
|-----------|------|---------|
| MCU | Seeed XIAO ESP32-S3 | USB-OTG + BLE 5.0 |
| Sensor | VL53L1X (or VL53L4CD) | I2C time-of-flight, 10-500mm |
| Display | SSD1306 128x64 OLED | 4-page Elektron-style UI |
| Encoder | Rotary w/ push button | Page navigation + parameter control |
| LED ring | 16x WS2812B NeoPixel | Color-coded distance bar + animations |
| Bypass btn | Momentary push button | Mute MIDI output |
| Edit btn | Momentary push button | Toggle edit mode on Settings page |
| Transport SW | SPDT slide switch | USB / BLE default at boot |
| Pickup SW (x2) | SPDT slide switches | Neck / Bridge on/off (analog, not GPIO) |
| Battery | LiPo (optional) | Charges via XIAO built-in circuit |

## Wiring

### GPIO pinout

| Function       | XIAO Pin | GPIO  | Notes |
|----------------|----------|-------|-------|
| I2C SDA        | D4       | 5     | Shared: OLED + VL53L1X |
| I2C SCL        | D5       | 6     | Shared: OLED + VL53L1X |
| NeoPixel DIN   | D1       | 2     | 16-LED ring, data in |
| Transport SW   | D2       | 3     | DIP switch, INPUT_PULLUP |
| Bypass button  | D3       | 4     | Momentary, INPUT_PULLUP |
| Encoder A      | D8       | 7     | Quadrature channel A |
| Encoder B      | D9       | 8     | Quadrature channel B |
| Encoder push   | D10      | 9     | Encoder built-in button |
| VL53L1X XSHUT  | D6       | 43    | Sensor reset/address set |
| Edit button    | D7       | 44    | Momentary, INPUT_PULLUP |

### I2C bus (400kHz)

| Device   | Address |
|----------|---------|
| SSD1306 OLED | 0x3C |
| VL53L1X      | 0x30 (remapped from 0x29 via XSHUT) |

### Button wiring

All buttons use the internal pullup resistor (INPUT_PULLUP). No external resistor required:

```
GPIO pin ──── ┤ ├ ──── GND
             button
```

## Firmware versions

The firmwares in `code/` split along the transmitter/receiver line.

**Transmitter — the guitar:**

| Version | Folder | Description |
|---------|--------|-------------|
| v4 | `tof_midi_v4/` | Single device: full OLED UI, encoder menu, settings page, NVS persistence, animations |
| **v5** | **`tof_midi_v5/`** | **Adds WiFi web-config UI, 8 presets, boot self-test, free-heap monitor — USB + BLE MIDI** |

**Receiver — the dongle ("Heat"):**

| Version | Folder | Description |
|---------|--------|-------------|
| v6 (rx) | `tof_midi_v6_rx/` | Minimal, works-on-first-flash: ESP-NOW → USB-MIDI + local VL53L5CX 8×8 + OLED link diagnostics |
| **v6 (full)** | **`tof_midi_v6/`** | **Full Heat dongle: adds DIN/TRS MIDI, MCP4561 expression out, DRONE/RHYTHM/DRUMS routing, NVS presets, WiFi config** |

Each device's folder has its own `README.md` with the deep detail; the sections below summarise the guitar's v4 UI (largely carried into v5) and the receiver dongle.

### v4 features

- 4-page OLED UI (Elektron Digitakt-inspired)
- Rotary encoder navigation with long-press bypass
- Runtime-adjustable settings saved to flash (NVS)
- NeoPixel gradient arc with velocity pulse and mode-change wipe
- Bypass breathing animation and BLE connection flash
- Synchronized OLED + NeoPixel startup animation
- Sensor error recovery (retries, graceful degradation)
- OLED screensaver after 60s inactivity
- Hardware watchdog (5s auto-reset on hang)
- DIP switch live-read (no reboot needed)
- Toast overlay notifications on setting changes

## User interface

### Controls

| Input | Action |
|-------|--------|
| Encoder rotate | Adjust setting on current page |
| Encoder short-press (<500ms) | Next page |
| Encoder long-press (>500ms) | Toggle bypass (mute) |
| Bypass button | Toggle bypass (mute) |
| Edit button | Enter/exit edit mode on Settings page |
| Transport DIP switch | Set default transport (read at boot + live) |

### OLED pages

Navigate pages with encoder push. The active page is shown by a tall white block in the page indicator bar at the bottom; inactive pages show thin dashes.

#### Page 1 -- Live

Real-time sensor data. Encoder rotation adjusts LED brightness.

```
┌────────────────────────────────────────┐
│▌USB▐ ▌CC▐ Ch1              ▌MUTE▐     │
│────────────────────────────────────────│
│DST ▓▓▓▓▓▓▓▓▓▓▓▓ ▓▓▓ ░ ░ ░ ░    102  │
│────────────────────────────────────────│
│VEL ▓▓▓ ░ ░ ░ ░ ░ ░ ░ ░ ░ ░ ░    38  │
│────────────────────────────────────────│
│CC20  CC21                  245mm       │
│────────────────────────────────────────│
│▓▓▓ ▓▓▓ ▓▓▓ ▓▓▓ ▓▓▓ ░ ░ ░ ░ ░ ░ ░   │
│              █ _ _ _                   │
└────────────────────────────────────────┘
```

- **Status bar**: Transport badge, mode badge, channel, BLE indicator, mute/error badge
- **DST/BND bar**: Segmented distance bar + numeric value
- **VEL bar**: Segmented velocity bar + numeric value
- **Info row**: CC numbers (or bend value) + raw distance in mm
- **Activity meter**: Full-width segmented bar with quarter-mark ticks
- When muted: bars show diagonal stripes, meter shows dashed outline

#### Page 2 -- Transport

Select MIDI output. Encoder rotation cycles USB / BLE / Both.

```
┌────────────────────────────────────────┐
│█████████████████████████████████████████│
│█       OUTPUT                        █│
│█████████████████████████████████████████│
│ ╭────────╮  ╭────────╮  ╭────────╮    │
│ │  USB   │  │███BLE██│  │  BOTH  │    │
│ │  icon  │  │██icon██│  │  icon  │    │
│ │        │  │████████│  │        │    │
│ ╰────────╯  ╰████████╯  ╰────────╯    │
│              _ █ _ _                   │
└────────────────────────────────────────┘
```

#### Page 3 -- Mode

Select CC or Pitch Bend mode. Encoder rotation toggles.

```
┌────────────────────────────────────────┐
│█████████████████████████████████████████│
│█       MODE                          █│
│█████████████████████████████████████████│
│ ╭──────────────────╮ ╭──────────────╮  │
│ │██ staircase ████ │ │   arrows     │  │
│ │██ waveform █████ │ │   ↑ ─── ↓   │  │
│ │█████████CC██████ │ │    BEND      │  │
│ ╰██████████████████╯ ╰──────────────╯  │
│              _ _ █ _                   │
└────────────────────────────────────────┘
```

#### Page 4 -- Settings

Editable parameter list. Encoder rotation moves cursor. Press edit button to enter edit mode (value blinks with `< >` arrows), rotate to change value, press edit button again to confirm. Settings auto-save to flash 2 seconds after the last change.

```
┌────────────────────────────────────────┐
│█████████████████████████████████████████│
│█   SETTINGS                    EDIT  █│
│█████████████████████████████████████████│
│ CC Dist                           20  │
│ CC Vel                            21  │
│█Curve████████████████████████<LIN>████│
│ LED Brt                           40  │
│ Invert                           OFF  │
│              _ _ _ █                   │
└────────────────────────────────────────┘
```

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| MIDI Ch | 1--16 | 1 | MIDI channel for all output |
| CC Dist | 0--127 | 20 | CC number for distance |
| CC Vel | 0--127 | 21 | CC number for velocity |
| Dist Min | 5--200 mm | 10 | Sensor near threshold |
| Dist Max | 100--1000 mm | 500 | Sensor far threshold |
| Curve | LIN / LOG / EXP | LIN | Response curve shape |
| LED Brt | 5--255 | 40 | NeoPixel ring brightness |
| Invert | OFF / ON | OFF | Flip distance mapping (closer = higher value) |

### Toast notifications

Brief centered overlay messages appear for 500ms on any page when settings change:

| Event | Toast text |
|-------|-----------|
| Bypass on | `MUTED` |
| Bypass off | `ACTIVE` |
| Transport change | `USB` / `BLE` / `BOTH` |
| Mode change | `CC` / `BEND` |
| Sensor failure | `NO SENSOR` |

## NeoPixel ring

16-LED WS2812B ring with color-coded animations:

| State | Animation | Color |
|-------|-----------|-------|
| USB + CC mode | Gradient arc (bright tip, dim tail) | Green |
| USB + Pitch Bend | Gradient arc | Purple |
| BLE + CC mode | Gradient arc | Blue |
| BLE + Pitch Bend | Gradient arc | Cyan |
| USB+BLE + CC | Gradient arc | Teal |
| USB+BLE + PB | Gradient arc | Violet |
| Bypassed (muted) | Sine-wave breathing | Amber |
| BLE scanning | Two-LED chase spinner | Blue |
| BLE connected | Full ring flash (200ms) | Blue |
| Mode/transport change | Clockwise wipe (150ms) | New mode color |
| High velocity (>100) | Leading LED flash | White |
| Sensor error | Alternating blink | Red |

### Startup sequence

The OLED and NeoPixel ring play a coordinated 4-phase boot animation:

1. **Line sweep** -- horizontal line draws across OLED center; single LED spins around ring
2. **Box expand** -- rounded rectangle grows from center line; ring fills outward with gradient
3. **Typewriter** -- "M I D I G U I T A R" types in character by character; ring pulses gently
4. **Fade out** -- black bars close from top and bottom; ring fades to black

## Sensor processing

```
VL53L1X raw distance (mm)
    │
    ▼
EMA smoothing (alpha = 0.2)
    │
    ▼
Normalize to 0.0--1.0 (distMin → distMax range)
    │
    ▼
Apply curve (linear / log / exp)
    │
    ▼
Optional invert (1.0 - value)
    │
    ├──► CC mode:  scale to 0--127, send CC
    └──► PB mode:  scale to 0--16383, send Pitch Bend

Velocity = |delta EMA| per poll cycle
    deadzone: < 2mm → 0
    cap: 30mm → 127
```

## Reliability

- **Sensor recovery**: If VL53L1X fails to initialize, retries 3 times before falling back to display-only mode (OLED and ring still work, MIDI is disabled). An `ERR` badge appears on the Live page.
- **Watchdog**: 5-second hardware watchdog timer auto-resets the ESP32 on hang.
- **Screensaver**: OLED blanks after 60 seconds of inactivity. Wakes on encoder, button, or sensor movement.
- **Flash persistence**: Settings saved to ESP32 NVS with 2-second write debounce to prevent flash wear from rapid encoder scrolling.
- **DIP live-read**: Transport DIP switch is polled every 200ms in the main loop -- no reboot needed to change transport.

## The receiver dongle ("Heat")

A small box on a custom-milled single-sided PCB, built around a XIAO ESP32-S3 **Plus**. It does the inverse of the guitar: it **receives** the guitar's MIDI over ESP-NOW and re-emits it as **class-compliant USB-MIDI** to a host — and it carries its **own VL53L5CX 8×8 multizone ToF** as a second gesture zone (a floor- or amp-mounted pad), fused into the same MIDI stream. (The dongle was developed as "Heat", a Tangible Music Lab "Out of Control" submission.)

### Hardware

| Component | Part | Bus / pin |
|-----------|------|-----------|
| MCU | XIAO ESP32-S3 Plus | — |
| Sensor | VL53L5CX 8×8 multizone ToF | I²C 0x29 |
| Display | SSD1306 128×64 OLED | I²C 0x3C |
| Expression out | MCP4561 single 10kΩ I²C digital pot (PDIP-8) | I²C 0x2E |
| MIDI out | 5-pin DIN + 3.5mm TRS Type A (shared 2×220 Ω) | Serial1 TX, GPIO43 |
| Visual | NeoPixel stick (8 px) via 3.3→5V level shifter | GPIO9 (D10) |
| Controls | EC11 encoder + mute + mode buttons | GPIO7/8/4, D0/D1 |

I²C bus runs at 400 kHz: `0x29` VL53L5CX, `0x2E` MCP4561, `0x3C` OLED.

### Outputs

- **USB-MIDI** — class-compliant (TinyUSB); the guitar's stream plus the dongle's local gesture zone, merged.
- **DIN + TRS MIDI** — 31250 baud on GPIO43 (Serial1 TX), driving both the 5-pin DIN jack and the 3.5mm TRS Type A jack through a shared 2×220 Ω network.
- **Expression** — one MCP4561 digital pot at 0x2E → 1/4″ stereo jack, emulating an expression pedal into other gear's EXP input (Casio Dimension-Shifter style). The wiper tracks a selectable ToF feature: distance / velocity / centroid-X / coverage, with optional invert.

### Modes & controls

The full firmware adds a **DRONE / RHYTHM / DRUMS** note router (continuous CCs stream in all three modes — only note triggering differs), cycled with the mode button. The mode button also selects the sensor source (**BOTH → GUITAR → LOCAL**). Bypass/panic is on the encoder tap and the mute button — there is no footswitch. Triple-tapping the encoder opens a WiFi config AP with live readouts, 8 presets, and on-stage routing thresholds (trigger / release / vel-floor / gate-ms).

### Two firmwares

- **`code/tof_midi_v6_rx/`** — focused, works-on-first-flash: ESP-NOW → USB-MIDI passthrough + local VL53L5CX + OLED link diagnostics. Deliberately omits the Heat extras (digipot, DIN/TRS, mode router, web config) so the core link + sensor + display can be confirmed first.
- **`code/tof_midi_v6/`** — the full Heat dongle, folding those extras back in. Pin-compatible with `v6_rx` so both firmwares run on the same board.

The full standalone Heat PlatformIO project (with its own `README.md`, `ARCHITECTURE.md`, and KiCad) lives under `code/tof_midi_v6_rx/heat/`.

## Building

### Guitar — Arduino IDE

1. Board: **XIAO_ESP32S3**
2. USB Mode: **USB-OTG (TinyUSB)**
3. Install libraries:
   - SparkFun VL53L1X
   - Adafruit NeoPixel
   - Adafruit SSD1306
   - Adafruit GFX
4. Upload

### Guitar — Arduino CLI

```bash
# v5 (latest guitar firmware) — swap in tof_midi_v4/ for the older single-device build
arduino-cli compile -b esp32:esp32:XIAO_ESP32S3 \
  --build-property "build.extra_flags=-DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT=0" \
  code/tof_midi_v5/

arduino-cli upload -b esp32:esp32:XIAO_ESP32S3 -p /dev/cu.usbmodem* \
  code/tof_midi_v5/
```

### Receiver dongle

The `.ino` sketches (`tof_midi_v6_rx/`, `tof_midi_v6/`) build the same way under Arduino — board **XIAO_ESP32S3**, USB-OTG (TinyUSB), `ARDUINO_USB_MODE=0`. Libraries: SparkFun VL53L5CX, Adafruit NeoPixel, Adafruit SSD1306, Adafruit GFX (the MCP4561 digipot is driven over raw `Wire`; ESP-NOW + USB-MIDI ship with the ESP32 core).

The full standalone Heat project under `code/tof_midi_v6_rx/heat/` uses PlatformIO instead:

```bash
pio run -d code/tof_midi_v6_rx/heat/heat                 # build
pio run -d code/tof_midi_v6_rx/heat/heat -t upload       # flash
```

## PCB

Two KiCad projects:

- **Guitar:** `finalMidiGuitar/finalMidiGuitar.kicad_{sch,pcb,pro}`. See `HARDWARE.md` for the full hardware specification.
- **Receiver dongle ("Heat"):** `code/tof_midi_v6_rx/heat/kicad/heat.kicad_{sch,pcb,pro}`, with `BOM.md` (authoritative parts list) and `PCB_WIRING.md` (pin-by-pin wiring) alongside.

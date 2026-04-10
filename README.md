# MIDI Guitar

A time-of-flight distance sensor embedded in a Squier Mustang electric guitar, turning hand gestures into MIDI messages for controlling effects in Max/MSP, Ableton, or any MIDI host.

## How it works

Wave your hand over the VL53L1X sensor mounted near the pickups. The firmware converts distance and hand speed into MIDI CC and pitch bend messages, sent over USB or Bluetooth LE. A rotary encoder navigates a 4-page OLED menu; a bypass button mutes output; a DIP switch selects the default transport.

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

Four versions in `code/`, each building on the last:

| Version | Folder | Description |
|---------|--------|-------------|
| v1 | `tof_midi/` | Minimal: distance to CC20 over USB |
| v2 | `tof_midi_v2/` | Adds velocity CC, pitch bend, BLE, NeoPixel bar |
| v3 | `tof_midi_v3/` | Adds OLED display |
| **v4** | **`tof_midi_v4/`** | **Full UI: encoder menu, settings page, NVS persistence, animations** |

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

## Building

### Arduino IDE

1. Board: **XIAO_ESP32S3**
2. USB Mode: **USB-OTG (TinyUSB)**
3. Install libraries:
   - SparkFun VL53L1X
   - Adafruit NeoPixel
   - Adafruit SSD1306
   - Adafruit GFX
4. Upload

### Arduino CLI

```bash
arduino-cli compile -b esp32:esp32:XIAO_ESP32S3 \
  --build-property "build.extra_flags=-DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT=0" \
  code/tof_midi_v4/

arduino-cli upload -b esp32:esp32:XIAO_ESP32S3 -p /dev/cu.usbmodem* \
  code/tof_midi_v4/
```

## PCB

KiCad 9 project files in the root directory (`midiguitar.kicad_sch`, `midiguitar.kicad_pcb`). See `HARDWARE.md` for the full hardware specification and `PCB_BUILD_PLAN.md` for the build guide with BOM.

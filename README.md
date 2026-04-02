# MIDI Guitar

A time-of-flight distance sensor embedded in a Squier Mustang electric guitar, turning hand gestures into MIDI messages for controlling effects in Max/MSP, Ableton, or any MIDI host.

## How it works

Wave your hand over the VL53L1X sensor mounted near the pickups. The firmware converts distance and hand speed into MIDI CC and pitch bend messages, sent over USB or Bluetooth LE.

## MIDI output

| Message    | Source        | Use case                         |
|------------|---------------|----------------------------------|
| CC20       | Hand distance | Continuous modulation (wah, filter, delay) |
| CC21       | Hand velocity | Speed-based effects (resonance, attack)    |
| Pitch Bend | Hand distance | 14-bit smooth pitch shifting               |

Touch tap cycles between CC and Pitch Bend mode. Slide switch selects USB or BLE transport.

## Hardware

- **Seeed XIAO ESP32-S3** — microcontroller (USB + BLE)
- **VL53L1X / VL53L4CD** — I2C time-of-flight distance sensor
- **8x WS2812B NeoPixel strip** — visual feedback (bar graph + status)
- **SPDT slide switch** — USB / BLE mode select
- **Copper touch pad** — mode cycling via ESP32 native capacitive touch
- **LiPo battery** — optional, charges via XIAO's built-in circuit

## Wiring

| Function      | XIAO Pin | GPIO  |
|---------------|----------|-------|
| I2C SDA       | D4       | GPIO5 |
| I2C SCL       | D5       | GPIO6 |
| Touch pad     | D0       | GPIO1 |
| NeoPixel DIN  | D1       | GPIO2 |
| Switch        | D2       | GPIO3 |
| Battery       | BAT+/BAT-| —    |

## Firmware

Two versions in `code/`:

- **`tof_midi/`** — v1, basic: distance → CC20 over USB
- **`tof_midi_v2/`** — v2, full: distance + velocity + pitch bend, USB/BLE, NeoPixel, touch, response curves

### Building (Arduino IDE)

1. Board: **XIAO_ESP32S3**
2. USB Mode: **USB-OTG (TinyUSB)**
3. Install libraries: **SparkFun VL53L1X**, **Adafruit NeoPixel**
4. Upload

### Configuration

All tunable parameters are `#define`s at the top of the sketch:

- `MIDI_CH` — MIDI channel (1–16)
- `CC_DIST` / `CC_VEL` — CC numbers
- `DIST_MIN_MM` / `DIST_MAX_MM` — sensor range mapping
- `EMA_ALPHA` — smoothing (0 = heavy, 1 = raw)
- `CURVE` — response curve (`CURVE_LINEAR`, `CURVE_LOG`, `CURVE_EXP`)
- `TOUCH_PIN` / `STRIP_PIN` / `SWITCH_PIN` — set to -1 to disable

## NeoPixel feedback

| State             | Pattern                       |
|-------------------|-------------------------------|
| USB + CC          | Green bar graph               |
| USB + Pitch Bend  | Purple bar graph              |
| BLE + CC          | Blue bar graph                |
| BLE + Pitch Bend  | Cyan bar graph                |
| BLE advertising   | Blue chase animation          |
| Sensor error      | Red flash                     |

## PCB

KiCad 9 project files in the root directory. The schematic includes the XIAO module, voltage regulator, slide switch, USB-A connector, and passive components. PCB layout is not yet routed.

## License

MIT

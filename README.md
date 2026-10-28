# MIDI Guitar

A time-of-flight distance sensor embedded in a Squier Mustang electric guitar, turning hand gestures into MIDI messages for controlling effects in Max/MSP, Ableton, or any MIDI host.

## How it works

The system has two halves:

- **Guitar** (`code/tof_midi_v5/`): a VL53L1X sensor near the pickups measures hand distance. The firmware turns distance and hand speed into MIDI CC or 14-bit pitch bend, sends it over USB-MIDI, and broadcasts it over ESP-NOW (WiFi channel 1) to the receiver. A rotary encoder drives a 4-page OLED menu; a 16-LED NeoPixel ring mirrors the values.
- **Receiver dongle** (`code/tof_midi_v6_rx/`): receives the guitar's ESP-NOW stream and re-emits it as USB-MIDI and on a 5-pin DIN MIDI OUT. It also has its own VL53L5CX 8×8 sensor as a second gesture zone, an expression-pedal input and MUTE / HOLD footswitches.

## Firmware

| Folder | Runs on | Status |
|--------|---------|--------|
| **`code/tof_midi_v5/`** | Guitar (`finalMidiGuitar` PCB) | **Current.** USB-MIDI + ESP-NOW TX, 4-page UI, NVS settings, 8 presets, WiFi config page. See its [README](code/tof_midi_v5/README.md). |
| **`code/tof_midi_v6_rx/`** | Receiver dongle | **Current.** ESP-NOW RX → USB + DIN MIDI, local VL53L5CX, expression pedal, footswitches, pairing. |
| `code/tof_midi_v6/` | Receiver dongle | Experimental "Heat" build: mode router (DRONE / RHYTHM / DRUMS), MCP4561 expression output, presets, web config. Untested on hardware; ignores the dongle's expression input and HOLD footswitch. |
| `code/tof_midi_v4/` | Older guitar wiring | Legacy (BLE-MIDI, old pin map). Does **not** match the current PCB. |
| `code/debug/`, `code/debug_oled/` | — | Bring-up diagnostics. `debug/` still uses the old pin map. |

## Wiring

XIAO ESP32-S3 pad → GPIO: `D0=1 D1=2 D2=3 D3=4 D4=5 D5=6 D6=43 D7=44 D8=7 D9=8 D10=9`

### Guitar (v5, `finalMidiGuitar` PCB)

| Function | XIAO pad | GPIO | Notes |
|----------|----------|------|-------|
| I2C SDA | D4 | 5 | OLED (0x3C) + VL53L1X (0x29), 400 kHz |
| I2C SCL | D5 | 6 | |
| NeoPixel data | D10 | 9 | Through level shifter U1 to the 16-LED ring |
| Encoder A | D8 | 7 | |
| Encoder B | D9 | 8 | |
| Encoder push | D3 | 4 | To GND, INPUT_PULLUP. All gestures are on this push. |
| SW1 | D2 | 3 | Populated but unused (reads stuck LOW on this PCB) |

The VL53L1X stays at its default address 0x29; the board has no XSHUT or INT trace.

### Receiver dongle (v6_rx, rev 2026-10)

| Function | XIAO pad | GPIO | Notes |
|----------|----------|------|-------|
| I2C SDA | D4 | 5 | OLED (0x3C) + VL53L5CX (0x29) [+ MCP4561 0x2E for v6] |
| I2C SCL | D5 | 6 | |
| NeoPixel data | D10 | 9 | Through level shifter to the 8-LED stick |
| Encoder A / B | D8 / D9 | 7 / 8 | |
| Encoder push | D3 | 4 | |
| MODE button S3 | D0 | 1 | To GND |
| MUTE button S4 + footswitch tip | D1 | 2 | To GND |
| Expression pedal wiper | D2 | 3 | ADC, internal pull-down |
| MIDI OUT (DIN) | D6 | 43 | Serial1 TX, 31250 baud |
| HOLD footswitch (ring) | D7 | 44 | To GND |

## Building

Arduino IDE or `arduino-cli`, board **XIAO_ESP32S3**, arduino-esp32 core 3.x.

- **USB Mode: USB-OTG (TinyUSB)** — required for USB-MIDI.
- **USB CDC On Boot: Enabled** for the dongle sketches. With it disabled, `Serial` runs on UART0, whose pins (GPIO 43/44) are the dongle's MIDI OUT and HOLD footswitch.

Libraries (Library Manager): Adafruit NeoPixel, Adafruit SSD1306, Adafruit GFX, plus **SparkFun VL53L1X** for the guitar or **SparkFun VL53L5CX** for the dongle.

```bash
arduino-cli compile -b esp32:esp32:XIAO_ESP32S3:USBMode=default,CDCOnBoot=cdc code/tof_midi_v5/
```

```bash
arduino-cli compile -b esp32:esp32:XIAO_ESP32S3:USBMode=default,CDCOnBoot=cdc code/tof_midi_v6_rx/
```

After editing `code/tof_midi_v5/web_ui.html`, regenerate the embedded page with `code/tof_midi_v5/tools/build_web_ui.sh`.

## PCB

The guitar board is the KiCad project in `finalMidiGuitar/` (`finalMidiGuitar.kicad_sch`, `finalMidiGuitar.kicad_pcb`), with a STEP export and F.Cu + drill gerbers. `HARDWARE.md` documents an earlier iteration of the design and is out of date.

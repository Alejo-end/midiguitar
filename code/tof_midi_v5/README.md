# MIDI Guitar — v5

A Squier Mustang turned into a gesture-controlled MIDI instrument.
Wave your hand over the body and a distance sensor turns the motion
into MIDI. Plug it in over USB or pair it as a Bluetooth MIDI device
and play any soft synth.

## What's in the box

- **XIAO ESP32-S3 Sense** — the brain
- **VL53L1X ToF sensor** — reads hand distance (10–500 mm)
- **NeoPixel ring (16 LEDs)** — visual feedback
- **OLED 128×64** — small status screen
- **Rotary encoder + push** — main navigation
- **Tactile button (SW1)** — context-sensitive action
- **Custom milled PCB** — wires it all to the XIAO

All on the body of the guitar. USB-C for power + MIDI; battery is not
required.

## What it does

1. The ToF sensor measures how far your hand is from the body, ~50
   times per second.
2. That distance gets mapped to MIDI:
   - **CC mode** → MIDI Control Change (default CC20 distance, CC21 velocity)
   - **PB mode** → 14-bit Pitch Bend
3. The values are sent over **USB-MIDI**, **Bluetooth MIDI**, or both.
4. The OLED shows live values, the LED ring mirrors them, and the
   encoder/button let you tweak settings on the fly.

## How to use it

### Powering on
Plug USB-C in. After a short intro animation it lands on the **LIVE**
page, ready to play.

### The five pages
Press the encoder to cycle pages:

| Page         | What it shows / does                          |
|--------------|-----------------------------------------------|
| **LIVE**     | Distance bar, velocity bar, raw mm, mode info |
| **OUTPUT**   | Pick USB, BLE, or both                        |
| **MODE**     | Pick CC or BEND                               |
| **SETTINGS** | Eight values: MIDI channel, CC numbers, distance min/max, curve, LED brightness, invert |
| **PRESETS**  | 8 saved snapshots — load on the fly, save the current setup |

Turn the encoder to change values. On SETTINGS, press SW1 to enter
edit mode for the highlighted row.

### The button (SW1)
- **Tap on LIVE** → mute/unmute (bypass)
- **Tap on OUTPUT/MODE** → cycle to the next option
- **Tap on SETTINGS** → toggle edit on the highlighted row
- **Tap on PRESETS** → load the highlighted slot
- **Hold ≥1 s on PRESETS** → save current settings to the highlighted slot
- **Triple-tap from anywhere** → enter WiFi config mode

### Long-press the encoder
Mute / unmute regardless of which page you're on.

## The web UI

Triple-tap SW1 → the device starts a WiFi network called
**MidiGuitar-Config** (password `12345678`). Connect with your phone
and open <http://192.168.4.1>.

You'll see a dark, mobile-friendly page with every setting on it,
plus a live monitor and 8 preset slots with `LOAD` / `SAVE` buttons.
Tap a control, change it, that's it — the device saves automatically.
Triple-tap SW1 again to exit and turn WiFi off.

The page works without internet because the ESP32 is the access point.
Everything (HTML, CSS, JS) is served from the device itself.

## Presets

Eight slots store snapshots of every persisted setting (MIDI channel,
CC numbers, distance bounds, curve, brightness, invert). Use them to
switch between sounds/songs without re-tweaking.

**On the device** (PRESETS page):
- Encoder selects the slot.
- Tap SW1 to load.
- Hold SW1 for 1 second to save current settings to the highlighted slot.
- Filled slots have a solid border; empty slots have a dotted border.

**On the phone** (web UI):
- Tap a slot to select it.
- Tap `LOAD` to apply, or `SAVE` to overwrite the slot with the
  current settings.
- Filled slots are bright; empty slots are dimmed.

## Settings reference

| Setting     | Range / choices    | What it does                   |
|-------------|-------------------|--------------------------------|
| MIDI Ch     | 1–16              | Output channel                 |
| CC Dist     | 0–127             | Control Change number for distance |
| CC Vel      | 0–127             | Control Change number for velocity |
| Dist Min    | 5–500 mm          | Distance that maps to value 0  |
| Dist Max    | 50–2000 mm        | Distance that maps to value max |
| Curve       | Linear / Log / Exp | Response shape                 |
| LED Brt     | 5–255             | NeoPixel ring brightness       |
| Invert      | ON / OFF          | Reverse the mapping (close = high) |

Settings persist across reboots (saved to flash 2 seconds after the
last change).

## Building & flashing

### Requirements
- Arduino IDE (or `arduino-cli`)
- ESP32 board package (Espressif Arduino-ESP32 v3.x)
- Board: **XIAO_ESP32S3**, USB mode: **USB-OTG (TinyUSB)**

### Libraries
- SparkFun VL53L1X
- Adafruit NeoPixel
- Adafruit GFX
- Adafruit SSD1306

### Compile
```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 tof_midi_v5
```
Then flash via Arduino IDE or `arduino-cli upload`.

## Files in this folder

```
tof_midi_v5.ino           Main sketch — sensor, MIDI, UI, web, presets
web_ui.html               Canonical config page — open in a browser to preview
web_ui_html.h             Generated from web_ui.html; do not edit by hand
tools/build_web_ui.sh     Regenerates web_ui_html.h from web_ui.html
README.md                 This file
CLAUDE.md                 Engineering rules + roadmap
WEB_UI_PLAN.md            Design doc for the web UI
```

After editing `web_ui.html`, run `tools/build_web_ui.sh` to regenerate
the header before flashing.

## Tips & caveats

- **WiFi config mode disables Bluetooth-grade timing.** WiFi and BLE
  share the same radio. Don't keep config mode open while playing
  over BLE — it'll glitch. Triple-tap to exit when you're done.
- **First boot might show "NO SENSOR".** Check the ribbon cable; the
  VL53L1X retries 3 times at boot.
- **USB MIDI shows up as "MidiGuitar"** on the host.
- **Bluetooth pairing**: in iOS/macOS use the Audio MIDI Setup app's
  MIDI Studio → Bluetooth Configuration. Android needs a BLE-MIDI
  capable app.
- All persisted settings (channel, CCs, distance bounds, curve,
  brightness, invert, transport, mode) come back on reboot. Presets
  also include mode.

## Known limits

- Single sensor (a second VL53L1X is on the roadmap).
- No on-device factory reset yet — re-flashing wipes NVS if needed.
- Web config mode requires triple-tap; there's no physical "config"
  switch.

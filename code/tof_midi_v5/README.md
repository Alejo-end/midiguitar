# REINS guitar firmware (v5)

This is the firmware for the guitar side of REINS. A distance sensor under the pickguard measures how far your hand is from the body. The firmware turns that into MIDI. It sends every message over USB MIDI and over ESP-NOW radio to the REINS receiver box.

## Hardware

- **XIAO ESP32-S3 Sense**: the microcontroller.
- **VL53L1X ToF sensor**: reads the hand distance, 10 to 500 mm by default.
- **LED ring, 16 WS2812B**: shows the value.
- **OLED, 128 x 64**: status screen.
- **Rotary encoder with push**: settings and pages.
- **Button SW1 on D1**: mute on and off, on every page.
- **Battery on D0**: the OLED shows `LOW BAT` under 3.45 V.
- **Board**: the REINS guitar board in `../../reins-guitar/`. On the older milled board, set `BTN_PIN` and `BAT_PIN` to -1.

## What it does

1. The sensor measures the hand distance every 20 ms.
2. The distance becomes MIDI. **CC mode** sends CC 20 (distance) and CC 21 (velocity). **BEND mode** sends 14-bit pitch bend.
3. Every message goes to USB MIDI and to ESP-NOW (broadcast, WiFi channel 1) at the same time. There is no Bluetooth MIDI.
4. The OLED shows the live values and the LED ring follows them.

## Pages

Hold the encoder push for 600 ms or more to go to the next page.

| Page | What it shows or does |
| --- | --- |
| **LIVE** | Distance bar, velocity bar, raw mm, mode |
| **MODE** | CC or BEND |
| **SETTINGS** | Eight values: MIDI channel, CC numbers, distance min and max, curve, LED brightness, invert. The header also shows the free heap in KB (`Hxxk`) |
| **PRESETS** | 8 saved snapshots: load or save |

Turn the encoder to change values. On SETTINGS, a short tap on the encoder turns edit mode on or off for the selected row.

## Encoder push gestures

| Gesture | Effect |
| --- | --- |
| Short tap (under 400 ms) on LIVE | mute or unmute |
| Short tap on MODE | next option |
| Short tap on SETTINGS | edit on or off for the selected row |
| Short tap on PRESETS | load the selected slot |
| Hold (600 ms or more) | next page |
| Hold on PRESETS (1.5 s or more) | save the current settings to the selected slot |
| Triple tap (3 short taps within 600 ms) | WiFi config mode on or off |
| Hold during boot (5 s or more) | factory reset: clears the saved settings and restarts |

## Web config page

Triple tap the encoder. The guitar starts a WiFi network called **MidiGuitar-Config** (password `12345678`). Connect a phone and open <http://192.168.4.1> or <http://midiguitar.local>. The second address needs mDNS (Bonjour): macOS and iOS have it, Android needs an app.

The page shows every setting, a live monitor and the 8 preset slots with `LOAD` and `SAVE`. A change saves itself. Triple tap again to leave config mode and turn WiFi off. The page needs no internet. The ESP32 serves it.

## Presets

Eight slots store every saved setting (MIDI channel, CC numbers, distance limits, curve, brightness, invert).

On the guitar (PRESETS page): turn the encoder to select a slot. A short tap loads it. Hold for 1.5 s to save to it. Filled slots have a solid border, empty slots a dotted border.

On the phone: tap a slot, then `LOAD` or `SAVE`. Filled slots are bright, empty slots dim.

## Settings

| Setting | Range | What it does |
| --- | --- | --- |
| MIDI Ch | 1 to 16 | Output channel |
| CC Dist | 0 to 127 | CC number for the distance |
| CC Vel | 0 to 127 | CC number for the velocity |
| Dist Min | 5 to 500 mm | Distance for value 0 |
| Dist Max | 50 to 2000 mm | Distance for the top value |
| Curve | Linear, Log, Exp | Response shape |
| LED Brt | 5 to 255 | LED ring brightness |
| Invert | on, off | Close gives a high value |

The firmware saves the settings to flash 2 seconds after the last change.

## Factory reset

Hold the encoder push while you plug in USB-C (or press reset). After about 5 seconds the firmware clears its saved settings and restarts. The OLED is not on yet at that point, so the restart is the only sign.

## Build and flash

Requirements:

- Arduino IDE or `arduino-cli`
- ESP32 board package (Espressif Arduino-ESP32 v3.x)
- Board **XIAO_ESP32S3**, USB mode **USB-OTG (TinyUSB)**
- Libraries: SparkFun VL53L1X, Adafruit NeoPixel, Adafruit GFX, Adafruit SSD1306

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 tof_midi_v5
```

Then flash with the Arduino IDE or `arduino-cli upload`.

## Files

| File | Content |
| --- | --- |
| `tof_midi_v5.ino` | The sketch: sensor, MIDI, UI, web page, presets |
| `web_ui.html` | The config page. Open it in a browser to preview |
| `web_ui_html.h` | Made from `web_ui.html`. Do not edit it by hand |
| `tools/build_web_ui.sh` | Makes `web_ui_html.h` from `web_ui.html` |
| `CLAUDE.md` | Engineering rules and roadmap |
| `WEB_UI_PLAN.md` | Design notes for the config page |

After you edit `web_ui.html`, run `tools/build_web_ui.sh` before you flash.

## Tips

- **Boot self-test.** After the intro, the OLED shows the OLED, I2C and SENSOR status for about 1 s. If `SENSOR` reads `FAIL`, check the sensor cable. The firmware tries the VL53L1X 3 times at boot.
- **Heap monitor.** When you do not edit, the SETTINGS header shows the free heap (`Hxxk`). Watch it to find leaks.

## Known limits

- One sensor only.
- Config mode needs the triple tap. There is no config switch.

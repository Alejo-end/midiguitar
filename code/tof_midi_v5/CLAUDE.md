# MIDI Guitar — Roadmap & Engineering Plan

This document is for Claude Code (or any future agent / collaborator) picking up
work on `tof_midi_v5.ino`. It describes the system as it exists, the constraints
that shape it, and a prioritised roadmap of work to do next.

---

## 1. System overview

A Squier Mustang fitted with a XIAO ESP32-S3 Sense that turns hand gestures and
button presses into MIDI. Key pieces:

- **VL53L1X ToF sensor** (I²C @ 0x30) — distance → MIDI CC or 14-bit pitch bend
- **Adafruit NeoPixel ring** (16 px, GPIO 1 via level shifter) — visual feedback
- **SSD1306 OLED** (128×64, I²C @ 0x3C) — UI + status
- **Rotary encoder + push** (GPIOs 3, 2, 44) — primary navigation
- **TTP223 / tactile button** (GPIO 7) — context-sensitive action / triple-tap config
- **NVS (Preferences)** — persists user settings across reboots

Three MIDI transports coexist: **USB-MIDI** (TinyUSB), **BLE-MIDI** (custom 5-byte
packet format), and a **WiFi AP config mode** that exposes a web form on
`192.168.4.1` for editing settings from a phone.

The firmware is single-file Arduino C++ (~1200 lines). Hot path is a 20 ms poll
loop on Arduino's core 1; the WiFi server runs on core 0 only when config mode
is entered.

---

## 2. Hard constraints — DO NOT VIOLATE

These exist for real reasons and have been hit during development. Any change
that breaks one of these is a regression.

### 2.1 Performance budget
- **MIDI latency target: < 5 ms USB, < 20 ms BLE.** Anything that adds blocking
  work to the main `loop()` is suspect. The sensor poll → MIDI send path must
  stay tight.
- **No `delay()` longer than 5 ms in `loop()` or anything it calls.** Use
  `millis()` deltas. The intro animation is the only exception (runs once at
  boot before the watchdog is armed).
- **`server.handleClient()` must NEVER run on core 1.** It will block on slow
  clients and stall MIDI. It belongs in `webTask()` on core 0.

### 2.2 Radio
- WiFi and BLE share the 2.4 GHz radio. Running both *simultaneously* causes
  audible BLE-MIDI jitter. Config mode is therefore gated behind a triple-tap
  gesture and turns WiFi off on exit. **Do not auto-enable WiFi at boot.**
- If you ever need WiFi + BLE at the same time, reduce WiFi TX power
  (`WiFi.setTxPower(WIFI_POWER_8_5dBm)`) and accept that BLE will be jittery.

### 2.3 Watchdog
- The task WDT is 10 seconds. Both `loop()` and `webTask()` reset it. Any new
  long-running task must `esp_task_wdt_add(nullptr)` and reset it, or be
  pinned to a non-monitored core.

### 2.4 Memory
- After init with BLE + USB-MIDI active, free heap should be ≥ 100 KB. Adding
  WiFi pulls it down to ~70 KB. If you go below 50 KB, something is wrong
  (probably a String leak or a Web server with PROGMEM-less HTML).
- Don't use `String` in the hot path. Use `char[]` + `snprintf`.

### 2.5 NVS
- All persisted settings go through `loadSettings()` / `saveSettings()`. Adding
  a new persisted field means: declare default → declare runtime variable →
  add to both load and save → add to settings page if user-facing.
- Saves are debounced 2 s after last change to avoid wear. Keep it that way.

### 2.6 Pin map
The pin assignments come from the milled PCB (`finalMidiGuitar` schematic,
socket M1). Don't change them without updating the hardware. The mapping
between Seeed's "Dn" labels and ESP32 GPIOs is:

```
D0=1, D1=2, D2=3, D4=5, D5=6, D6=43, D7=44, D8=7, D10=9
```

The VL53L1X INT pin (D10 / GPIO 9) is **polled, not interrupt-driven**, because
the trace on the current PCB is unreliable. Don't reintroduce the interrupt
without verifying the trace.

---

## 3. Architecture rules

These are softer than constraints but worth following.

### 3.1 File layout
The sketch is currently single-file. If it grows past ~1500 lines, split into:
```
tof_midi_v5.ino       — setup/loop/globals only
midi_send.h/.cpp      — sendCC, sendPB, BLE/USB plumbing
sensor.h/.cpp         — VL53L1X init + poll + curve mapping
ui_oled.h/.cpp        — all draw* functions + page state machine
ui_ring.h/.cpp        — NeoPixel animations
config_web.h/.cpp     — WiFi AP, web server handlers, webTask
prefs.h/.cpp          — load/save + factory reset
```
Don't split prematurely. Single-file is easier for a hardware project at this
size.

### 3.2 Naming
- `static` everything that's file-local. The codebase is already disciplined
  about this.
- Constants use `SCREAMING_CASE`. Runtime state uses `camelCase`. Don't break
  this.
- Functions that touch the OLED are prefixed `draw*`. Functions that
  send MIDI are prefixed `send*`. Functions that change global state in
  response to user input are prefixed `cycle*` / `toggle*` / `adjust*`.

### 3.3 OLED rendering
- Page draw functions assume a clean buffer (cleared in `refreshOLED()`).
- New pages must call `drawPageDots(pageIdx, NUM_PAGES)` at the bottom.
- Don't call `oled.display()` from inside a page draw function — only
  `refreshOLED()` does that.
- Frame rate is capped at ~15 fps via `tOled` throttle. Don't bypass it.

### 3.4 NeoPixel rendering
- Animations are exclusive: `updateRing()` decides which one runs based on
  state precedence (BLE flash > wipe > config > BLE chase > bypass > live).
- Adding a new animation = adding a new branch to `updateRing()` AND a new
  `show*()` helper. Keep precedence explicit.

---

## 4. Roadmap — prioritised

Each item is sized as **S** (< 1 hour), **M** (1–4 hours), **L** (a session).
Items lower in the list assume earlier ones are done.

### Phase A — stability & polish (do first)

**A1. Fix WiFi config exit race [S]**
There's a small window where `webTask` could call `webServer.handleClient()`
on a server that's already been stopped. Add a mutex or sequence the shutdown
more carefully (set `configMode = false` → `vTaskDelay(20)` → stop server →
disconnect AP).

**A2. Persist `configMode = false` always [S]**
Currently if the board crashes in config mode, NVS doesn't know to come back
in performance mode. Verify this is actually the case (NVS only persists
what `saveSettings()` writes — `configMode` isn't in there, good) and add a
comment so nobody adds it.

**A3. Heap monitor on `PG_SETTINGS` [S]**
Add a hidden page (or settings entry) that shows `ESP.getFreeHeap()` live.
Useful for catching leaks when adding features.

**A4. Better OLED config-mode page [M]**
Current `drawPageConfig()` is functional but ugly. Add a QR code for the SSID
(WiFi join QR format: `WIFI:S:MidiGuitar-Config;T:WPA;P:12345678;;`). There
are tiny QR libraries that fit the AVR memory budget; on ESP32 it's trivial.
Consider `qrcode` by ricmoo (~5 KB).

**A5. Boot self-test [S]**
At boot, briefly check: I²C bus alive, sensor responds, OLED responds, NVS
readable. Show pass/fail badges in the intro animation. Helps diagnose dead
PCBs without serial.

### Phase B — features the project actually wants

**B1. Multi-preset system [M]**
Right now there's one set of settings. Add 8 named presets, stored under NVS
keys `preset_0..7`. Add a "PRESETS" page with 8 slots à la Digitakt. Encoder
selects, tap loads, long-press saves. This is the highest-impact feature for
actually using the instrument live.

**B2. Two-sensor support [L]**
The original plan was two ToF sensors. Add a second VL53L1X at I²C address
0x31 (use XSHUT sequencing to set distinct addresses). Each sensor maps to
its own CC pair. Settings page gains `Sensor2 Enabled`, `CC Dist 2`,
`CC Vel 2`. PCB already has provisions for two sensors per the schematic.

**B3. Capacitive bank-switch buttons [M]**
Add 3× TTP223 inputs that switch between banks (transposing the active CCs
or switching presets). Wiring already exists per the schematic; just need
firmware GPIO reads + debounce + action mapping.

**B4. MIDI Learn [M]**
On the live page, hold the encoder + tap edit → enters learn mode for next
gesture. Move the hand → captures min/max range automatically. Current
manual `distMinMm` / `distMaxMm` calibration is annoying.

### Phase C — networking

**C1. mDNS [S]**
After `WiFi.softAP()`, register `midiguitar.local`. Means the user types
that instead of `192.168.4.1`. Add `MDNS.begin("midiguitar")` and one line.

**C2. WebSocket live values [M]**
Replace the 200 ms `fetch('/live')` polling with a WebSocket. Gets
~50 Hz updates without HTTP overhead. Uses
[`ESPAsyncWebServer`](https://github.com/me-no-dev/ESPAsyncWebServer) +
`AsyncWebSocket` — slight dependency cost but cleaner. Only worth doing if
you want a "live monitor" tab in the web UI for calibrating curves.

**C3. WiFi-MIDI (RTP-MIDI / AppleMIDI) [L]**
Replace or supplement BLE-MIDI with [AppleMIDI-ESP32](https://github.com/lathoub/Arduino-AppleMIDI-Library).
Lower jitter than BLE on macOS. Needs the board to be on the same WiFi
network as the host (so this is a different mode from AP config — would
need to add station-mode credentials in the config UI first). This is the
most "professional" upgrade path.

**C4. OSC over WiFi [M]**
For Max/MSP / TouchDesigner / SuperCollider users. Send VL53L1X distance
and velocity as `/midiguitar/dist 0.0..1.0` and `/midiguitar/vel 0..1`.
Use `ArduinoOSC` library. Cheaper than RTP-MIDI to add.

### Phase D — polish only after all above

**D1. Factory reset [S]**
Long-press both buttons at boot → erase NVS namespace. Show "FACTORY RESET"
toast.

**D2. OTA firmware updates [M]**
Add ArduinoOTA in config mode. Push new sketches over WiFi without USB.
Convenient but introduces a security surface (the AP password is the only
gate).

**D3. Battery monitoring [M]**
If the project ever goes battery-powered, read VBAT via voltage divider on
A0. Show as battery icon on every page.

**D4. Sysex preset dump/restore [M]**
Send/receive presets as MIDI sysex from a host DAW. Useful for backup and
sharing presets between players.

---

## 5. How Claude Code should work on this

When picking up a task:

1. **Read this doc and the sketch first.** Don't ask the user for context that's
   already documented.
2. **Pick exactly one roadmap item per session.** Don't try to do A1+A2+A3 in
   one PR — they touch different areas and should be reviewable separately.
3. **Build before claiming done.** The sketch must compile under
   `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3` with TinyUSB enabled.
   No warnings (treat warnings as errors).
4. **Write a 5-line "what changed" summary** at the end of each session.
   Format: what / why / files touched / risk / how to test.
5. **Don't refactor architecturally without proposing it first.** This codebase
   prefers being clear over being abstracted.
6. **The user may not have the hardware in front of them.** Make changes
   testable on the bench: print to Serial, use a fake-sensor mode if needed,
   make the OLED mock-renderable.

---

## 6. Known issues / scars

Things that look weird but are weird for a reason:

- **VL53 INT pin is set as `INPUT_PULLUP` but never read.** Trace damage on the
  PCB; polling instead. Don't "clean this up."
- **`USB.enableDFU()` before `USBMIDI.begin()`.** Order matters on ESP32-S3
  TinyUSB. Don't reorder.
- **`bleSend()` builds the timestamp manually.** The 13-bit BLE-MIDI timestamp
  format is non-obvious. Don't "simplify" without reading the MIDI BLE spec.
- **The intro animation runs before WDT init.** Intentional — the animation is
  ~2 seconds and would otherwise need WDT resets sprinkled in.
- **Settings page scrollbar height math looks weird.** It is. Tested visually.
  Don't touch unless you're prepared to re-verify all 8 settings render.

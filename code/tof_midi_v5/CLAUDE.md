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

**B5. v6 — ESP-NOW transport + CV/MIDI receiver dongle [L]**
Replace BLE-MIDI with an ESP-NOW link to a dedicated receiver dongle. The
dongle is a Heat-project BOM derivative (strip the MLX90640 thermal cam and
the ST7789 IPS display, keep the rest) that terminates ESP-NOW packets as
**class-compliant USB-MIDI to a host** plus **4 channels of CV / expression**
via an MCP4728 quad 12-bit DAC into 3.5 mm TRS jacks. Net effect: the guitar
drives Eurorack / Moog Mother-32 / pedals directly with no laptop in the
audio path, and one-way latency drops to ~2–4 ms (vs. ~7–15 ms BLE-MIDI with
jitter spikes).

The dongle also carries a local **VL53L5CX 8×8 multizone ToF** for a
floor/amp-mounted gesture pad — second expressive zone alongside the guitar
neck's sensor, both fused into the same MIDI/CV output stream.

Firmware split (both live alongside `tof_midi_v5/` in the `code/` directory):
- **Guitar (`tof_midi_v6_tx/tof_midi_v6_tx.ino`):** *not yet written.* Strip
  `bleSend()` + BLE init from v5; add ESP-NOW send with the 5-byte packet
  format defined below. Keep USB-MIDI as the tethered fallback.
- **Dongle (`tof_midi_v6_rx/tof_midi_v6_rx.ino`):** *initial cut landed.*
  ESP-NOW recv callback → ring buffer → USB-MIDI write. SSD1306 page UI for
  link diagnostics (LIVE = last msg + pps + age, INFO = self/peer MAC +
  counters + heap, encoder long-press = page, tap on INFO = reset counters).
  No DAC or local sensor yet — those are PRs 3 and 4. VL53L5CX poll, when
  added, must run on a separate task pinned to core 0 (same pattern as
  `webTask` in v5).

Wire-format contract (both ends must agree, byte-identical):
```
struct __attribute__((packed)) EspNowMidiPacket {
    uint8_t magic;    // 0xCB sync byte — rejects RF noise of the right length
    uint8_t seq;      // rolling u8, lets RX count drops via gaps
    uint8_t status;   // MIDI status byte (cmd nibble | channel nibble)
    uint8_t data1;
    uint8_t data2;
};  // sizeof() == 5
```
Both ends pin to **WiFi channel 1** for ESP-NOW. If the guitar later enters
WiFi config mode on a different channel, ESP-NOW link drops until both ends
agree again — this is the open coexistence question, deliberately punted.

Open questions to resolve before starting:
- ESP-NOW channel pinning vs. WiFi config mode — lock both to one channel,
  or pause ESP-NOW while config mode is up
- Pairing model: hardcoded MAC vs. pair-on-button-hold
- Confirm Heat's existing op-amp scaling stage actually outputs 0–5 V CV;
  otherwise add one to the dongle KiCad
- Rip BLE out entirely vs. keep it behind a build flag during transition

Inventory check (everything but extra TRS jacks is already on hand):
MCP4728 ✓, VL53L5CX ✓, XIAO ESP32-S3 Plus ✓, SSD1306 (from guitar build) ✓,
EC11 encoder ✓, passives ✓, enclosure stock ✓. Need 3 additional
Switchcraft 35RAPC2AV jacks to populate all 4 CV outputs.

Slicing into reviewable PRs:
1. ESP-NOW TX on guitar, parallel to existing BLE, behind a runtime toggle
   on `PG_SETTINGS` [M]
2. Dongle firmware: ESP-NOW RX → USB-MIDI passthrough, minimal UI [M]
3. MCP4728 driver + CV channel mapping settings page on the dongle [M]
4. VL53L5CX on the dongle + sensor fusion into the MIDI/CV stream [M]
5. Strip BLE from guitar firmware, finalize v6 [S]

Constraints from §2 still apply: ESP-NOW receive callbacks must not block
core 1 on the dongle; the 20 ms loop budget on the guitar is unchanged.
Once v6 ships and BLE is removed, §6's note about the BLE-MIDI timestamp
gymnastics becomes obsolete — delete it then. §2.2's WiFi+BLE coexistence
rule is superseded by a new WiFi+ESP-NOW coexistence rule (same channel,
or one at a time); update §2.2 as part of PR 1.

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
- **No `USB.enableDFU()`.** It was removed (v5 + v6_rx): the CDC+DFU+MIDI
  composite made hosts (macOS / Ableton) enumerate the serial port but skip the
  MIDI interface, so the device didn't appear as a MIDI input. Plain CDC+MIDI
  (`usbMidi.begin(); USB.begin();`) enumerates reliably. Don't re-add enableDFU
  unless you also re-verify the device still shows up in Audio MIDI Setup.
- **`bleSend()` builds the timestamp manually.** The 13-bit BLE-MIDI timestamp
  format is non-obvious. Don't "simplify" without reading the MIDI BLE spec.
- **The intro animation runs before WDT init.** Intentional — the animation is
  ~2 seconds and would otherwise need WDT resets sprinkled in.
- **Settings page scrollbar height math looks weird.** It is. Tested visually.
  Don't touch unless you're prepared to re-verify all 8 settings render.

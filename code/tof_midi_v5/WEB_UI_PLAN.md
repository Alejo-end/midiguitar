# Web UI Plan — Max/MSP-style mobile config

Companion to `CLAUDE.md`. Scope: replace the minimal form served at
`192.168.4.1` (current `handleRoot()`) with a richer mobile-first page
that exposes every persisted setting and a live monitor. Closest
roadmap match: A4 (better config-mode UI) extended with full settings
parity from the OLED page.

---

## 1. Goal

A self-hosted config UI that:

- Looks like a Cycling '74 Max for Live device — dark panels, LCD-style
  number boxes, segmented controls, monospace typography, teal accent.
- Exposes **all** persisted settings (the current form is missing
  transport, mode, brightness, invert).
- Streams live distance / velocity / raw mm so the player can calibrate
  while watching the page.
- Auto-saves via the existing 2 s debounced `saveSettings()` — no SAVE
  button, no nags.
- Runs **entirely off the ESP32 AP**: no internet, no CDN, no fonts.
  All HTML/CSS/JS inline in a single `PROGMEM` blob.

## 2. Constraints recap (from CLAUDE.md §2)

- All page bytes live in PROGMEM. Do not hold them in `String`.
- handlers must remain non-blocking; no long work in webTask.
- webTask stays pinned to core 0 — no architecture change.
- Free heap floor with WiFi up: 70 KB. Page payload budget: ≤ 8 KB.
- Pin map and BLE/USB plumbing untouched.

## 3. Endpoints

| Method | Path          | Purpose                                       |
|--------|---------------|-----------------------------------------------|
| GET    | `/`           | Static PROGMEM page (HTML+CSS+JS, one blob)   |
| GET    | `/api/state`  | JSON of all persisted settings + preset mask  |
| GET    | `/api/live`   | JSON of live values (dist, vel, mm, bend, …)  |
| GET    | `/api/set`    | `?k=KEY&v=VALUE` → set one field, returns 204 |
| GET    | `/api/preset` | `?slot=N&op=load|save` → preset op, returns 204 |

GET-for-set is intentional: simpler `fetch()` calls, no CSRF surface
since the AP is isolated, no body parsing.

`/save` and `/live` (the old form-style endpoints) are removed; the
API replaces both.

### `/api/state` shape

```json
{ "ch":1, "ccd":20, "ccv":21, "dmin":10, "dmax":500,
  "cv":0, "brt":40, "inv":0, "tr":0, "md":0, "pe":5 }
```

`pe` is a bitmask: bit N set means preset slot N has a saved snapshot.

### `/api/live` shape

```json
{ "d":63, "v":12, "b":8192, "mm":250, "c":1, "bp":0 }
```

`c` = BLE connected, `bp` = bypassed.

### `/api/set` keys

`ch ccd ccv dmin dmax cv brt inv tr md` — same names as `/api/state`.
Mode and transport changes call `triggerWipe()` and lazy-init BLE the
same way `cycleMode()` / `cycleTransport()` do.

## 4. Page sections (mobile-first, single column, max-width 480px)

1. **Header** — device name, host, BLE-conn dot.
2. **LIVE MONITOR** — distance bar, velocity bar, raw mm box, bend box,
   bypass badge.
3. **PRESETS** — 8 slots in a 4×2 grid, plus LOAD / SAVE buttons.
   Filled slots are bright; empty are dimmed.
4. **OUTPUT** — segmented USB / BLE / BOTH.
5. **MODE** — segmented CC / BEND.
6. **MIDI** — channel, CC distance, CC velocity (number boxes).
7. **SENSOR** — dist min / max, curve dropdown, invert toggle.
8. **LED** — brightness range slider.

## 5. Aesthetic

- Background `#1a1a1a`, cards `#232323` with `#2d2d2d` header strips.
- Borders `#3a3a3a`, dashed dividers `#2d2d2d`.
- Accent teal `#5fd7c5` (selected segments, BLE dot, slider thumb).
- LCD number boxes: `#ffb742` text on `#0e0e0e`, monospace.
- Caps headers, 0.18em letter-spacing, no big shadows.
- 44 px minimum tap target on mobile.

## 6. Implementation in `tof_midi_v5.ino`

- `web_ui.html` is the **canonical** source — open it directly in any
  browser to preview with mock data (a `file://`-only fetch shim mocks
  `/api/state`, `/api/live`, `/api/preset`).
- `tools/build_web_ui.sh` strips everything between
  `<!-- BUILD:STRIP_BEGIN -->` / `<!-- BUILD:STRIP_END -->` markers
  (top doc comment, mock script) and wraps the rest in a PROGMEM raw
  string, writing `web_ui_html.h`. Run after every change to the HTML.
- The `.ino` `#include`s `web_ui_html.h` and serves it via `send_P`.
- `handleRoot()` → `webServer.send_P(200, "text/html", WEB_UI_HTML)`.
- `handleApiState() / handleApiLive() / handleApiSet()` use `char[]` +
  `snprintf` (no `String` in builders).
- `handleSave()` and `handleLive()` removed (replaced by the API).
- In `enterConfigMode()`, register the new routes.

### File layout

```
tof_midi_v5/
├── tof_midi_v5.ino           sketch — sensor + MIDI + handlers + routes
├── web_ui.html               canonical, browser-previewable
├── web_ui_html.h             generated PROGMEM page (flashed)
├── tools/build_web_ui.sh     regenerator
├── README.md                 user-facing docs
├── CLAUDE.md                 roadmap + constraints
└── WEB_UI_PLAN.md            this doc
```

JS side:
- On load: `fetch('/api/state')` → populate inputs.
- On change/input: `fetch('/api/set?k=...&v=...')` (range debounced
  60 ms; numbers fire on `change`, not per keystroke).
- `setInterval(tick, 200)` polls `/api/live` and animates bars.

## 7. Risk

- HTML grows past 8 KB → `send_P` allocation under WiFi+BLE could
  pressure heap. Keep total < 8 KB; minify if it creeps.
- A user dragging the brightness slider → ~16 req/s. Web server can
  handle it, but each request marks `settingsDirty` and resets the 2 s
  save timer. JS-side 60 ms debounce on ranges keeps it sane.
- `/api/set` for `tr=BLE/BOTH` must lazy-init BLE — handled by mirroring
  `cycleTransport()` semantics.

## 8. Test plan

Hardware test (user runs locally; no `arduino-cli` on dev machine):

1. `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3` — clean,
   no warnings.
2. Triple-tap SW1 → connect to `MidiGuitar-Config` → open
   `http://192.168.4.1`.
3. Phone Safari + Chrome, portrait + landscape — every tap target
   ≥ 44 px, no horizontal scroll.
4. Change every setting once → verify OLED + LED ring reflect within
   ~200 ms; values persist after triple-tap exit + reboot.
5. Live monitor — wave hand, bars update at ~5 Hz, no jitter on the
   MIDI side (BLE jitter is expected per CLAUDE.md §2.2).
6. Heap check: enter config mode, navigate page for 30 s, watch
   `ESP.getFreeHeap()` over Serial — should stay ≥ 70 KB.

## 9. Out of scope (deferred)

- mDNS (C1) — independent one-liner, do separately.
- WebSocket live values (C2) — current 200 ms polling is fine.
- MIDI Learn (B4) — needs hardware-side gesture detection first.
- OTA (D2) — security review needed before adding.

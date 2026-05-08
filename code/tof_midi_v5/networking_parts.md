# Assignment 10 — Networking and Communications

> Project: **MIDI Guitar** — a Squier Mustang fitted with a XIAO ESP32-S3 Sense
> that turns hand-distance gestures over a VL53L1X time-of-flight sensor into
> MIDI Control Change and Pitch Bend messages. (The S3 and the C3 share the
> same Espressif WiFi/BLE stack, so all the code below works the same way on
> a XIAO ESP32C3.)

---

## 1. How I use wireless communication in my project

The MIDI Guitar uses wireless communication in two complementary ways.

First, **BLE-MIDI** lets the guitar send musical messages directly to a phone,
tablet, or laptop running a synth or DAW — no cables, no dongles. The host
pairs with the device advertised as `MidiGuitar` and immediately starts
receiving MIDI Control Change (CC20 = distance, CC21 = velocity) and 14-bit
Pitch Bend generated from the ToF sensor.

Second, **WiFi in Access Point mode** turns the guitar into a tiny web server
for live configuration. The performer triple-taps the encoder, joins the
`MidiGuitar-Config` network from their phone, and tweaks MIDI channel, CC
numbers, sensor range, LED brightness, and presets in a browser — no
re-flashing required. Both radios share the 2.4 GHz antenna, so WiFi stays
OFF during play to keep BLE-MIDI latency under 20 ms.

---

## 2. Connecting to the board from a web browser

![Web UI on phone](media/web_ui_screenshot.png)

To make the board reachable from a browser, the ESP32 runs in **WiFi Access
Point mode**: it broadcasts its own network and starts a small HTTP server on
port 80. No router or internet is required — the phone connects directly to
the guitar.

```cpp
WiFi.mode(WIFI_AP);
WiFi.softAP(AP_SSID, AP_PASS);          // "MidiGuitar-Config" / "12345678"
IPAddress ip = WiFi.softAPIP();         // always 192.168.4.1
webServer.on("/", handleRoot);          // map URL → handler
webServer.begin();
```

When the browser requests `/`, `handleRoot()` returns an HTML page that lives
in PROGMEM as `WEB_UI_HTML`. To avoid blocking the MIDI hot loop, the server
is pinned to the second CPU core via FreeRTOS:

```cpp
xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, &webTaskH, 0);
```

The user joins `MidiGuitar-Config`, opens **`http://192.168.4.1`**, and the
configuration page loads.

---

## 3. Sending data **to** the board

![Sending data to the board](media/send_data.mp4)

Sending data to the board uses simple **HTTP GET requests with query string
parameters**. The web UI's JavaScript fires `fetch('/api/set?k=ccd&v=42')`
whenever the user moves a slider, where `k` is the setting key and `v` is the
new integer value. The handler parses the arguments, validates the key, and
writes the value to the matching runtime variable:

```cpp
static void handleApiSet() {
    const String &k = webServer.arg("k");
    long v = webServer.arg("v").toInt();
    const Setting *s = settingByKey(k.c_str());
    if (!s) { webServer.send(400, "text/plain", "bad key"); return; }
    settingSet(*s, v);
    webServer.send(204);     // 204 = success, no body
}
```

Changes propagate instantly: the OLED redraws, the LED ring updates colour,
and the new value is queued for non-volatile (NVS) save two seconds later.
Presets save and load via `/api/preset?slot=N&op=save|load` the same way.

---

## 4. Receiving data **from** the board

![Receiving live sensor data](media/receive_data.mp4)

The browser receives data from the board by polling two JSON endpoints.
`/api/state` returns the full current configuration once at page load;
`/api/live` returns real-time sensor readings several times per second. Both
are built with `snprintf` into a fixed `char[]` buffer rather than the
`String` class, so the heap stays stable during long sessions.

```cpp
static void handleApiLive() {
    char buf[120];
    snprintf(buf, sizeof(buf),
        "{\"d\":%d,\"v\":%d,\"b\":%d,\"mm\":%d,\"c\":%u,\"bp\":%u}",
        liveDist, liveVel, liveBend, (int)(ema >= 0 ? ema : 0),
        bleConnected ? 1u : 0u, bypassed ? 1u : 0u);
    webServer.send(200, "application/json", buf);
}
```

The keys are abbreviated (`d` = distance CC, `v` = velocity, `b` = pitch
bend, `mm` = raw millimetres) to keep packets small. The page parses each
response and updates the on-screen meters, letting me watch the ToF sensor
live and tune the distance range visually.

---

## 5. Wired communication — I²C

![I²C bus scan and OLED + sensor running together](media/i2c_demo.mp4)

For board-to-peripheral communication, this project uses **I²C** to talk to
two devices on the same bus: a **VL53L1X** time-of-flight sensor at address
`0x29` and an **SSD1306** OLED display at `0x3C`. Both share the SDA/SCL
pins on the XIAO ESP32-S3 (GPIOs 5 and 6), so a single two-wire bus carries
everything — that is the whole point of I²C.

```cpp
#define I2C_SDA   5     // XIAO D4 → sensor SDA + OLED SDA
#define I2C_SCL   6     // XIAO D5 → sensor SCL + OLED SCL
#define I2C_FREQ  400000

Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
sensor.begin(Wire);
oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
```

At boot the firmware scans every address from 1 to 126 and prints every ACK
to the serial console. This is both a sanity check during bring-up and
deliberate warm-up traffic — the SSD1306 clone on my PCB refuses its init
sequence unless the bus has already seen activity:

```cpp
for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("[I2C] 0x%02X ACK\n", a);
}
```

---

## Reference — full networking source

The complete WiFi + web-server code lives in
[`tof_midi_v5.ino`](tof_midi_v5.ino) between the comment markers
`// ─── WiFi config mode ───` (line ~413) and the end of `exitConfigMode()`
(line ~549), plus the `#include <WiFi.h>` / `#include <WebServer.h>` block
near the top.

| Endpoint           | Method | Purpose                              |
|--------------------|--------|--------------------------------------|
| `/`                | GET    | Serve the configuration HTML page    |
| `/api/state`       | GET    | Read full settings as JSON           |
| `/api/live`        | GET    | Read live sensor values as JSON      |
| `/api/set?k=&v=`   | GET    | Update one setting                   |
| `/api/preset`      | GET    | Save / load a preset slot (0–7)      |

### Placeholders to replace before submitting

- `media/web_ui_screenshot.png` — screenshot of the page open in your phone's
  browser at `192.168.4.1`.
- `media/send_data.mp4` — 5–10 s clip: move a slider in the browser, watch
  the OLED / LED ring react.
- `media/receive_data.mp4` — 5–10 s clip: wave your hand in front of the
  sensor, watch the live values update in the browser.
- `media/i2c_demo.mp4` — 5–10 s clip: serial monitor showing the I²C scan
  output (`0x29 ACK`, `0x3C ACK`) and the OLED + sensor running together.

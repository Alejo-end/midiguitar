// tof_midi_v5.ino — ToF MIDI controller with WiFi config mode
//
// VL53L1X — long-range hand gestures (10–500mm)
// MIDI: CC20 (distance), CC21 (velocity), Pitch Bend (14-bit)
// Config: triple-tap encoder push to open WiFi AP at 192.168.4.1
//
// Board: XIAO_ESP32S3, USB: USB-OTG (TinyUSB)
// Libs: SparkFun VL53L1X, Adafruit NeoPixel, Adafruit SSD1306, Adafruit GFX
//
// ─── In-file roadmap (work that fits inside this single .ino) ─────────────
// Cross-reference: ./CLAUDE.md §4 for the full project roadmap. The items
// here are the subset that can land without splitting the sketch.
//
//   [done]
//   B1  8-slot preset system (SETTINGS table + presetsExistMask).
//   A1  WiFi exit race — webTaskAlive handshake in exitConfigMode().
//   A2  Comment on configMode-not-in-SETTINGS (next to its declaration).
//   A3  Heap monitor — KB readout in the PG_SETTINGS header (non-edit).
//   A5  Boot self-test card via drawBootBadges() post-sensor-init.
//   C1  mDNS — http://midiguitar.local in enterConfigMode().
//   D1  Factory reset — hold ENC_PUSH ≥ 5 s at boot → prefs.clear().
//
//   [now-able]
//   B4  MIDI Learn — long-press encoder on PG_LIVE → capture next 2s of
//       readings as new distMin/distMax. ~40 LOC, all inside handleEncoder
//       + pollSensor (gated by a `learnUntil` timestamp).
//   D4  SysEx preset dump/restore — usbMidi has `.sendSysEx`/`.read`.
//       Format: 0xF0 0x7D 0x4D 0x47 <slot> <payload> 0xF7. ~60 LOC.
//   --  Multi-tap dispatch — currently fires N actions for N taps; user-
//       facing behavior change to collapse to single dispatch per flush.
//
//   [perf items]
//   Per-loop millis() caching, rate-limited sensor polling, and TEMP-debug
//   block gating are already applied. Future: track max loop micros to
//   verify the < 5 ms MIDI latency target in §2.1.

// ─── Config ──────────────────────────────────────────────────────────────────

#define DEBUG           1

// Pin map — XIAO ESP32-S3 socket M1, matches finalMidiGuitar.kicad_sch.
// XIAO Dn → GPIO: D0=1 D1=2 D2=3 D3=4 D4=5 D5=6 D6=43 D7=44 D8=7 D9=8 D10=9
#define I2C_SDA         5     // XIAO D4 → J1 sensor SDA, J4 OLED SDA
#define I2C_SCL         6     // XIAO D5 → J1 sensor SCL, J4 OLED SCL
#define I2C_FREQ        400000  // matches debug_oled.ino which works on this board

// ESP-NOW link to the v6 receiver dongle. The guitar broadcasts its MIDI on a
// fixed channel; the dongle listens (../tof_midi_v6_rx). No pairing — broadcast
// sidesteps MAC bookkeeping and the dongle accepts any sender with the right
// magic byte. Channel must match the dongle's (both pin channel 1). ESP-NOW TX
// is suspended while WiFi config mode is up, and restored on exit.
#define ESPNOW_CHANNEL  1
#define ESPNOW_MAGIC    0xCB
#define VL53_ADDR       0x29   // VL53L1X power-on default; no XSHUT remap on this board
#define POLL_MS         20
#define EMA_ALPHA       0.2f
#define VEL_DEAD_MM     2.0f
#define VEL_MAX_MM      30.0f
#define CURVE_LINEAR    0
#define CURVE_LOG       1
#define CURVE_EXP       2
#define STRIP_PIN       9     // XIAO D10 → U1 level shifter A, then R1 → J3 NeoPixel DIN
// SW1 and the battery divider exist only on the JLCPCB board (../../reins-guitar).
// On the milled finalMidiGuitar board SW1 sat on D2 (GPIO3) and read stuck LOW,
// because its footprint joined both pads to one internal contact. The JLCPCB board
// moves SW1 to D1 and wires it on diagonal pads. For the milled board set both to -1.
#define BTN_PIN         2     // XIAO D1 → SW1 to GND: mute toggle on every page (-1 = none)
#define BAT_PIN         1     // XIAO D0 → R4/R5 divider, half the battery voltage (-1 = none)
#define BAT_LOW_MV      3450  // "LOW BAT" toast below this; the LED ring dims near 3.4 V
#define BAT_FULL_MV     4150
#define BAT_EMPTY_MV    3350
#define ENC_A           7     // XIAO D8 → S1 encoder channel A
#define ENC_B           8     // XIAO D9 → S1 encoder channel B
#define ENC_PUSH        4     // XIAO D3 → S1 encoder pushbutton to GND
#define NUM_PIXELS      16
#define OLED_W          128
#define OLED_H          64
#define OLED_ADDR       0x3C
#define NUM_PAGES       4
#define DEBOUNCE_MS     180

// Loop tunables
#define OLED_REFRESH_MS         66     // ~15 fps cap (CLAUDE.md §3.3)
#define NVS_SAVE_DEBOUNCE_MS    2000   // 2s after last setting change (§2.5)
#define OLED_SLEEP_MS           60000  // 60s no-activity → blank screen
#define LIVE_VEL_THRESH         5      // liveVel above this keeps OLED awake
#define BOOT_SERIAL_WAIT_MS     2000   // only honoured when DEBUG=1

// Encoder push gesture thresholds (used by handleEncoder)
#define ENC_TAP_MAX_MS          400    // release within this → short tap
#define ENC_HOLD_MIN_MS         600    // held this long → next page
#define ENC_SAVE_MIN_MS         1500   // held this long on PG_PRESETS → save

// WiFi config mode
#define AP_SSID         "MidiGuitar-Config"
#define AP_PASS         "12345678"
#define CFG_TAP_WINDOW  600
#define CFG_TAP_COUNT   3
// Set to 1 to boot directly into config mode (skips the SW1 triple-tap).
// Useful when SW1 isn't wired/working. Set back to 0 for normal play mode.
#define BOOT_INTO_CONFIG 0

// Defaults
#define MIDI_CH_DEFAULT      1
#define CC_DIST_DEFAULT      20
#define CC_VEL_DEFAULT       21
#define DIST_MIN_DEFAULT     10
#define DIST_MAX_DEFAULT     500
#define DIST_MIN_LO          5
#define DIST_MIN_HI          500
#define DIST_MAX_LO          50
#define DIST_MAX_HI          2000
#define CURVE_DEFAULT        CURVE_LINEAR
#define STRIP_BRIGHT_DEFAULT 76    // ~30% of 255 — ring runs white at this level

// Runtime-adjustable settings
static uint8_t  midiCh      = MIDI_CH_DEFAULT;
static uint8_t  ccDist      = CC_DIST_DEFAULT;
static uint8_t  ccVel       = CC_VEL_DEFAULT;
static int16_t  distMinMm   = DIST_MIN_DEFAULT;
static int16_t  distMaxMm   = DIST_MAX_DEFAULT;
static uint8_t  curve       = CURVE_DEFAULT;
static uint8_t  stripBright = STRIP_BRIGHT_DEFAULT;
static bool     distInvert  = false;

// ─── Includes ────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <SparkFun_VL53L1X.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>      // esp_read_mac() + ESP_MAC_WIFI_STA — core 3.x moved these here
#include <WebServer.h>
#include <ESPmDNS.h>
#include "web_ui_html.h"

// ─── Types ───────────────────────────────────────────────────────────────────

enum Mode      : uint8_t { MODE_CC, MODE_PB, MODE_COUNT };
enum Page      : uint8_t { PG_LIVE, PG_MODE, PG_SETTINGS, PG_PRESETS };

// Settings table types — declared up here so Arduino's auto-generated function
// prototypes (inserted near the top of the .ino) can see them. The actual
// SETTINGS[] table and setting* helpers live further down.
enum SType : uint8_t { T_U8, T_I16, T_BOOL, T_CURVE };
struct Setting {
    const char *jsonKey;
    const char *label;
    const char *nvsKey;
    SType       type;
    void       *slot;
    int16_t     lo, hi;
    int8_t      step;
    void      (*onChange)();
};

// ─── ESP-NOW MIDI link ───────────────────────────────────────────────────────
// Fixed 5-byte packet, byte-identical with the v6 receiver
// (../tof_midi_v6_rx/tof_midi_v6_rx.ino). magic rejects RF garbage; seq lets the
// RX count drops. Keep __attribute__((packed)) or the two ends disagree on
// offsets. The static_assert catches an accidental padding bump.
struct __attribute__((packed)) EspNowMidiPacket {
    uint8_t magic;    // 0xCB sync byte
    uint8_t seq;      // rolling sequence for drop detection
    uint8_t status;   // MIDI status byte (cmd nibble | channel nibble)
    uint8_t data1;
    uint8_t data2;
};
static_assert(sizeof(EspNowMidiPacket) == 5,
              "EspNowMidiPacket must stay 5 bytes — packed struct broke");

static const uint8_t ESPNOW_BCAST[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static bool          espNowReady = false;
static uint8_t       espNowSeq   = 0;
static uint32_t      espNowTx    = 0;     // TX packet count (debug)
static int           espNowErr   = 0;     // last esp_now_send() return (debug)
static uint32_t      sReady      = 0;     // VL53L1X data-ready hits (debug)
static uint32_t      sValid      = 0;     // VL53L1X valid distance reads (debug)

static unsigned long wipeUntil   = 0;   // ring "wipe" animation on mode change
static uint32_t      wipeColor   = 0;

// ─── Globals ─────────────────────────────────────────────────────────────────

static USBMIDI           usbMidi;
static SFEVL53L1X        sensor;
static Adafruit_NeoPixel ring(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_SSD1306  oled(OLED_W, OLED_H, &Wire);
static Preferences       prefs;

// WiFi config mode state.
// CLAUDE.md A2 — `configMode` is intentionally NOT in the SETTINGS table.
// If the board crashes while in config mode, NVS will not restore it, so
// the next boot comes up in performance mode (play). Don't "fix" that.
static WebServer        webServer(80);
static bool             configMode   = false;
static TaskHandle_t     webTaskH     = nullptr;
static volatile bool    webTaskAlive = false;     // set by webTask itself

// Settings page state
#define NUM_SETTINGS    8
static int8_t  settingsCursor  = 0;
static bool    settingsEditing = false;
static bool    settingsDirty   = false;

// Presets page state — 8 named slots in NVS keys "preset_0".."preset_7".
// presetsExistMask bit N = slot N has a saved preset (cached to avoid
// hitting NVS on every redraw).
#define NUM_PRESETS     8
static int8_t  presetCursor    = 0;
static uint8_t presetsExistMask = 0;
static unsigned long dirtyTime = 0;

static Mode      mode      = MODE_CC;
static bool      bypassed  = false;
static bool      sensorOK  = false;
static uint16_t  batMv     = 0;       // smoothed battery voltage, 0 = no battery seen

static float   ema      = -1.0f;
static float   prevEma  = -1.0f;
static int16_t prevDist = -1;
static int16_t prevVel  = -1;
static int16_t prevBend = -1;

static int16_t liveDist = 0;
static int16_t liveVel  = 0;
static int16_t liveBend = 0;

static unsigned long tPoll        = 0;
static unsigned long tOled        = 0;
static unsigned long lastActivity = 0;
static bool          oledAsleep   = false;

// ─── Encoder ─────────────────────────────────────────────────────────────────

static volatile int8_t encDelta = 0;

static void IRAM_ATTR encISR() {
    static uint8_t prev = 0;
    static int8_t  raw  = 0;
    uint8_t s = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    uint8_t c = (prev << 2) | s;
    switch (c) {
        case 0b0001: case 0b0111: case 0b1110: case 0b1000: raw++; break;
        case 0b0010: case 0b1011: case 0b1101: case 0b0100: raw--; break;
    }
    prev = s;
    if (raw >= 4)  { encDelta++; raw -= 4; }
    if (raw <= -4) { encDelta--; raw += 4; }
}

// ─── Menu ────────────────────────────────────────────────────────────────────

static Page currentPage = PG_LIVE;
static bool oledDirty   = true;

static const char   *toastText  = nullptr;
static unsigned long toastUntil = 0;

static void showToast(const char *msg) {
    toastText  = msg;
    toastUntil = millis() + 1000;
    oledDirty  = true;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

static float applyCurve(float t) {
    switch (curve) {
        case CURVE_LOG: return log1pf(t * (M_E - 1.0f));
        case CURVE_EXP: return (expf(t) - 1.0f) / (M_E - 1.0f);
        default:        return t;
    }
}

static void updateEma(float &v, float s, float a) {
    v = (v < 0.0f) ? s : a * s + (1.0f - a) * v;
}

static float normCurve(float v, float lo, float hi) {
    return applyCurve(constrain((v - lo) / (hi - lo), 0.0f, 1.0f));
}

static uint8_t wrap(int8_t v, uint8_t n) {
    if (v < 0) return n - 1;
    if (v >= n) return 0;
    return v;
}

// ─── ESP-NOW send ────────────────────────────────────────────────────────────
// Broadcast one 5-byte MIDI packet to the dongle. Connectionless: if no dongle
// is listening the frame is simply unacked — no blocking, no error to handle.
static void espNowSend(uint8_t st, uint8_t d1, uint8_t d2) {
    if (!espNowReady) return;
    EspNowMidiPacket p = { ESPNOW_MAGIC, espNowSeq++, st, d1, d2 };
    espNowErr = esp_now_send(ESPNOW_BCAST, (const uint8_t *)&p, sizeof(p));
    if (espNowErr == ESP_OK) espNowTx++;   // count only real sends so the counter can't lie
}

// ─── MIDI output ─────────────────────────────────────────────────────────────
// Transport is no longer user-selectable: every message goes out BOTH USB
// (consumed only when a host is plugged in; harmlessly dropped otherwise) AND
// ESP-NOW (to the v6 dongle). No BLE, no mode toggle.
static void sendCC(uint8_t cc, uint8_t val) {
    if (bypassed) return;
    usbMidi.controlChange(cc, val, midiCh);
    espNowSend(0xB0 | (midiCh - 1), cc, val);
}

static void sendPB(uint16_t bend) {
    if (bypassed) return;
    usbMidi.pitchBend(bend, midiCh);
    espNowSend(0xE0 | (midiCh - 1), bend & 0x7F, (bend >> 7) & 0x7F);
}

// ─── Sensor init ─────────────────────────────────────────────────────────────

static bool initSensor() {
    if (sensor.begin(Wire) != 0) return false;
    sensor.setTimingBudgetInMs(POLL_MS);
    sensor.setIntermeasurementPeriod(POLL_MS);
    sensor.startRanging();
    unsigned long t0 = millis();
    while (!sensor.checkForDataReady()) {
        if (millis() - t0 > 200) return false;
        delay(5);
    }
    sensor.clearInterrupt();
    return true;
}

// ─── ESP-NOW init ────────────────────────────────────────────────────────────
// Safe to call more than once — exitConfigMode() re-runs it to restore the link
// after WiFi was flipped to AP for the config web UI. Pins the STA interface to
// ESPNOW_CHANNEL and registers a broadcast peer so esp_now_send() has a target.
static void initEspNow() {
    // No esp_now_deinit() here — calling it before WiFi/ESP-NOW is initialized
    // (i.e. at first boot) dereferences an uninitialized internal pointer and
    // panics. Teardown for the re-init path lives in enterConfigMode(), where
    // ESP-NOW is actually live. So this only ever does a clean fresh init.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();              // enable the radio without associating to an AP
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    // Kill modem sleep so the radio stays awake (set last, see the RX note).
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (esp_now_init() != ESP_OK) { espNowReady = false; return; }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ESPNOW_BCAST, 6);
    peer.channel = 0;              // 0 = use the interface's current channel — robust
                                   // against any mismatch vs. forcing ESPNOW_CHANNEL
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) { espNowReady = false; return; }   // broadcast needs a peer

    // Full TX power for best range. Config mode clamps it to 8.5 dBm to save
    // heap; restoring it here (not just at boot) keeps the boot and config-exit
    // paths symmetric so the link doesn't stay weak after a config session.
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    espNowReady = true;
}

// ─── Settings table ─────────────────────────────────────────────────────────
//
// Single source of truth for every persisted setting. Order is locked —
// preset serialization, NVS keys, and the OLED settings page index all
// depend on it. Append; do not reorder, do not insert in the middle, or
// existing presets become garbled.
//
// SType / Setting are declared earlier (near the other enums) so the
// Arduino preprocessor's auto-prototypes can see them.

static void onBrightChange() { ring.setBrightness(stripBright); }

static const Setting SETTINGS[] = {
    { "ch",   "MIDI Ch",  "midiCh",  T_U8,    &midiCh,      1, 16,                   1, nullptr },
    { "ccd",  "CC Dist",  "ccDist",  T_U8,    &ccDist,      0, 127,                  1, nullptr },
    { "ccv",  "CC Vel",   "ccVel",   T_U8,    &ccVel,       0, 127,                  1, nullptr },
    { "dmin", "Dist Min", "distMin", T_I16,   &distMinMm,   DIST_MIN_LO, DIST_MIN_HI, 5, nullptr },
    { "dmax", "Dist Max", "distMax", T_I16,   &distMaxMm,   DIST_MAX_LO, DIST_MAX_HI,10, nullptr },
    { "cv",   "Curve",    "curve",   T_CURVE, &curve,       0, 2,                    1, nullptr },
    { "brt",  "LED Brt",  "bright",  T_U8,    &stripBright, 5, 255,                  5, onBrightChange },
    { "inv",  "Invert",   "invert",  T_BOOL,  &distInvert,  0, 1,                    1, nullptr },
};
static_assert(sizeof(SETTINGS)/sizeof(SETTINGS[0]) == NUM_SETTINGS,
              "NUM_SETTINGS must equal the SETTINGS table length");

static int32_t settingGet(const Setting &s) {
    switch (s.type) {
        case T_U8: case T_CURVE: return *(uint8_t*)s.slot;
        case T_I16:              return *(int16_t*)s.slot;
        case T_BOOL:             return *(bool*)s.slot ? 1 : 0;
    }
    return 0;
}

static bool settingSet(const Setting &s, int32_t v) {
    bool changed = false;
    switch (s.type) {
        case T_U8: case T_CURVE: {
            uint8_t n = (uint8_t)constrain((int)v, s.lo, s.hi);
            if (n != *(uint8_t*)s.slot) { *(uint8_t*)s.slot = n; changed = true; }
            break;
        }
        case T_I16: {
            int16_t n = (int16_t)constrain((int)v, s.lo, s.hi);
            if (n != *(int16_t*)s.slot) { *(int16_t*)s.slot = n; changed = true; }
            break;
        }
        case T_BOOL: {
            bool n = (v != 0);
            if (n != *(bool*)s.slot) { *(bool*)s.slot = n; changed = true; }
            break;
        }
    }
    if (changed && s.onChange) s.onChange();
    return changed;
}

static bool settingAdjust(const Setting &s, int8_t dir) {
    if (s.type == T_BOOL)  return settingSet(s, !*(bool*)s.slot);
    if (s.type == T_CURVE) return settingSet(s, wrap(*(uint8_t*)s.slot + dir, 3));
    return settingSet(s, settingGet(s) + dir * s.step);
}

static void settingFormat(const Setting &s, char *out, size_t n) {
    if (s.type == T_BOOL) {
        snprintf(out, n, "%s", *(bool*)s.slot ? "ON" : "OFF");
    } else if (s.type == T_CURVE) {
        const char *c[] = { "LIN", "LOG", "EXP" };
        uint8_t v = *(uint8_t*)s.slot;
        snprintf(out, n, "%s", v < 3 ? c[v] : "?");
    } else {
        snprintf(out, n, "%d", (int)settingGet(s));
    }
}

static const Setting *settingByKey(const char *k) {
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        if (strcmp(SETTINGS[i].jsonKey, k) == 0) return &SETTINGS[i];
    }
    return nullptr;
}

// I16 = 2 bytes little-endian; everything else = 1 byte. 16 bytes max.
static size_t settingsToBytes(uint8_t *buf, size_t cap) {
    size_t off = 0;
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        size_t need = (s.type == T_I16) ? 2 : 1;
        if (off + need > cap) return 0;
        int32_t v = settingGet(s);
        buf[off++] = (uint8_t)(v & 0xFF);
        if (s.type == T_I16) buf[off++] = (uint8_t)((v >> 8) & 0xFF);
    }
    return off;
}

static size_t settingsFromBytes(const uint8_t *buf, size_t len) {
    size_t off = 0;
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        size_t need = (s.type == T_I16) ? 2 : 1;
        if (off + need > len) break;
        int32_t v = buf[off++];
        if (s.type == T_I16) v = (int16_t)((uint16_t)v | ((uint16_t)buf[off++] << 8));
        settingSet(s, v);
    }
    return off;
}

// ─── WiFi config mode ───────────────────────────────────────────────────────
// Page bytes live in web_ui_html.h. web_ui.html is the browser preview
// — keep them in sync by hand.
//
// Forward decls: implementations live further down (Actions / Presets).
static bool setMode(Mode newM);
static bool savePresetTo(uint8_t slot);
static bool loadPresetFrom(uint8_t slot);

static void handleRoot() {
    webServer.send_P(200, "text/html", WEB_UI_HTML);
}

static void handleApiState() {
    char buf[256];
    size_t off = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "{");
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        off += snprintf(buf + off, sizeof(buf) - off, "\"%s\":%ld,",
                        SETTINGS[i].jsonKey, (long)settingGet(SETTINGS[i]));
    }
    off += snprintf(buf + off, sizeof(buf) - off,
                    "\"md\":%u,\"pe\":%u}",
                    (uint8_t)mode, presetsExistMask);
    webServer.send(200, "application/json", buf);
}

static void handleApiLive() {
    char buf[120];
    snprintf(buf, sizeof(buf),
        "{\"d\":%d,\"v\":%d,\"b\":%d,\"mm\":%d,\"c\":%u,\"bp\":%u}",
        liveDist, liveVel, liveBend,
        (int)(ema >= 0 ? ema : 0),
        espNowReady ? 1u : 0u, bypassed ? 1u : 0u);
    webServer.send(200, "application/json", buf);
}

static void handleApiSet() {
    if (!webServer.hasArg("k") || !webServer.hasArg("v")) {
        webServer.send(400, "text/plain", "missing"); return;
    }
    const String &k = webServer.arg("k");
    long v = webServer.arg("v").toInt();
    bool changed = false, persists = true;

    if      (k == "md") changed = setMode((Mode)constrain((int)v, 0, (int)MODE_COUNT - 1));
    else {
        const Setting *s = settingByKey(k.c_str());
        if (!s) { webServer.send(400, "text/plain", "bad key"); return; }
        changed = settingSet(*s, v);
    }

    if (changed) {
        oledDirty = true;
        if (persists) { settingsDirty = true; dirtyTime = millis(); }
    }
    webServer.send(204);
}

static void handleApiPreset() {
    if (!webServer.hasArg("slot") || !webServer.hasArg("op")) {
        webServer.send(400, "text/plain", "missing");
        return;
    }
    int slot = webServer.arg("slot").toInt();
    const String &op = webServer.arg("op");
    if (slot < 0 || slot >= NUM_PRESETS) {
        webServer.send(400, "text/plain", "bad slot");
        return;
    }
    if (op == "load") {
        if (!loadPresetFrom((uint8_t)slot)) { webServer.send(404, "text/plain", "empty"); return; }
        showToast("LOADED");
    } else if (op == "save") {
        if (!savePresetTo((uint8_t)slot)) { webServer.send(500, "text/plain", "err"); return; }
        showToast("SAVED");
    } else {
        webServer.send(400, "text/plain", "bad op");
        return;
    }
    oledDirty = true;
    webServer.send(204);
}

static void webTask(void*) {
    esp_task_wdt_add(nullptr);
    webTaskAlive = true;
    while (configMode) {
        webServer.handleClient();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    esp_task_wdt_delete(nullptr);
    webTaskAlive = false;     // signal exitConfigMode that handleClient won't run again
    vTaskDelete(nullptr);
}

static void enterConfigMode() {
    if (configMode) return;
    configMode = true;

    // Tear down ESP-NOW here, while it's still live and WiFi is up (safe), so
    // the AP switch below is clean and exitConfigMode() can do a fresh init.
    if (espNowReady) { esp_now_deinit(); espNowReady = false; }

    WiFi.mode(WIFI_AP);
    // Pin the AP to the ESP-NOW channel so config mode doesn't move the radio
    // off it. (ESP-NOW TX is still suspended while in AP mode — it resumes when
    // exitConfigMode() re-runs initEspNow().)
    WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_CHANNEL);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // save heap
    delay(100);
    IPAddress ip = WiFi.softAPIP();

    // CLAUDE.md C1 — resolve http://midiguitar.local on hosts that honour
    // mDNS (macOS / iOS always; Windows ≥10 mostly; Android needs Bonjour).
    MDNS.begin("midiguitar");
    MDNS.addService("http", "tcp", 80);

    webServer.on("/",           handleRoot);
    webServer.on("/api/state",  handleApiState);
    webServer.on("/api/live",   handleApiLive);
    webServer.on("/api/set",    handleApiSet);
    webServer.on("/api/preset", handleApiPreset);
    webServer.begin();

    xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, &webTaskH, 0);

    static char ipMsg[24];
    snprintf(ipMsg, sizeof(ipMsg), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    showToast(ipMsg);
#if DEBUG
    Serial.printf("Config mode: http://%s  (or http://midiguitar.local)\n", ipMsg);
    Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
#endif
}

static void exitConfigMode() {
    if (!configMode) return;
    configMode = false;

    // CLAUDE.md A1 — wait for webTask to clear webTaskAlive before stopping
    // the server, otherwise its in-flight handleClient() could run after
    // webServer.stop() returns and dereference a torn-down handler.
    const uint32_t deadline = millis() + 200;
    while (webTaskAlive && (int32_t)(millis() - deadline) < 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    MDNS.end();
    webServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    webTaskH = nullptr;
    // Config mode took WiFi to AP; bring the ESP-NOW link back up for play mode.
    initEspNow();
    showToast("CFG OFF");
#if DEBUG
    Serial.println("Config mode off");
#endif
}

// ─── NeoPixel ────────────────────────────────────────────────────────────────

static uint16_t modeHue() {
    if (configMode) return 21845;  // green during config
    return mode == MODE_CC ? 28000 : 45000;   // CC vs BEND
}

static void triggerWipe() {
    wipeColor = ring.Color(200, 200, 200);   // white wipe (scaled by stripBright)
    wipeUntil = millis() + 150;
}

static void showRing(int16_t val) {
    int n = constrain((val * NUM_PIXELS + 63) / 127, 0, NUM_PIXELS);
    ring.clear();
    for (int i = 0; i < n; i++) {
        // White bar with a brightness ramp along its length (was mode-hued).
        uint8_t bright = (n > 1) ? map(i, 0, n - 1, 76, 255) : 255;
        ring.setPixelColor(i, ring.Color(bright, bright, bright));
    }
    if (liveVel > 100 && n > 0) ring.setPixelColor(n - 1, ring.Color(255, 255, 255));
    ring.show();
}

static void showBreathe() {
    float phase = sinf(millis() * TWO_PI / 2000.0f);
    uint8_t b = 10 + (uint8_t)(30.0f * (1.0f + phase) / 2.0f);
    uint32_t col = ring.Color(b, b, b);   // white breathe (was yellow)
    for (int i = 0; i < NUM_PIXELS; i++) ring.setPixelColor(i, col);
    ring.show();
}

static void showConfig() {
    // Was a rotating rainbow; now a white brightness wave that circles the ring
    // so config mode is still visibly animated without any color.
    uint32_t t = millis();
    for (int i = 0; i < NUM_PIXELS; i++) {
        float w = sinf(t * TWO_PI / 1500.0f + i * TWO_PI / NUM_PIXELS);
        uint8_t b = 30 + (uint8_t)(60.0f * (1.0f + w) / 2.0f);
        ring.setPixelColor(i, ring.Color(b, b, b));
    }
    ring.show();
}

static void updateRing() {
    ring.setBrightness(stripBright);
    if (millis() < wipeUntil) {
        unsigned long elapsed = 150 - (wipeUntil - millis());
        int n = (int)(elapsed * NUM_PIXELS / 150);
        ring.clear();
        for (int i = 0; i < n && i < NUM_PIXELS; i++) ring.setPixelColor(i, wipeColor);
        ring.show();
        return;
    }
    if (configMode) { showConfig();  return; }
    if (bypassed)   { showBreathe(); return; }
    showRing(liveDist);
}

// ─── OLED drawing ───────────────────────────────────────────────────────────

static void drawPageDots(uint8_t current, uint8_t total) {
    int16_t startX = OLED_W / 2 - (total * 8) / 2;
    for (uint8_t i = 0; i < total; i++) {
        int16_t dx = startX + i * 8;
        if (i == current) oled.fillRect(dx, 60, 5, 4, SSD1306_WHITE);
        else              oled.fillRect(dx + 1, 63, 3, 1, SSD1306_WHITE);
    }
}

static void drawHLine(int16_t y) { oled.drawFastHLine(0, y, OLED_W, SSD1306_WHITE); }

static void drawPageHeader(const char *title) {
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print(title);
    oled.setTextColor(SSD1306_WHITE);
}

// Picks layout (slot width, gap) so `count` slots fit centered in OLED_W.
static void drawSegmentedPage(const char *title, const char *const *labels,
                              uint8_t count, uint8_t selIdx, uint8_t pageIdx,
                              int16_t slotW, int16_t slotH) {
    drawPageHeader(title);
    int16_t marginX = (OLED_W - slotW * count - 2 * (count - 1)) / 2;
    for (uint8_t i = 0; i < count; i++) {
        int16_t sx = marginX + i * (slotW + 2);
        bool sel = (i == selIdx);
        if (sel) oled.fillRoundRect(sx, 14, slotW, slotH, 3, SSD1306_WHITE);
        else     oled.drawRoundRect(sx, 14, slotW, slotH, 3, SSD1306_WHITE);
        oled.setTextColor(sel ? SSD1306_BLACK : SSD1306_WHITE);
        int16_t lw = strlen(labels[i]) * 6;
        oled.setCursor(sx + (slotW - lw) / 2, 14 + slotH - 10);
        oled.print(labels[i]);
    }
    oled.setTextColor(SSD1306_WHITE);
    drawPageDots(pageIdx, NUM_PAGES);
}

static int16_t drawBadge(int16_t x, int16_t y, const char *txt) {
    int16_t w = strlen(txt) * 6 + 4;
    oled.fillRoundRect(x, y, w, 9, 2, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(x + 2, y + 1);
    oled.print(txt);
    oled.setTextColor(SSD1306_WHITE);
    return w;
}

static void drawSegBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t val, int16_t maxVal) {
    int16_t fillW = constrain((int32_t)val * w / maxVal, 0, w);
    for (int16_t sx = 0; sx < w; sx += 4) {
        int16_t segW = (sx + 3 <= w) ? 3 : w - sx;
        if (sx < fillW) {
            int16_t fw = (sx + 3 <= fillW) ? segW : max((int16_t)0, (int16_t)(fillW - sx));
            if (fw > 0) oled.fillRect(x + sx, y, fw, h, SSD1306_WHITE);
        }
        if (sx >= fillW && (sx / 4) % 2 == 0) oled.drawPixel(x + sx, y + h / 2, SSD1306_WHITE);
    }
}

static void drawMuteStripes(int16_t x, int16_t y, int16_t w, int16_t h) {
    for (int16_t i = 0; i < w + h; i += 4) {
        int16_t x0 = x + i, y0 = y, x1 = x + i - h, y1 = y + h;
        if (x0 > x + w) { y0 += x0 - (x + w); x0 = x + w; }
        if (x1 < x)     { y1 -= x - x1; x1 = x; }
        if (y0 < y + h && y1 > y) oled.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
    }
}

static void drawPageConfig() {
    drawPageHeader("CONFIG MODE");
    oled.setCursor(0, 16);
    oled.print("WiFi:");
    oled.setCursor(36, 16);
    oled.print(AP_SSID);

    oled.setCursor(0, 28);
    oled.print("Pass:");
    oled.setCursor(36, 28);
    oled.print(AP_PASS);

    oled.setCursor(0, 40);
    oled.print("URL:");
    oled.setCursor(36, 40);
    oled.print("192.168.4.1");
    oled.setCursor(36, 48);
    oled.print("or midiguitar.local");

    oled.setCursor(0, 56);
    oled.print("Triple-tap to exit");
}

static void drawPageLive() {
    int16_t bx = 0;
    // Transport is fixed now: always broadcasting ESP-NOW (+ USB when wired).
    bx += drawBadge(0, 0, "NOW") + 2;
    bx += drawBadge(bx, 0, mode == MODE_CC ? "CC" : "PB") + 2;
    oled.setCursor(bx, 1);
    oled.printf("Ch%d", midiCh);
    if (batMv) {                                   // battery charge, between Ch and the badges
        oled.setCursor(oled.getCursorX() + 4, 1);
        oled.printf("%d%%", batPercent());
    }

    if (!sensorOK)      drawBadge(OLED_W - 22, 0, "ERR");
    else if (bypassed)  drawBadge(OLED_W - 28, 0, "MUTE");

    drawHLine(10);

    int16_t r1 = 12, barX = 20, barW = 70, barH = 7;
    oled.setCursor(0, r1);
    oled.print(mode == MODE_CC ? "DST" : "BND");
    if (bypassed) drawMuteStripes(barX, r1, barW, barH);
    else          drawSegBar(barX, r1, barW, barH, liveDist, 127);
    oled.setCursor(94, r1 - 1);
    oled.printf("%3d", liveDist);

    drawHLine(22);

    int16_t r2 = 24;
    oled.setCursor(0, r2);
    oled.print("VEL");
    if (bypassed) drawMuteStripes(barX, r2, barW, barH);
    else          drawSegBar(barX, r2, barW, barH, liveVel, 127);
    oled.setCursor(94, r2 - 1);
    oled.printf("%3d", liveVel);

    drawHLine(34);

    if (mode == MODE_PB) {
        oled.setCursor(0, 36);
        oled.printf("BEND %5d", liveBend);
    } else {
        oled.setCursor(0, 36);
        oled.printf("CC%d  CC%d", ccDist, ccVel);
    }
    oled.setCursor(80, 36);
    if (ema >= 0) oled.printf("%3dmm", (int)ema);

    drawHLine(45);

    int16_t gy = 47, gh = 12;
    if (bypassed) {
        for (int16_t dx = 0; dx < OLED_W; dx += 6) {
            oled.drawFastHLine(dx, gy, 3, SSD1306_WHITE);
            oled.drawFastHLine(dx, gy + gh - 1, 3, SSD1306_WHITE);
        }
    } else {
        drawSegBar(0, gy, OLED_W, gh, liveDist, 127);
    }
    for (int i = 1; i < 4; i++) {
        int16_t tx = OLED_W * i / 4;
        int16_t gFill = constrain((int32_t)liveDist * OLED_W / 127, 0, OLED_W);
        uint16_t tc = (gFill > tx) ? SSD1306_BLACK : SSD1306_WHITE;
        oled.drawFastVLine(tx, gy, 3, tc);
        oled.drawFastVLine(tx, gy + gh - 3, 3, tc);
    }

    drawPageDots(0, NUM_PAGES);
}

static void drawPageMode() {
    static const char *L[] = { "CC", "BEND" };
    drawSegmentedPage("MODE", L, MODE_COUNT, (uint8_t)mode, 1, 58, 40);
}

// ─── Settings page (table-driven) ──────────────────────────────────────────

static void getSettingInfo(uint8_t idx, char *label, char *val, size_t vLen) {
    if (idx >= NUM_SETTINGS) return;
    strcpy(label, SETTINGS[idx].label);
    settingFormat(SETTINGS[idx], val, vLen);
}

static void adjustSetting(uint8_t idx, int8_t dir) {
    if (idx >= NUM_SETTINGS) return;
    settingAdjust(SETTINGS[idx], dir);
    settingsDirty = true;
    dirtyTime = millis();
}

// ─── Presets ───────────────────────────────────────────────────────────────

static void presetKey(char *out, size_t n, uint8_t slot) {
    snprintf(out, n, "preset_%u", slot);
}

static void refreshPresetsExist() {
    presetsExistMask = 0;
    prefs.begin("midi_guitar", true);
    for (uint8_t i = 0; i < NUM_PRESETS; i++) {
        char k[12]; presetKey(k, sizeof(k), i);
        if (prefs.isKey(k)) presetsExistMask |= (1u << i);
    }
    prefs.end();
}

// Preset format: settingsToBytes payload + 1 trailing byte for mode.
// Older presets without the mode byte still load (mode left unchanged).
static bool savePresetTo(uint8_t slot) {
    if (slot >= NUM_PRESETS) return false;
    uint8_t buf[32];
    size_t n = settingsToBytes(buf, sizeof(buf));
    if (n == 0 || n + 1 > sizeof(buf)) return false;
    buf[n++] = (uint8_t)mode;
    char k[12]; presetKey(k, sizeof(k), slot);
    prefs.begin("midi_guitar", false);
    size_t wrote = prefs.putBytes(k, buf, n);
    prefs.end();
    if (wrote == n) presetsExistMask |= (1u << slot);
    return wrote == n;
}

static bool loadPresetFrom(uint8_t slot) {
    if (slot >= NUM_PRESETS) return false;
    if (!((presetsExistMask >> slot) & 1)) return false;
    char k[12]; presetKey(k, sizeof(k), slot);
    uint8_t buf[32];
    prefs.begin("midi_guitar", true);
    size_t got = prefs.getBytes(k, buf, sizeof(buf));
    prefs.end();
    if (got == 0) return false;
    size_t off = settingsFromBytes(buf, got);
    if (off < got) setMode((Mode)constrain((int)buf[off], 0, (int)MODE_COUNT - 1));
    prevDist = -1; prevBend = -1; prevVel = -1;
    settingsDirty = true;
    dirtyTime = millis();
    return true;
}

static void drawPageSettings() {
    drawPageHeader("SETTINGS");
    if (settingsEditing) {
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(OLED_W - 24, 2); oled.print("EDIT");
        oled.setTextColor(SSD1306_WHITE);
    } else {
        // CLAUDE.md A3 — live heap monitor in the header (KB).
        // Hidden in plain sight: only appears when not editing, so the EDIT
        // badge takes precedence. Useful for catching leaks when adding
        // features without burning a whole page.
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(OLED_W - 28, 2);
        oled.printf("H%uk", (unsigned)(ESP.getFreeHeap() / 1024));
        oled.setTextColor(SSD1306_WHITE);
    }

    int16_t rowH = 10, startY = 13;
    int8_t scrollTop = max(0, settingsCursor - 2);
    if (scrollTop + 5 > NUM_SETTINGS) scrollTop = max(0, NUM_SETTINGS - 5);

    for (int8_t i = 0; i < 5 && scrollTop + i < NUM_SETTINGS; i++) {
        int8_t idx = scrollTop + i;
        int16_t ry = startY + i * rowH;
        bool sel = (idx == settingsCursor);
        char label[10], val[8];
        getSettingInfo(idx, label, val, sizeof(val));

        if (sel) { oled.fillRect(0, ry, OLED_W, rowH, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
        else     { oled.setTextColor(SSD1306_WHITE); }

        oled.setCursor(2, ry + 1);
        oled.print(label);

        int16_t vw = strlen(val) * 6;
        bool blink = sel && settingsEditing && ((millis() / 300) % 2);
        if (!blink) { oled.setCursor(OLED_W - vw - 4, ry + 1); oled.print(val); }
        if (sel && settingsEditing) {
            oled.setCursor(OLED_W - vw - 10, ry + 1); oled.print("<");
            oled.setCursor(OLED_W - 4, ry + 1);       oled.print(">");
        }
        oled.setTextColor(SSD1306_WHITE);
    }

    if (NUM_SETTINGS > 5) {
        int16_t sbH = 50 * 5 / NUM_SETTINGS;
        int16_t sbY = startY + (50 - sbH) * scrollTop / (NUM_SETTINGS - 5);
        oled.drawFastVLine(OLED_W - 1, startY, 50, SSD1306_WHITE);
        oled.fillRect(OLED_W - 2, sbY, 2, sbH, SSD1306_WHITE);
    }
    drawPageDots(2, NUM_PAGES);
}

static void drawPagePresets() {
    drawPageHeader("PRESETS");
    int16_t cellW = 28, cellH = 18, gap = 2;
    int16_t startX = (OLED_W - (cellW * 4 + gap * 3)) / 2;

    for (uint8_t i = 0; i < NUM_PRESETS; i++) {
        int16_t row = i / 4, col = i % 4;
        int16_t x   = startX + col * (cellW + gap);
        int16_t y   = 14 + row * (cellH + 2);
        bool sel    = (i == (uint8_t)presetCursor);
        bool exists = (presetsExistMask >> i) & 1;

        if (sel) {
            oled.fillRoundRect(x, y, cellW, cellH, 2, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else if (exists) {
            oled.drawRoundRect(x, y, cellW, cellH, 2, SSD1306_WHITE);
            oled.setTextColor(SSD1306_WHITE);
        } else {
            for (int16_t dx = 0; dx < cellW; dx += 3) {
                oled.drawPixel(x + dx, y, SSD1306_WHITE);
                oled.drawPixel(x + dx, y + cellH - 1, SSD1306_WHITE);
            }
            for (int16_t dy = 0; dy < cellH; dy += 3) {
                oled.drawPixel(x, y + dy, SSD1306_WHITE);
                oled.drawPixel(x + cellW - 1, y + dy, SSD1306_WHITE);
            }
            oled.setTextColor(SSD1306_WHITE);
        }
        char lbl[4]; snprintf(lbl, sizeof(lbl), "P%u", i + 1);
        int16_t lw = strlen(lbl) * 6;
        oled.setCursor(x + (cellW - lw) / 2, y + (cellH - 7) / 2);
        oled.print(lbl);
        oled.setTextColor(SSD1306_WHITE);
    }

    oled.setCursor(0, 54);
    oled.print("TAP load HOLD save");
    drawPageDots(3, NUM_PAGES);
}

// ─── NVS ─────────────────────────────────────────────────────────────────────

static void loadSettings() {
    prefs.begin("midi_guitar", true);
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        switch (s.type) {
            case T_U8: case T_CURVE: *(uint8_t*)s.slot = prefs.getUChar(s.nvsKey, *(uint8_t*)s.slot); break;
            case T_I16:              *(int16_t*)s.slot = prefs.getShort(s.nvsKey, *(int16_t*)s.slot); break;
            case T_BOOL:             *(bool*)s.slot    = prefs.getBool (s.nvsKey, *(bool*)s.slot); break;
        }
    }
    uint8_t md = prefs.getUChar("mode", MODE_CC);
    mode       = (md < MODE_COUNT) ? (Mode)md : MODE_CC;
    prefs.end();
}

static void saveSettings() {
    prefs.begin("midi_guitar", false);
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        switch (s.type) {
            case T_U8: case T_CURVE: prefs.putUChar(s.nvsKey, *(uint8_t*)s.slot); break;
            case T_I16:              prefs.putShort(s.nvsKey, *(int16_t*)s.slot); break;
            case T_BOOL:             prefs.putBool (s.nvsKey, *(bool*)s.slot); break;
        }
    }
    prefs.putUChar("mode", mode);
    prefs.end();
    settingsDirty = false;
}

static void refreshOLED() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    if (configMode) {
        drawPageConfig();
    } else {
        switch (currentPage) {
            case PG_LIVE:     drawPageLive();     break;
            case PG_MODE:     drawPageMode();     break;
            case PG_SETTINGS: drawPageSettings(); break;
            case PG_PRESETS:  drawPagePresets();  break;
        }
    }
    if (toastText && millis() < toastUntil) {
        int16_t tw = strlen(toastText) * 6 + 8;
        int16_t tx = (OLED_W - tw) / 2;
        oled.fillRoundRect(tx, 24, tw, 16, 3, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setTextSize(1);
        oled.setCursor(tx + 4, 28);
        oled.print(toastText);
        oled.setTextColor(SSD1306_WHITE);
    }
    oled.display();
    oledDirty = false;
}

// ─── Intro ───────────────────────────────────────────────────────────────────

static void playIntro() {
#if DEBUG
    Serial.println("[boot] intro");
#endif
    oled.clearDisplay();
    oled.display();
    uint16_t hue = modeHue();
    for (int16_t x = 0; x < OLED_W; x += 4) {
        oled.drawLine(0, 31, x, 31, SSD1306_WHITE);
        oled.display();
        uint8_t p = (x / 4) % NUM_PIXELS;
        ring.clear();
        ring.setPixelColor(p, ring.ColorHSV(hue, 255, 120));
        ring.show();
    }
    delay(60);
    const char *title = "M I D I G U I T A R";
    uint8_t len = strlen(title);
    int16_t startX = (OLED_W - len * 6) / 2;
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    for (uint8_t i = 0; i < len; i++) {
        oled.setCursor(startX + i * 6, 28);
        oled.print(title[i]);
        if (title[i] != ' ') {
            oled.display();
            delay(40);
            ring.fill(ring.ColorHSV(hue, 200, 40 + (i % 3) * 30));
            ring.show();
        }
    }
    oled.setCursor((OLED_W - 12) / 2, 40);
    oled.print("v5");
    oled.display();
    delay(400);
    for (int16_t i = 0; i < OLED_H / 2; i += 2) {
        oled.fillRect(0, i, OLED_W, 2, SSD1306_BLACK);
        oled.fillRect(0, OLED_H - i - 2, OLED_W, 2, SSD1306_BLACK);
        oled.display();
        ring.fill(ring.ColorHSV(hue, 200, 80 - 80 * i / (OLED_H / 2)));
        ring.show();
        delay(15);
    }
    oled.clearDisplay();
    oled.display();
    ring.clear();
    ring.show();
}

// CLAUDE.md A5 — boot self-test card. Runs once between sensor init and
// the WDT arm so a failed sensor (or unexpected I²C device count) is
// visible without a serial console. Pre-WDT, so the 800 ms hold is safe.
static void drawBootBadges(bool oledOK, uint8_t i2cDev, bool sensorOK) {
    if (!oledOK) return;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("BOOT SELF-TEST");
    oled.setTextColor(SSD1306_WHITE);

    oled.setCursor(0, 18);  oled.print("OLED");
    oled.setCursor(60, 18); oled.print("OK");          // got here = OLED responded

    oled.setCursor(0, 32);  oled.print("I2C");
    oled.setCursor(60, 32); oled.printf("%u dev", i2cDev);

    oled.setCursor(0, 46);  oled.print("SENSOR");
    oled.setCursor(60, 46); oled.print(sensorOK ? "OK" : "FAIL");

    oled.display();
    delay(800);
}

// ─── Actions ─────────────────────────────────────────────────────────────────

// Setters return true iff state actually changed. Shared between the
// encoder cycle path and the web `/api/set` handler so the ring wipe and
// live-value resets stay in one place.
static bool setMode(Mode newM) {
    if (newM == mode) return false;
    mode = newM;
    prevDist = -1; prevBend = -1; liveDist = 0;
    triggerWipe();
    return true;
}

static void cycleMode(int8_t dir) {
    setMode((Mode)wrap((int8_t)mode + dir, MODE_COUNT));
    settingsDirty = true; dirtyTime = millis();
    showToast(mode == MODE_CC ? "CC" : "BEND");
}

static void toggleBypass() {
    bypassed = !bypassed;
    showToast(bypassed ? "MUTED" : "ACTIVE");
}

// SW1 on D1: a press toggles mute on any page, like the encoder tap on PG_LIVE.
static void handleButton(uint32_t now) {
#if BTN_PIN >= 0
    static bool     prev   = false;
    static uint32_t tPress = 0;
    const bool down = digitalRead(BTN_PIN) == LOW;
    if (down && !prev && now - tPress > DEBOUNCE_MS) {
        tPress = now;
        toggleBypass();
        lastActivity = now;
        if (oledAsleep) { oledAsleep = false; oledDirty = true; }
    }
    prev = down;
#endif
}

// Battery on D0 through a 100k/100k divider. Reads once a second, smoothed.
// Below 2.5 V there is no battery (switch off or unplugged) and nothing shows.
static void readBattery(uint32_t now) {
#if BAT_PIN >= 0
    static uint32_t tBat   = 0;
    static bool     warned = false;
    if (now - tBat < 1000) return;
    tBat = now;
    const uint32_t mv = analogReadMilliVolts(BAT_PIN) * 2;
    if (mv < 2500)     { batMv = 0; return; }
    batMv = batMv ? (uint16_t)((batMv * 3 + mv) / 4) : (uint16_t)mv;
    if (!warned && batMv < BAT_LOW_MV) { warned = true; showToast("LOW BAT"); }
    if (warned && batMv > BAT_LOW_MV + 100) warned = false;
#endif
}

static int batPercent() {
    return constrain(((int)batMv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV), 0, 100);
}

// ─── Encoder + menu ──────────────────────────────────────────────────────────

// Encoder push gestures:
//   short tap (release < 400 ms) — page contextual action
//                                  (PG_LIVE: bypass, PG_MODE: cycle,
//                                   PG_SETTINGS: toggle edit, PG_PRESETS: load)
//   long press (held ≥ 600 ms)   — advance to next page
//   long press on PG_PRESETS     — save preset (≥ 1500 ms)
//   triple-tap (3 short ≤ 600 ms) — toggle WiFi config mode
//
// Short taps are deferred until the tap-window closes so a triple-tap
// doesn't fire stray contextual actions on its way to config mode.
static void handleEncoder() {
    noInterrupts();
    int8_t delta = encDelta;
    encDelta = 0;
    interrupts();

    static bool          prev       = false;
    static uint32_t      pressStart = 0;
    static uint32_t      firstTapAt = 0;
    static uint8_t       pendingTaps = 0;
    static bool          longFired  = false;
    static bool          saveFired  = false;

    bool     pushed = digitalRead(ENC_PUSH) == LOW;
    uint32_t now    = millis();

    // Press edge
    if (pushed && !prev) {
        pressStart = now;
        longFired  = false;
        saveFired  = false;
    }

    // Held — fire long-hold actions exactly once
    if (pushed && !configMode) {
        if (!saveFired && currentPage == PG_PRESETS && now - pressStart >= ENC_SAVE_MIN_MS) {
            saveFired = true;
            longFired = true;  // suppress page-advance below
            showToast(savePresetTo((uint8_t)presetCursor) ? "SAVED" : "ERR");
            oledDirty = true;
        }
        if (!longFired && now - pressStart >= ENC_HOLD_MIN_MS) {
            longFired = true;
            currentPage = (Page)wrap((int8_t)currentPage + 1, NUM_PAGES);
            oledDirty = true;
        }
    }

    // Release edge — register a short tap for the tap-window logic
    if (!pushed && prev) {
        uint32_t held = now - pressStart;
        if (held < ENC_TAP_MAX_MS && !longFired && !saveFired) {
            if (now - firstTapAt > CFG_TAP_WINDOW) {
                pendingTaps = 1;
                firstTapAt  = now;
            } else {
                pendingTaps++;
            }
        }
    }

    // Tap window expired — flush pending taps
    if (pendingTaps > 0 && now - firstTapAt > CFG_TAP_WINDOW) {
        if (pendingTaps >= CFG_TAP_COUNT) {
            configMode ? exitConfigMode() : enterConfigMode();
        } else if (!configMode) {
            for (uint8_t i = 0; i < pendingTaps; i++) {
                switch (currentPage) {
                    case PG_LIVE:     toggleBypass();                                                       break;
                    case PG_MODE:     cycleMode(1);                                                         break;
                    case PG_SETTINGS: settingsEditing = !settingsEditing;                                   break;
                    case PG_PRESETS:  showToast(loadPresetFrom((uint8_t)presetCursor) ? "LOADED" : "EMPTY"); break;
                    default: break;
                }
            }
        }
        pendingTaps = 0;
        oledDirty = true;
    }

    prev = pushed;

    // Rotation
    if (delta == 0) return;
    oledDirty = true;
    if (configMode) return;

    switch (currentPage) {
        case PG_LIVE: {
            const Setting *s = settingByKey("brt");
            if (s && settingAdjust(*s, delta > 0 ? 1 : -1)) {
                settingsDirty = true; dirtyTime = millis();
            }
            break;
        }
        case PG_MODE:      cycleMode(delta > 0 ? 1 : -1);      break;
        case PG_SETTINGS:
            if (settingsEditing) adjustSetting(settingsCursor, delta > 0 ? 1 : -1);
            else settingsCursor = constrain(settingsCursor + (delta > 0 ? 1 : -1), 0, NUM_SETTINGS - 1);
            break;
        case PG_PRESETS:
            presetCursor = constrain(presetCursor + (delta > 0 ? 1 : -1), 0, NUM_PRESETS - 1);
            break;
        default: break;
    }
}


// ─── Sensor poll ─────────────────────────────────────────────────────────────

static void pollSensor() {
    // Rate-limit the I²C dataReady poll to the sensor's own measurement
    // period. checkForDataReady() returned false ~99% of the time when
    // called every loop iteration — that's a hot-path I²C transaction
    // saved for nothing.
    static uint32_t tNextPoll = 0;
    uint32_t now = millis();
    if ((int32_t)(now - tNextPoll) < 0) return;
    tNextPoll = now + POLL_MS;

    if (!sensor.checkForDataReady()) return;
    sReady++;
    int16_t dist = -1;
    if (sensor.getRangeStatus() == 0) dist = sensor.getDistance();
    sensor.clearInterrupt();
    if (dist < 0) return;
    sValid++;

    prevEma = ema;
    updateEma(ema, (float)dist, EMA_ALPHA);
    float curved = normCurve(ema, distMinMm, distMaxMm);
    if (distInvert) curved = 1.0f - curved;

    if (mode == MODE_CC) {
        int16_t cc = constrain((int16_t)lroundf(curved * 127.0f), 0, 127);
        liveDist = cc;
        if (cc != prevDist) { prevDist = cc; sendCC(ccDist, cc); }
    } else {
        int16_t b = (int16_t)constrain((int)lroundf(curved * 16383.0f), 0, 16383);
        liveBend = b;
        liveDist = b / (16383 / 127);
        if (b != prevBend) { prevBend = b; sendPB(b); }
    }

    float d = fabsf(ema - prevEma);
    if (d < VEL_DEAD_MM) d = 0.0f;
    int16_t v = (int16_t)lroundf(constrain(d / VEL_MAX_MM * 127.0f, 0.0f, 127.0f));
    liveVel = v;
    if (v != prevVel) { prevVel = v; sendCC(ccVel, v); }
}

// ─── Setup / loop ────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
#if DEBUG
    delay(BOOT_SERIAL_WAIT_MS);          // give the host time to attach the serial monitor
    Serial.println("\n[boot] tof_midi_v5");
#endif

    // CLAUDE.md D1 — factory reset gesture. Hold ENC_PUSH for 5+ s at boot
    // and the NVS namespace is cleared. Runs BEFORE loadSettings() so we
    // don't load values we're about to erase. No OLED yet (not initialised),
    // so the user holds blind; the auto-reboot is the confirmation.
    pinMode(ENC_PUSH, INPUT_PULLUP);
    if (digitalRead(ENC_PUSH) == LOW) {
        uint32_t t0 = millis();
        while (digitalRead(ENC_PUSH) == LOW && millis() - t0 < 5000) {
            delay(20);
        }
        if (millis() - t0 >= 5000) {
            Preferences p;
            p.begin("midi_guitar", false);
            p.clear();
            p.end();
#if DEBUG
            Serial.println("[boot] FACTORY RESET — NVS cleared, rebooting");
#endif
            delay(300);
            ESP.restart();
        }
    }

    loadSettings();
    refreshPresetsExist();

    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_PUSH, INPUT_PULLUP);
#if BTN_PIN >= 0
    pinMode(BTN_PIN, INPUT_PULLUP);
#endif
    attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);

    // I2C + OLED + NeoPixel come up before USB/ESP-NOW so the user sees feedback
    // immediately and the radios can't disturb bus settling.
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
    delay(50);

    // Bus warm-up scan — the SSD1306 clone on this PCB rejects the init
    // sequence unless dummy I²C traffic precedes it. Keep this even with
    // DEBUG off; the prints are diagnostic but the loop is load-bearing.
    uint8_t i2cFound = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            i2cFound++;
#if DEBUG
            Serial.printf("[I2C] 0x%02X ACK\n", a);
#endif
        }
    }
#if DEBUG
    Serial.printf("[I2C] %u device(s)\n", i2cFound);
    Serial.print("[boot] oled.begin ... ");
#endif
    bool oledOK = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
#if DEBUG
    Serial.println(oledOK ? "OK" : "FAIL — no ACK at 0x3C");
#endif

    ring.begin();
    ring.setBrightness(stripBright);
    ring.show();

    if (oledOK) playIntro();

    // USB-MIDI. No USB.enableDFU() — the extra DFU interface in the composite
    // (CDC+DFU+MIDI) can make a host enumerate the serial port but skip MIDI, so
    // the guitar wouldn't appear as a MIDI device when tethered. Clean CDC+MIDI
    // enumerates everywhere. (Normal serial-bootloader flashing still works.)
    usbMidi.begin();
    USB.begin();
    initEspNow();   // always-on ESP-NOW broadcast link to the v6 dongle

    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        if (initSensor()) { sensorOK = true; break; }
        delay(200);
    }
#if DEBUG
    Serial.printf("[boot] sensor %s\n", sensorOK ? "OK" : "FAIL");
#endif
    if (!sensorOK) showToast("NO SENSOR");

    drawBootBadges(oledOK, i2cFound, sensorOK);

    // Bumped to 10s — WiFi init can stall longer than 5s on first boot
    const esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = 10000,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_init(&wdtCfg);
    esp_task_wdt_add(nullptr);

    lastActivity = millis();
    oledDirty = true;

#if BOOT_INTO_CONFIG
    enterConfigMode();
#endif
}

void loop() {
    esp_task_wdt_reset();
    const uint32_t now = millis();   // cache once — every read below uses this

#if DEBUG
    // ESP-NOW TX diagnostic. rdy=1: initEspNow OK. tx: successful broadcasts —
    // climbs ~1/s from the keepalive even idle, faster as the sensor moves.
    // err: last esp_now_send() return (0=OK). ch: real radio channel (must be 1).
    // srdy/sval: VL53L1X data-ready vs valid-distance reads — if srdy climbs but
    // sval doesn't, the sensor ranges but sees no target (nothing in range).
    static uint32_t tDbg = 0;
    if (now - tDbg > 500) {
        tDbg = now;
        uint8_t prim = 0; wifi_second_chan_t sec;
        esp_wifi_get_channel(&prim, &sec);
        Serial.printf("[now] rdy=%d tx=%lu err=%d ch=%u | sensor=%d srdy=%lu sval=%lu d=%d bypass=%d %s\n",
                      espNowReady, (unsigned long)espNowTx, espNowErr, (unsigned)prim,
                      sensorOK, (unsigned long)sReady, (unsigned long)sValid, liveDist,
                      bypassed, mode == MODE_CC ? "CC" : "PB");
    }
#endif

    // ESP-NOW keepalive — 1 Hz MIDI Active Sensing (0xFE). The dongle counts it
    // (so link reads UP even when the sensor is idle) but doesn't forward it as a
    // note. Sends regardless of bypass so the link indicator never drops.
    static uint32_t tKeep = 0;
    if (espNowReady && now - tKeep >= 250) {   // 4 Hz — link stays UP through a few dropped frames
        tKeep = now;
        espNowSend(0xFE, 0, 0);
    }

    const bool encMoved = (encDelta != 0);

    handleEncoder();
    handleButton(now);
    readBattery(now);
    if (currentPage != PG_SETTINGS && settingsEditing) settingsEditing = false;

    if (sensorOK) pollSensor();

    if (now - tPoll >= POLL_MS) {
        tPoll = now;
        updateRing();
        if (currentPage == PG_LIVE && sensorOK)              oledDirty = true;
        if (currentPage == PG_SETTINGS && settingsEditing)   oledDirty = true;
        if (configMode)                                       oledDirty = true;
    }

    if (encMoved || digitalRead(ENC_PUSH) == LOW || liveVel > LIVE_VEL_THRESH) {
        lastActivity = now;
        if (oledAsleep) { oledAsleep = false; oledDirty = true; }
    }

    if (settingsDirty && now - dirtyTime > NVS_SAVE_DEBOUNCE_MS) saveSettings();

    if (toastText) {
        if (now < toastUntil) oledDirty = true;
        else                  toastText = nullptr;    // drop stale literal pointer
    }

    // No screensaver while in config mode — the user is actively reading it.
    if (!configMode && !oledAsleep && now - lastActivity > OLED_SLEEP_MS) {
        oledAsleep = true;
        oled.clearDisplay();
        oled.display();
    }

    if (!oledAsleep && oledDirty && now - tOled >= OLED_REFRESH_MS) {
        tOled = now;
        refreshOLED();
    }
}

// tof_midi_v6.ino — hands-free ToF MIDI + expression controller (Heat)
//
// Receiver-dongle PCB target (../tof_midi_v6_rx/heat/kicad/). Hand gesture
// over a VL53L5CX 8×8 ToF sensor drives MIDI (USB + DIN/TRS) and a single
// expression output (MCP4561 digital pot — emulates an expression pedal to
// control other gear, Dimension-Shifter style). ESP-NOW RX bridges MIDI
// from the v5 guitar TX. NeoPixel stick gives gesture visual feedback.
//
//   ESP-NOW RX (from v5 guitar TX)  ─┐
//   Local VL53L5CX 8×8 ToF grid     ─┼─▶  mode router (DRONE/RHYTHM/DRUMS)
//                                                │
//                                  ┌─────────────┼──────────────┬───────────┐
//                                  ▼             ▼              ▼           ▼
//                              USB MIDI     DIN+TRS MIDI    MCP4561      SSD1306 OLED
//                              (TinyUSB)    (Serial1 TX)    digipot      (live+pages)
//                                                          → 1× EXP out  + NeoPixel viz
//
// Cross-references:
//   ../tof_midi_v5/tof_midi_v5.ino       — page system, settings, web UI, presets
//   ../tof_midi_v6_rx/tof_midi_v6_rx.ino — ESP-NOW RX, bypass mux
//   ../tof_midi_v6_rx/heat/kicad/BOM.md  — authoritative parts list
//   ../tof_midi_v6_rx/heat/kicad/PCB_WIRING.md — pin-by-pin wiring
//
// Board: XIAO_ESP32S3 (Plus module in a generic 14-pin socket — Plus
//        underside pads D11..D19 are NOT exposed on this PCB).
//        USB: USB-OTG (TinyUSB).
//
// Libs (Arduino IDE Library Manager):
//   SparkFun VL53L5CX Arduino Library
//   Adafruit NeoPixel
//   Adafruit SSD1306, Adafruit GFX
//   (MCP4561 digipot driven over raw Wire — no library needed)
//   (ESP-NOW + USBMIDI ship with arduino-esp32 core)
//   (Preferences + WebServer + WiFi ship with arduino-esp32 core)
//
// ─── Build / port milestones ──────────────────────────────────────────────
//   [M0] Skeleton compiles, OLED splash, USB MIDI enumerates. — DONE
//   [M1] ESP-NOW RX path verified (lifted from v6_rx). — DONE
//   [M2] VL53L5CX bring-up at 0x29, 8×8 @ 15 Hz, sharpener 20%, CLOSEST. — DONE (untested)
//   [M3] Feature extraction: distMm, liveVel, centroidX/Y, coverage. — DONE (untested)
//   [M4] MCP4561 digipot expression OUTPUT (1 wiper from a ToF feature). — DONE (untested)
//   [M5] Mode router (DRONE / RHYTHM / DRUMS) → MIDI only (no CV). — DONE (untested)
//   [M6] DIN + TRS MIDI via Serial1 TX, mirrors USB. — DONE (untested)
//   [M7] OLED pages: LIVE / MODE / SETTINGS / PRESETS / INFO. — DONE (untested)
//   [M8] NVS settings + 8-slot presets + WiFi config web UI. — DONE (untested)
//   [M9] NeoPixel gesture feedback (distance bar + velocity colour). — DONE (untested)

#define DEBUG 1

// ─── Pin map — generic XIAO ESP32-S3 socket (only 14 top-side pads) ─────
// XIAO Dn → GPIO:
//   D0=1 D1=2 D2=3 D3=4 D4=5 D5=6 D6=43 D7=44 D8=7 D9=8 D10=9
//
// Pads 1/2/8 (D0/D1/D7) are the free pads on the current board; buttons
// take D0/D1, D7 is spare. D2/GPIO3 is a strapping pin — left unused.
// The expression output is a digital pot on the I²C bus (no GPIO), and
// the bypass/mute is on the encoder tap + BTN_MUTE (no footswitch).

#define I2C_SDA      5    // D4 → ToF + OLED + MCP4561 digipot bus
#define I2C_SCL      6    // D5
#define I2C_FREQ     400000

#define ENC_A        7    // D8 → encoder channel A
#define ENC_B        8    // D9 → encoder channel B
#define ENC_PUSH     4    // D3 → encoder pushbutton to GND

#define MIDI_TX_PIN  43   // D6 → Serial1 TX → DIN + TRS jacks, 220 Ω series
#define BTN_MUTE     1    // D0 → mute button (all-notes-off panic)   [free pad]
#define BTN_MODE     2    // D1 → mode-cycle button (DRONE/RHYTHM/DRUMS) [free pad]

// NeoPixel data → level shifter (3.3→5V) → stick DIN. Drives the existing
// level-shifter input pad (v5 STRIP_PIN = D10). Confirm against the board.
#define NEOPIXEL_PIN 9    // D10 → level-shifter A-input
#define NUM_PIXELS   8

#define OLED_W       128
#define OLED_H       64
#define OLED_ADDR    0x3C

#define VL53L5CX_ADDR 0x29
#define DIGIPOT_ADDR  0x2E   // MCP4561 single I²C digipot (A0 strap → 0x2E,
                             // clear of ToF 0x29 and OLED 0x3C)

#define NUM_PAGES    5       // LIVE / MODE / SETTINGS / PRESETS / INFO

// ─── Loop tunables ───────────────────────────────────────────────────────
#define OLED_REFRESH_MS       66      // ~15 fps
#define BOOT_SERIAL_WAIT_MS   2000
#define NO_SIGNAL_MS          1500    // ESP-NOW link timeout
#define TOF_POLL_MS           10      // i.e. ≥ 100 Hz check vs the 15 Hz frame rate
#define NVS_SAVE_DEBOUNCE_MS  2000

// Encoder push gestures
#define ENC_TAP_MAX_MS        400
#define ENC_HOLD_MIN_MS       600
#define ENC_SAVE_MIN_MS       1500   // long-hold on PG_PRESETS → save

// Tactile button debounce
#define BTN_DEBOUNCE_MS       30

// WiFi config mode (triple-tap encoder push)
#define AP_SSID               "Heat-Config"
#define AP_PASS               "12345678"
#define CFG_TAP_WINDOW        600
#define CFG_TAP_COUNT         3

// ─── Includes ────────────────────────────────────────────────────────────
#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>
#include <SparkFun_VL53L5CX_Library.h>
#include <Preferences.h>
#include <WebServer.h>

// ─── ESP-NOW packet (must stay byte-identical with v5 TX) ────────────────
#define ESPNOW_MAGIC 0xCB
struct __attribute__((packed)) EspNowMidiPacket {
    uint8_t magic;
    uint8_t seq;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};
static_assert(sizeof(EspNowMidiPacket) == 5,
              "EspNowMidiPacket must stay 5 bytes — packed struct broke");

// ─── Types ───────────────────────────────────────────────────────────────
enum Mode : uint8_t { MODE_DRONE, MODE_RHYTHM, MODE_DRUMS, MODE_COUNT };
enum Page : uint8_t { PG_LIVE, PG_MODE, PG_SETTINGS, PG_PRESETS, PG_INFO };

enum SType : uint8_t { T_U8, T_I16, T_BOOL };
struct Setting {
    const char *name;       // short label for OLED (≤7 chars)
    const char *nvsKey;     // NVS key (≤7 chars per Preferences limit)
    SType       type;
    void       *ptr;
    int32_t     lo;
    int32_t     hi;
    int32_t     step;
};

// ─── Persisted runtime settings (NVS-backed) ─────────────────────────────
// Defaults applied at boot; overwritten by loadSettings() if NVS has values.
static uint8_t  midiCh       = 1;      // mode router channel
static uint8_t  digiSrc      = 0;      // digipot expression source: 0=dist 1=vel 2=cX 3=cov
static bool     digiInvert   = false;  // flip the expression wiper direction
static uint8_t  ccDist       = 20;     // CC# for distance
static uint8_t  ccVel        = 21;     // CC# for velocity
static uint8_t  ccCentroidX  = 22;     // CC# for centroidX
static uint8_t  ccCoverage   = 23;     // CC# for coverage
static int16_t  distMinMm    = 100;
static int16_t  distMaxMm    = 1000;
static uint8_t  trigThresh   = 30;     // 0..127 — velocity-CC trigger
static uint8_t  releaseThresh = 8;
static uint8_t  velFloor     = 20;
static uint8_t  drumGateMs   = 40;
static uint8_t  droneRoot    = 48;     // C3
static uint8_t  rhythmRoot   = 48;
static bool     distInvert   = false;

#define NUM_SETTINGS 15
static const Setting SETTINGS[NUM_SETTINGS] = {
    { "MIDI Ch", "midiCh",   T_U8,   &midiCh,        1,  16,   1 },
    { "Expr Src","eSrc",     T_U8,   &digiSrc,       0,   3,   1 },  // digipot source
    { "Expr Inv","eInv",     T_BOOL, &digiInvert,    0,   1,   1 },
    { "CC Dist", "ccDist",   T_U8,   &ccDist,        0, 127,   1 },
    { "CC Vel",  "ccVel",    T_U8,   &ccVel,         0, 127,   1 },
    { "CC CtrX", "ccCx",     T_U8,   &ccCentroidX,   0, 127,   1 },
    { "CC Cov",  "ccCov",    T_U8,   &ccCoverage,    0, 127,   1 },
    { "Dist Lo", "dMin",     T_I16,  &distMinMm,    20, 600,  20 },
    { "Dist Hi", "dMax",     T_I16,  &distMaxMm,   400,3000, 100 },
    { "Trig",    "trig",     T_U8,   &trigThresh,    5, 127,   5 },
    { "Release", "rls",      T_U8,   &releaseThresh, 0,  60,   2 },
    { "VelFlr",  "vfl",      T_U8,   &velFloor,      0,  64,   4 },
    { "GateMs",  "gms",      T_U8,   &drumGateMs,   10, 200,  10 },
    { "Drone N", "drnN",     T_U8,   &droneRoot,    24,  84,   1 },
    { "Rhthm N", "rhyN",     T_U8,   &rhythmRoot,   24,  84,   1 },
};

// ─── Globals ─────────────────────────────────────────────────────────────
static USBMIDI           usbMidi;
static Adafruit_SSD1306  oled(OLED_W, OLED_H, &Wire);
static Adafruit_NeoPixel strip(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static SparkFun_VL53L5CX tof;
static VL53L5CX_ResultsData tofData;
static Preferences       prefs;
static WebServer         webServer(80);

// Mode + page state
static Mode          currentMode  = MODE_DRONE;
static Page          currentPage  = PG_LIVE;
static bool          oledDirty    = true;
static unsigned long tOled        = 0;
static bool          bypassed     = false;
static bool          configMode   = false;
static bool          webTaskAlive = false;
static TaskHandle_t  webTask      = nullptr;

// Tunable settings UI
static uint8_t       settingIdx   = 0;     // PG_SETTINGS row selection
static bool          editMode     = false; // true → encoder turns adjust value
static uint8_t       presetSlot   = 0;     // PG_PRESETS slot selection
static uint8_t       presetsExistMask = 0; // bitmask of populated slots

// Settings-save debounce
static bool          settingsDirty = false;
static unsigned long dirtyTime     = 0;

// ToF features (filled by pollToF, consumed by routeMode)
static bool     sensorOK     = false;
static bool     tofFreshFrame = false;
static float    distMm        = 0;
static float    liveVel       = 0;     // mm/s, EMA-smoothed
static int8_t   centroidX     = 0;     // -32..+31 (scaled column)
static int8_t   centroidY     = 0;     // -32..+31 (scaled row)
static uint8_t  coverage      = 0;     // count of close zones (0..64)
static bool     tofValid      = false;
static uint16_t closestMm     = 0;     // raw closest reading
static int8_t   centroidXcc   = 64;    // 0..127 mapped from centroidX, for MIDI CC
static int8_t   distCC        = 0;     // 0..127 mapped from distMm
static int8_t   velCC         = 0;     // 0..127 mapped from liveVel

// Last MIDI note for the LIVE OLED page
static uint8_t  lastNoteN     = 0;
static uint8_t  lastNoteV     = 0;

// Boot-time MAC
static uint8_t selfMac[6] = {0};

// ESP-NOW RX state
#define RX_BUF_LEN 64
static volatile uint8_t        rxHead = 0;
static volatile uint8_t        rxTail = 0;
static EspNowMidiPacket        rxBuf[RX_BUF_LEN];
static volatile uint32_t pktTotal      = 0;
static volatile uint32_t pktDropped    = 0;
static volatile uint32_t pktOverflow   = 0;
static volatile uint8_t  lastSeq       = 0;
static volatile bool     seqInit       = false;
static volatile uint32_t lastRxMs      = 0;
static volatile uint8_t  lastPeerMac[6] = {0};

// Rolling pps over 1 s for the INFO page
static uint32_t rateWindowStart = 0;
static uint32_t rateWindowPkts  = 0;
static uint32_t lastRatePps     = 0;

// Toast overlay
static const char   *toastText  = nullptr;
static unsigned long toastUntil = 0;
static void showToast(const char *msg) {
    toastText  = msg;
    toastUntil = millis() + 1000;
    oledDirty  = true;
}

// ─── Encoder ISR (lifted from v5/v6_rx) ─────────────────────────────────
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

// ─── ESP-NOW RX (lifted verbatim from v6_rx) ────────────────────────────
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    const uint8_t *mac = info->src_addr;
#else
static void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
    if (len != (int)sizeof(EspNowMidiPacket)) return;
    const EspNowMidiPacket *p = (const EspNowMidiPacket *)data;
    if (p->magic != ESPNOW_MAGIC) return;
    pktTotal++;
    if (seqInit) {
        uint8_t gap = (uint8_t)(p->seq - lastSeq);
        if (gap > 1) pktDropped += (uint32_t)(gap - 1);
    } else { seqInit = true; }
    lastSeq  = p->seq;
    lastRxMs = millis();
    memcpy((void *)lastPeerMac, mac, 6);

    uint8_t next = (uint8_t)((rxHead + 1) % RX_BUF_LEN);
    if (next == rxTail) { pktOverflow++; return; }
    rxBuf[rxHead] = *p;
    rxHead = next;
}

static bool initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(onEspNowRecv);
    esp_read_mac(selfMac, ESP_MAC_WIFI_STA);
    return true;
}

// ─── M2: VL53L5CX bring-up ──────────────────────────────────────────────
// 8×8 @ 15 Hz, sharpener 20% (close-range hand zones suffer at default 5%),
// CLOSEST target order (hand wins over a back wall). The firmware-blob
// upload at begin() takes ~1 s — covered by the 10 s WDT.
static bool initToF() {
    Wire.beginTransmission(VL53L5CX_ADDR);
    if (Wire.endTransmission() != 0) {
#if DEBUG
        Serial.println("[tof] no ACK at 0x29");
#endif
        return false;
    }
    if (!tof.begin(VL53L5CX_ADDR, Wire)) {
#if DEBUG
        Serial.println("[tof] begin() failed");
#endif
        return false;
    }
    tof.setResolution(64);
    tof.setRangingFrequency(15);
    tof.setRangingMode(SF_VL53L5CX_RANGING_MODE::CONTINUOUS);
    tof.setSharpenerPercent(20);
    tof.setTargetOrder(SF_VL53L5CX_TARGET_ORDER::CLOSEST);
    tof.startRanging();
    sensorOK = true;
    return true;
}

// ─── M3: Feature extraction from the 8×8 grid ───────────────────────────
//
// distMm     — EMA-smoothed closest valid zone (mm).
// liveVel    — EMA-smoothed approach velocity (mm/s, positive = approaching).
// centroidX  — column-weighted mean of close zones, scaled to ±32.
// centroidY  — row-weighted mean of close zones, scaled to ±32.
// coverage   — count of close zones (0..64). Hand spread proxy.
// distCC     — MIDI CC mapping of distMm via [distMinMm, distMaxMm] window.
// velCC      — MIDI CC mapping of liveVel.
// centroidXcc — MIDI CC mapping of centroidX shifted to 0..127.
//
// Status: 5 = nominal target valid, 9 = target valid with wraparound;
// either is acceptable for our use (the wraparound flag means range > sensor
// max but the zone still reports a usable closest reading).
#define TOF_STATUS_OK1   5
#define TOF_STATUS_OK2   9
#define DIST_CLOSE_MM    700
#define EMA_ALPHA_DIST   0.30f
#define EMA_ALPHA_VEL    0.20f
#define VEL_DEAD_MMS     5.0f

static uint32_t tLastTof   = 0;
static float    prevDist   = 0;

static inline bool zoneOk(uint8_t s) {
    return s == TOF_STATUS_OK1 || s == TOF_STATUS_OK2;
}

static void pollToF() {
    tofFreshFrame = false;
    if (!sensorOK) return;
    static uint32_t tNextCheck = 0;
    uint32_t now = millis();
    if (now < tNextCheck) return;
    tNextCheck = now + TOF_POLL_MS;

    if (!tof.isDataReady()) return;
    if (!tof.getRangingData(&tofData)) return;

    // 1. Closest valid zone
    uint16_t minD = 0xFFFF;
    int closestIdx = -1;
    for (int i = 0; i < 64; i++) {
        if (!zoneOk(tofData.target_status[i])) continue;
        uint16_t d = tofData.distance_mm[i];
        if (d >= 20 && d < minD) { minD = d; closestIdx = i; }
    }
    if (closestIdx < 0) {
        tofValid = false;
        return;
    }
    closestMm = minD;

    // 2. EMA-smoothed distance
    float newD = (float)minD;
    if (distMm == 0) distMm = newD;             // first frame seed
    distMm += EMA_ALPHA_DIST * (newD - distMm);

    // 3. Approach velocity
    if (tLastTof > 0) {
        float dt = (now - tLastTof) * 0.001f;
        if (dt > 0.0001f) {
            float v = (prevDist - distMm) / dt;
            if (fabsf(v) < VEL_DEAD_MMS) v = 0.0f;
            liveVel += EMA_ALPHA_VEL * (v - liveVel);
        }
    }
    prevDist = distMm;
    tLastTof = now;

    // 4. Centroid + coverage over the close zones
    int sumX = 0, sumY = 0;
    coverage = 0;
    for (int i = 0; i < 64; i++) {
        if (!zoneOk(tofData.target_status[i])) continue;
        uint16_t d = tofData.distance_mm[i];
        if (d < 20 || d > DIST_CLOSE_MM) continue;
        sumX += (i % 8) - 4;
        sumY += (i / 8) - 4;
        coverage++;
    }
    if (coverage > 0) {
        centroidX = (int8_t)((sumX * 8) / (int)coverage);
        centroidY = (int8_t)((sumY * 8) / (int)coverage);
        tofValid = true;
    } else {
        centroidX = (int8_t)(((closestIdx % 8) - 4) * 8);
        centroidY = (int8_t)(((closestIdx / 8) - 4) * 8);
        tofValid = (distMm < 1500);
    }

    // 5. CC-mapped versions for MIDI CC streaming
    int distSpan = distMaxMm - distMinMm;
    float t = (distSpan > 0) ? ((distMm - distMinMm) / (float)distSpan) : 0.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (distInvert) t = 1.0f - t;
    distCC = (int8_t)(t * 127.0f);

    // velCC: clip |liveVel| to [0..500 mm/s] → 0..127.
    float av = fabsf(liveVel);
    if (av > 500.0f) av = 500.0f;
    velCC = (int8_t)((av / 500.0f) * 127.0f);

    // centroidX in -32..+31 → 0..127
    centroidXcc = (int8_t)(((int)centroidX + 32) * 127 / 63);
    if (centroidXcc < 0) centroidXcc = 0;
    if (centroidXcc > 127) centroidXcc = 127;

    tofFreshFrame = true;
}

// ─── M4: MCP4561 digipot — expression OUTPUT ────────────────────────────
//
// One I²C digital pot at DIGIPOT_ADDR emulates an expression pedal into
// another pedal's EXP jack (Dimension-Shifter style). The wiper tracks a
// chosen ToF feature continuously. 8-bit (0..255 = full sweep).
//
// MCP4561 write: command byte 0x00 = (wiper0 register, write), then the
// 8-bit value. No library — raw Wire is enough.

static bool digipotOK = false;

static bool initDigipot() {
    Wire.beginTransmission(DIGIPOT_ADDR);
    if (Wire.endTransmission() != 0) {
#if DEBUG
        Serial.println("[digipot] no ACK");
#endif
        return false;
    }
    digipotOK = true;
    return true;
}

static void digipotWrite(uint8_t wiper) {
    if (!digipotOK) return;
    Wire.beginTransmission(DIGIPOT_ADDR);
    Wire.write(0x00);     // wiper0 register, write command
    Wire.write(wiper);    // 8-bit value
    Wire.endTransmission();
}

// Map the selected ToF feature (0..127 CC-domain) to the 8-bit wiper and
// write it. Called once per fresh ToF frame so the I²C bus isn't spammed.
static uint8_t exprWiperLast = 0xFF;
static void updateExpressionOut() {
    uint8_t src;
    switch (digiSrc) {
        case 1:  src = (uint8_t)velCC; break;
        case 2:  src = (uint8_t)centroidXcc; break;
        case 3:  src = (uint8_t)constrain((int)coverage * 2, 0, 127); break;
        default: src = (uint8_t)distCC; break;     // 0 = distance
    }
    if (digiInvert) src = 127 - src;
    uint8_t wiper = (uint8_t)(((int)src * 255) / 127);
    if (wiper != exprWiperLast) { digipotWrite(wiper); exprWiperLast = wiper; }
}

// ─── M9: NeoPixel gesture feedback ──────────────────────────────────────
// Distance → lit-pixel bar; velocity → colour (green slow → red fast);
// bypass → dim red. Rate-limited to ~30 fps so it never stalls the loop.
static void initNeoPixel() {
    strip.begin();
    strip.setBrightness(60);
    strip.clear();
    strip.show();
}

static void updateNeoPixel() {
    static uint32_t tPix = 0;
    uint32_t now = millis();
    if ((now - tPix) < 33) return;
    tPix = now;
    strip.clear();
    if (bypassed) {
        for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(40, 0, 0));
        strip.show();
        return;
    }
    if (tofValid) {
        int lit = ((int)distCC * NUM_PIXELS) / 127;
        uint8_t r = (velCC * 2 > 255) ? 255 : (uint8_t)(velCC * 2);
        uint8_t g = 255 - r;
        for (int i = 0; i < lit && i < NUM_PIXELS; i++)
            strip.setPixelColor(i, strip.Color(r, g, 0));
    }
    strip.show();
}

// ─── M6: MIDI helpers — USB + DIN, bypass-aware ─────────────────────────
static void midiSendDin(uint8_t st, uint8_t d1, uint8_t d2) {
    Serial1.write(st);
    Serial1.write(d1);
    Serial1.write(d2);
}

static void midiNoteOn(uint8_t n, uint8_t v, uint8_t ch) {
    if (bypassed) return;
    usbMidi.noteOn(n, v, ch);
    midiSendDin(0x90 | ((ch - 1) & 0x0F), n & 0x7F, v & 0x7F);
    lastNoteN = n; lastNoteV = v;
}
static void midiNoteOff(uint8_t n, uint8_t v, uint8_t ch) {
    usbMidi.noteOff(n, v, ch);                                  // always — bypass-asymmetry
    midiSendDin(0x80 | ((ch - 1) & 0x0F), n & 0x7F, v & 0x7F);
}
static void midiCC(uint8_t cc, uint8_t val, uint8_t ch) {
    if (bypassed) return;
    usbMidi.controlChange(cc, val, ch);
    midiSendDin(0xB0 | ((ch - 1) & 0x0F), cc & 0x7F, val & 0x7F);
}
static void midiPB(uint16_t bend14, uint8_t ch) {
    if (bypassed) return;
    usbMidi.pitchBend(bend14, ch);
    midiSendDin(0xE0 | ((ch - 1) & 0x0F), bend14 & 0x7F, (bend14 >> 7) & 0x7F);
}

// ─── ESP-NOW MIDI forward (from v5 guitar TX) ───────────────────────────
static void drainRx() {
    for (uint8_t i = 0; i < RX_BUF_LEN; i++) {
        if (rxTail == rxHead) break;
        EspNowMidiPacket p = rxBuf[rxTail];
        rxTail = (uint8_t)((rxTail + 1) % RX_BUF_LEN);
        uint8_t cmd = p.status & 0xF0;
        uint8_t ch  = (uint8_t)((p.status & 0x0F) + 1);

        if (bypassed && cmd != 0x80) continue;   // only releases survive bypass

        switch (cmd) {
            case 0x80: midiNoteOff(p.data1, p.data2, ch); break;
            case 0x90: midiNoteOn (p.data1, p.data2, ch); break;
            case 0xB0: midiCC     (p.data1, p.data2, ch); break;
            case 0xE0: midiPB((uint16_t)p.data1 | ((uint16_t)p.data2 << 7), ch); break;
            default: break;
        }
    }
}

// ─── M5: Mode router (DRONE / RHYTHM / DRUMS) ───────────────────────────

#define DRONE_RANGE_NOTES  24             // ± centroidY span around root

static const uint8_t PENTATONIC[10] = { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22 };
static const uint8_t DRUM_NOTES[4]  = { 36, 38, 42, 45 };   // kick/snare/hat/lo-tom

static int8_t   heldNote   = -1;
static uint8_t  heldVel    = 0;
static uint8_t  heldCh     = 1;
static uint32_t gateUntil  = 0;
static bool     droneActive = false;
static int16_t  triggerPrev = 0;          // velCC at previous tick

static void allNotesOffAllChannels() {
    for (uint8_t ch = 1; ch <= 16; ch++) {
        usbMidi.controlChange(123, 0, ch);
        usbMidi.controlChange(120, 0, ch);
        midiSendDin(0xB0 | ((ch - 1) & 0x0F), 123, 0);
    }
    heldNote   = -1;
    droneActive = false;
    gateUntil   = 0;
}

static void releaseHeld() {
    if (heldNote < 0) return;
    midiNoteOff((uint8_t)heldNote, 0, heldCh);
    heldNote = -1;
}

static inline uint8_t binByCentroidX(uint8_t n) {
    int v = ((int)centroidXcc * (int)n) / 128;
    if (v < 0) return 0;
    if (v >= (int)n) return n - 1;
    return (uint8_t)v;
}

static void enterMode(Mode m) {
    if (m == currentMode) return;
    releaseHeld();
    if (droneActive) {
        midiNoteOff(droneRoot, 0, midiCh);
        droneActive = false;
    }
    triggerPrev = 0;
    gateUntil   = 0;
    currentMode = m;
    settingsDirty = true; dirtyTime = millis();
    showToast(m == MODE_DRONE  ? "DRONE"
            : m == MODE_RHYTHM ? "RHYTHM"
                               : "DRUMS");
}

static void droneTick() {
    if (heldNote < 0 && !droneActive) {
        midiNoteOn(droneRoot, 100, midiCh);
        droneActive = true;
        heldNote = (int8_t)droneRoot;
        heldCh   = midiCh;
    }
    // centroidX → 14-bit pitch bend
    int32_t bend = 8192 + ((int32_t)centroidX * 8192) / 32;
    if (bend < 0) bend = 0;
    if (bend > 16383) bend = 16383;
    midiPB((uint16_t)bend, midiCh);
}

static void rhythmTick() {
    bool rising  = (triggerPrev < trigThresh && velCC >= trigThresh);
    bool falling = (velCC < releaseThresh);
    if (heldNote < 0 && rising) {
        uint8_t idx = binByCentroidX(10);
        uint8_t n = rhythmRoot + PENTATONIC[idx];
        uint8_t v = (uint8_t)constrain((int)velCC + velFloor, 1, 127);
        midiNoteOn(n, v, midiCh);
        heldNote = (int8_t)n; heldVel = v; heldCh = midiCh;
    } else if (heldNote >= 0 && falling) {
        releaseHeld();
    }
}

static void drumsTick(uint32_t now) {
    bool rising = (triggerPrev < trigThresh && velCC >= trigThresh);
    if (heldNote < 0 && rising) {
        uint8_t bin = binByCentroidX(4);
        uint8_t n   = DRUM_NOTES[bin];
        uint8_t v   = (uint8_t)constrain((int)velCC + velFloor, 1, 127);
        midiNoteOn(n, v, 10);                       // MIDI ch 10 = GM drums
        heldNote = (int8_t)n; heldVel = v; heldCh = 10;
        gateUntil = now + drumGateMs;
    }
    if (heldNote >= 0 && now >= gateUntil) releaseHeld();
}

// Per-frame CC streaming for the mode-independent observables.
static int16_t prevDistCC = -1, prevVelCC = -1, prevCx = -1, prevCov = -1;

static void streamFeatureCCs() {
    if (bypassed) return;
    if (distCC != prevDistCC)     { midiCC(ccDist,      (uint8_t)distCC,     midiCh); prevDistCC = distCC; }
    if (velCC  != prevVelCC)      { midiCC(ccVel,       (uint8_t)velCC,      midiCh); prevVelCC  = velCC;  }
    if (centroidXcc != prevCx)    { midiCC(ccCentroidX, (uint8_t)centroidXcc,midiCh); prevCx     = centroidXcc; }
    if ((int16_t)coverage != prevCov) {
        uint8_t cv = (uint8_t)constrain((int)coverage * 2, 0, 127);
        midiCC(ccCoverage, cv, midiCh); prevCov = coverage;
    }
}

static void routeMode() {
    uint32_t now = millis();
    if (heldNote >= 0 && currentMode == MODE_DRUMS && now >= gateUntil) {
        releaseHeld();
    }
    if (bypassed) {
        if (heldNote >= 0) releaseHeld();
        if (droneActive)   { midiNoteOff(droneRoot, 0, midiCh); droneActive = false; }
        triggerPrev = velCC;
        return;
    }
    if (!tofValid) {
        triggerPrev = velCC;
        return;
    }
    if (tofFreshFrame) {
        streamFeatureCCs();
        updateExpressionOut();   // digipot tracks the selected ToF feature
    }

    switch (currentMode) {
        case MODE_DRONE:  droneTick();    break;
        case MODE_RHYTHM: rhythmTick();   break;
        case MODE_DRUMS:  drumsTick(now); break;
        case MODE_COUNT:  break;
    }
    triggerPrev = velCC;
}

// ─── Tactile buttons (BTN_MUTE, BTN_MODE) ───────────────────────────────
static void handleButtons() {
    static bool     muteRaw = true, muteStable = true;
    static uint32_t muteEdge = 0;
    static bool     modeRaw = true, modeStable = true;
    static uint32_t modeEdge = 0;
    uint32_t now = millis();

    // BTN_MUTE
    bool m = (digitalRead(BTN_MUTE) == HIGH);
    if (m != muteRaw) { muteRaw = m; muteEdge = now; }
    if ((now - muteEdge) >= BTN_DEBOUNCE_MS && muteRaw != muteStable) {
        muteStable = muteRaw;
        if (!muteStable) {           // press edge (LOW)
            bypassed = !bypassed;
            if (bypassed) allNotesOffAllChannels();
            showToast(bypassed ? "BYPASS" : "ACTIVE");
            oledDirty = true;
        }
    }

    // BTN_MODE
    bool md = (digitalRead(BTN_MODE) == HIGH);
    if (md != modeRaw) { modeRaw = md; modeEdge = now; }
    if ((now - modeEdge) >= BTN_DEBOUNCE_MS && modeRaw != modeStable) {
        modeStable = modeRaw;
        if (!modeStable) {           // press edge
            Mode next = (Mode)(((uint8_t)currentMode + 1) % (uint8_t)MODE_COUNT);
            enterMode(next);
        }
    }
}

// ─── Forward declarations for the settings UI ───────────────────────────
static void enterConfigMode();
static void exitConfigMode();
static bool savePresetTo(uint8_t slot);
static bool loadPresetFrom(uint8_t slot);
static void refreshPresetsExist();

// ─── M8: NVS persistence ────────────────────────────────────────────────
static const char *const NVS_NS = "heat";

static int32_t settingGetInt(const Setting &s) {
    switch (s.type) {
        case T_U8:   return *(uint8_t*)s.ptr;
        case T_I16:  return *(int16_t*)s.ptr;
        case T_BOOL: return *(bool*)s.ptr ? 1 : 0;
    }
    return 0;
}

static bool settingSetInt(const Setting &s, int32_t v) {
    bool changed = false;
    switch (s.type) {
        case T_U8: {
            uint8_t n = (uint8_t)constrain((int)v, (int)s.lo, (int)s.hi);
            if (n != *(uint8_t*)s.ptr) { *(uint8_t*)s.ptr = n; changed = true; }
            break;
        }
        case T_I16: {
            int16_t n = (int16_t)constrain((int)v, (int)s.lo, (int)s.hi);
            if (n != *(int16_t*)s.ptr) { *(int16_t*)s.ptr = n; changed = true; }
            break;
        }
        case T_BOOL: {
            bool n = (v != 0);
            if (n != *(bool*)s.ptr) { *(bool*)s.ptr = n; changed = true; }
            break;
        }
    }
    if (changed) { settingsDirty = true; dirtyTime = millis(); }
    return changed;
}

static void loadSettings() {
    prefs.begin(NVS_NS, true);
    for (uint8_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        if (!prefs.isKey(s.nvsKey)) continue;
        switch (s.type) {
            case T_U8:   *(uint8_t*)s.ptr = prefs.getUChar (s.nvsKey, *(uint8_t*)s.ptr); break;
            case T_I16:  *(int16_t*)s.ptr = prefs.getShort (s.nvsKey, *(int16_t*)s.ptr); break;
            case T_BOOL: *(bool*)s.ptr    = prefs.getBool  (s.nvsKey, *(bool*)s.ptr);    break;
        }
    }
    // Persisted mode + invert flag
    if (prefs.isKey("mode"))     currentMode = (Mode)prefs.getUChar("mode", (uint8_t)MODE_DRONE);
    if (prefs.isKey("dInv"))     distInvert  = prefs.getBool("dInv", distInvert);
    prefs.end();
}

static void saveSettings() {
    prefs.begin(NVS_NS, false);
    for (uint8_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        switch (s.type) {
            case T_U8:   prefs.putUChar(s.nvsKey, *(uint8_t*)s.ptr); break;
            case T_I16:  prefs.putShort(s.nvsKey, *(int16_t*)s.ptr); break;
            case T_BOOL: prefs.putBool (s.nvsKey, *(bool*)s.ptr);    break;
        }
    }
    prefs.putUChar("mode", (uint8_t)currentMode);
    prefs.putBool ("dInv", distInvert);
    prefs.end();
    settingsDirty = false;
}

// ─── M8: 8-slot preset system ───────────────────────────────────────────
static void presetKey(char *out, size_t n, uint8_t slot, const char *suffix) {
    snprintf(out, n, "p%u_%s", (unsigned)slot, suffix);
}

static void refreshPresetsExist() {
    presetsExistMask = 0;
    prefs.begin(NVS_NS, true);
    for (uint8_t s = 0; s < 8; s++) {
        char k[12]; presetKey(k, sizeof(k), s, "exi");
        if (prefs.getBool(k, false)) presetsExistMask |= (1 << s);
    }
    prefs.end();
}

static bool savePresetTo(uint8_t slot) {
    if (slot >= 8) return false;
    prefs.begin(NVS_NS, false);
    char k[12];
    for (uint8_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        presetKey(k, sizeof(k), slot, s.nvsKey);
        switch (s.type) {
            case T_U8:   prefs.putUChar(k, *(uint8_t*)s.ptr); break;
            case T_I16:  prefs.putShort(k, *(int16_t*)s.ptr); break;
            case T_BOOL: prefs.putBool (k, *(bool*)s.ptr);    break;
        }
    }
    presetKey(k, sizeof(k), slot, "mode"); prefs.putUChar(k, (uint8_t)currentMode);
    presetKey(k, sizeof(k), slot, "dInv"); prefs.putBool(k, distInvert);
    presetKey(k, sizeof(k), slot, "exi");  prefs.putBool(k, true);
    prefs.end();
    presetsExistMask |= (1 << slot);
    showToast("SAVED");
    return true;
}

static bool loadPresetFrom(uint8_t slot) {
    if (slot >= 8) return false;
    if (!(presetsExistMask & (1 << slot))) { showToast("EMPTY"); return false; }
    prefs.begin(NVS_NS, true);
    char k[12];
    for (uint8_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        presetKey(k, sizeof(k), slot, s.nvsKey);
        if (!prefs.isKey(k)) continue;
        switch (s.type) {
            case T_U8:   *(uint8_t*)s.ptr = prefs.getUChar(k, *(uint8_t*)s.ptr); break;
            case T_I16:  *(int16_t*)s.ptr = prefs.getShort(k, *(int16_t*)s.ptr); break;
            case T_BOOL: *(bool*)s.ptr    = prefs.getBool (k, *(bool*)s.ptr);    break;
        }
    }
    presetKey(k, sizeof(k), slot, "mode");
    if (prefs.isKey(k)) enterMode((Mode)prefs.getUChar(k, (uint8_t)MODE_DRONE));
    presetKey(k, sizeof(k), slot, "dInv");
    if (prefs.isKey(k)) distInvert = prefs.getBool(k, distInvert);
    prefs.end();
    settingsDirty = true; dirtyTime = millis();
    showToast("LOADED");
    return true;
}

// ─── M8: WiFi config web UI ─────────────────────────────────────────────
static const char *PAGE_HTML = R"HTML(<!doctype html>
<html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Heat Config</title>
<style>
:root{--bg:#111;--fg:#eee;--mut:#888;--acc:#f80;--card:#222}
*{box-sizing:border-box;font-family:system-ui,sans-serif}
body{background:var(--bg);color:var(--fg);margin:0;padding:16px;max-width:520px;margin:0 auto}
h1{margin:0 0 4px;font-size:20px;letter-spacing:.1em}
.sub{color:var(--mut);font-size:12px;margin-bottom:12px}
.card{background:var(--card);border-radius:8px;padding:12px;margin-bottom:12px}
.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #2a2a2a}
.row:last-child{border-bottom:none}
.row label{flex:1}
.row input,.row select{background:#000;color:var(--fg);border:1px solid #444;border-radius:4px;padding:4px 6px;font-size:14px;width:80px;text-align:right}
.bar{display:flex;align-items:center;margin:4px 0}
.bar span{min-width:50px;font-size:11px;color:var(--mut)}
.bar div{flex:1;background:#000;height:14px;border-radius:7px;overflow:hidden;margin-left:8px}
.bar div b{display:block;height:100%;background:var(--acc);transition:width .1s}
.modes{display:flex;gap:8px;margin-bottom:8px}
.modes button{flex:1;background:#000;color:var(--fg);border:1px solid #444;padding:8px;border-radius:6px;cursor:pointer;font-size:14px}
.modes button.on{background:var(--acc);color:#000;border-color:var(--acc)}
.presets{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.presets div{background:#000;border:1px solid #444;padding:6px;border-radius:4px;text-align:center;font-size:11px}
.presets div.have{border-color:var(--acc)}
.presets button{display:block;width:100%;margin-top:4px;background:#1a1a1a;color:var(--fg);border:1px solid #333;border-radius:3px;padding:4px;cursor:pointer;font-size:11px}
.presets button:hover{background:var(--acc);color:#000}
</style></head><body>
<h1>HEAT CONFIG</h1>
<div class=sub id=mac>--</div>

<div class=card>
<div class=bar><span>dist</span><div><b id=bdist></b></div></div>
<div class=bar><span>vel</span><div><b id=bvel></b></div></div>
<div class=bar><span>cX</span><div><b id=bcx></b></div></div>
<div class=bar><span>cov</span><div><b id=bcov></b></div></div>
<div class=bar><span>bypass</span><div><b id=bbyp></b></div></div>
</div>

<div class=card>
<div class=modes>
<button id=md0 onclick="setMode(0)">DRONE</button>
<button id=md1 onclick="setMode(1)">RHYTHM</button>
<button id=md2 onclick="setMode(2)">DRUMS</button>
</div>
<div class=presets id=pre></div>
</div>

<div class=card id=sets></div>

<script>
function fmt(v,mx){return Math.round(Math.min(100,Math.max(0,v/mx*100)))+'%'}
function get(u,cb){fetch(u).then(r=>r.json()).then(cb)}
function live(){
  get('/api/live',d=>{
    document.getElementById('bdist').style.width=fmt(d.dist,127);
    document.getElementById('bvel').style.width=fmt(d.vel,127);
    document.getElementById('bcx').style.width=fmt(d.cx,127);
    document.getElementById('bcov').style.width=fmt(d.cov,64);
    document.getElementById('bbyp').style.width=d.byp?'100%':'0%';
    for(let i=0;i<3;i++){document.getElementById('md'+i).className=(d.mode==i?'on':'')}
  })
}
function setMode(m){fetch('/api/set?mode='+m).then(live)}
function loadP(s){fetch('/api/preset?op=load&slot='+s).then(state)}
function saveP(s){fetch('/api/preset?op=save&slot='+s).then(state)}
function state(){
  get('/api/state',d=>{
    document.getElementById('mac').textContent=d.mac;
    const pre=document.getElementById('pre');pre.innerHTML='';
    for(let i=0;i<8;i++){
      const have=(d.presets>>i)&1;
      pre.innerHTML+='<div class="'+(have?'have':'')+'">P'+(i+1)+'<button onclick="loadP('+i+')">L</button><button onclick="saveP('+i+')">S</button></div>';
    }
    const sets=document.getElementById('sets');sets.innerHTML='';
    d.settings.forEach(s=>{
      sets.innerHTML+='<div class=row><label>'+s.name+'</label><input type=number value='+s.v+' min='+s.lo+' max='+s.hi+' step='+s.step+' onchange="fetch(\'/api/set?'+s.key+'=\'+this.value).then(live)"></div>';
    })
  })
}
state();setInterval(live,200);
</script></body></html>)HTML";

static String macStr() {
    char b[20];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
             selfMac[0], selfMac[1], selfMac[2],
             selfMac[3], selfMac[4], selfMac[5]);
    return String(b);
}

static void handleRoot()   { webServer.send(200, "text/html", PAGE_HTML); }

static void handleLive() {
    String j = "{";
    j += "\"dist\":"   + String((int)distCC) + ",";
    j += "\"vel\":"    + String((int)velCC) + ",";
    j += "\"cx\":"     + String((int)centroidXcc) + ",";
    j += "\"cov\":"    + String((int)coverage) + ",";
    j += "\"byp\":"    + String(bypassed ? 1 : 0) + ",";
    j += "\"mode\":"   + String((int)currentMode);
    j += "}";
    webServer.send(200, "application/json", j);
}

static void handleState() {
    refreshPresetsExist();
    String j = "{";
    j += "\"mac\":\""    + macStr() + "\",";
    j += "\"presets\":"  + String((int)presetsExistMask) + ",";
    j += "\"settings\":[";
    for (uint8_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        if (i) j += ",";
        j += "{\"name\":\"" + String(s.name) + "\",";
        j += "\"key\":\"" + String(s.nvsKey) + "\",";
        j += "\"v\":" + String((long)settingGetInt(s)) + ",";
        j += "\"lo\":" + String((long)s.lo) + ",";
        j += "\"hi\":" + String((long)s.hi) + ",";
        j += "\"step\":" + String((long)s.step) + "}";
    }
    j += "]}";
    webServer.send(200, "application/json", j);
}

static void handleSet() {
    bool any = false;
    for (uint8_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        if (webServer.hasArg(s.nvsKey)) {
            settingSetInt(s, webServer.arg(s.nvsKey).toInt());
            any = true;
        }
    }
    if (webServer.hasArg("mode")) {
        enterMode((Mode)constrain((int)webServer.arg("mode").toInt(), 0, MODE_COUNT - 1));
        any = true;
    }
    if (any) { settingsDirty = true; dirtyTime = millis(); oledDirty = true; }
    webServer.send(200, "application/json", "{\"ok\":1}");
}

static void handlePreset() {
    String op   = webServer.arg("op");
    int    slot = webServer.arg("slot").toInt();
    bool ok = false;
    if (op == "save") ok = savePresetTo((uint8_t)slot);
    if (op == "load") ok = loadPresetFrom((uint8_t)slot);
    webServer.send(200, "application/json", String("{\"ok\":") + (ok ? "1" : "0") + "}");
}

static void webTaskFn(void *) {
    webServer.on("/",            handleRoot);
    webServer.on("/api/live",    handleLive);
    webServer.on("/api/state",   handleState);
    webServer.on("/api/set",     handleSet);
    webServer.on("/api/preset",  handlePreset);
    webServer.begin();
    webTaskAlive = true;
    while (configMode) {
        webServer.handleClient();
        delay(2);
    }
    webServer.stop();
    webTaskAlive = false;
    vTaskDelete(nullptr);
}

static void enterConfigMode() {
    if (configMode) return;
    configMode = true;
    // Bring up the AP. ESP-NOW continues to receive on channel 1; the AP
    // is also pinned to channel 1 so both work simultaneously.
    WiFi.softAP(AP_SSID, AP_PASS, 1);
    delay(100);
    xTaskCreatePinnedToCore(webTaskFn, "webcfg", 8192, nullptr, 1, &webTask, 1);
    showToast("CONFIG");
}

static void exitConfigMode() {
    if (!configMode) return;
    configMode = false;
    // Let the web task observe the flag, finish its current client, exit.
    uint32_t deadline = millis() + 500;
    while (webTaskAlive && millis() < deadline) delay(5);
    WiFi.softAPdisconnect(true);
    showToast("EXIT CFG");
    if (settingsDirty) { saveSettings(); }
}

// ─── M5/M7: encoder gestures (rotate / tap / hold / triple-tap) ──────────
static void handleEncoder() {
    noInterrupts();
    int8_t delta = encDelta;
    encDelta = 0;
    interrupts();

    static bool      prev      = false;
    static uint32_t  pressStart = 0;
    static bool      longFired  = false;
    static bool      saveFired  = false;
    static uint8_t   tapCount   = 0;
    static uint32_t  tapsStart  = 0;

    bool     pushed = (digitalRead(ENC_PUSH) == LOW);
    uint32_t now    = millis();

    if (pushed && !prev) { pressStart = now; longFired = false; saveFired = false; }

    if (pushed && !longFired && (now - pressStart) >= ENC_HOLD_MIN_MS) {
        longFired = true;
        // PG_PRESETS treats long-hold as "save"; everything else flips page.
        if (currentPage != PG_PRESETS) {
            currentPage = (Page)((uint8_t)(currentPage + 1) % NUM_PAGES);
            oledDirty = true;
        }
    }
    if (pushed && !saveFired && currentPage == PG_PRESETS &&
        (now - pressStart) >= ENC_SAVE_MIN_MS) {
        saveFired = true;
        savePresetTo(presetSlot);
        oledDirty = true;
    }

    if (!pushed && prev) {
        uint32_t held = now - pressStart;
        if (held < ENC_TAP_MAX_MS && !longFired) {
            // Multi-tap accumulator (triple-tap → config mode toggle)
            if ((now - tapsStart) > CFG_TAP_WINDOW) { tapsStart = now; tapCount = 0; }
            tapCount++;
            if (tapCount >= CFG_TAP_COUNT) {
                tapCount = 0;
                if (configMode) exitConfigMode(); else enterConfigMode();
                oledDirty = true;
            } else {
                // Single short tap: context action per page.
                switch (currentPage) {
                    case PG_LIVE:
                        bypassed = !bypassed;
                        if (bypassed) allNotesOffAllChannels();
                        showToast(bypassed ? "BYPASS" : "ACTIVE");
                        break;
                    case PG_MODE:
                        enterMode((Mode)(((uint8_t)currentMode + 1) % (uint8_t)MODE_COUNT));
                        break;
                    case PG_SETTINGS:
                        editMode = !editMode;
                        break;
                    case PG_PRESETS:
                        loadPresetFrom(presetSlot);
                        break;
                    case PG_INFO:
                        // reset counters
                        pktTotal = pktDropped = pktOverflow = 0;
                        seqInit = false;
                        showToast("RST CNT");
                        break;
                }
            }
            oledDirty = true;
        }
    }
    prev = pushed;

    if (delta != 0) {
        switch (currentPage) {
            case PG_SETTINGS:
                if (editMode) {
                    const Setting &s = SETTINGS[settingIdx];
                    settingSetInt(s, settingGetInt(s) + (int32_t)delta * s.step);
                } else {
                    int n = (int)settingIdx + delta;
                    while (n < 0) n += NUM_SETTINGS;
                    settingIdx = (uint8_t)(n % NUM_SETTINGS);
                }
                break;
            case PG_PRESETS: {
                int n = (int)presetSlot + delta;
                while (n < 0) n += 8;
                presetSlot = (uint8_t)(n % 8);
                break;
            }
            case PG_MODE:
                enterMode((Mode)(((int)currentMode + delta + MODE_COUNT) % MODE_COUNT));
                break;
            default:
                // Rotate flips page on LIVE / INFO
                if (delta > 0) currentPage = (Page)((uint8_t)(currentPage + 1) % NUM_PAGES);
                else           currentPage = (Page)((uint8_t)(currentPage + NUM_PAGES - 1) % NUM_PAGES);
                break;
        }
        oledDirty = true;
    }
}

// ─── M7: OLED drawing helpers ────────────────────────────────────────────
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

static int16_t drawBadge(int16_t x, int16_t y, const char *txt) {
    int16_t w = strlen(txt) * 6 + 4;
    oled.fillRoundRect(x, y, w, 9, 2, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(x + 2, y + 1);
    oled.print(txt);
    oled.setTextColor(SSD1306_WHITE);
    return w;
}

static void drawSegBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t val, int16_t mx) {
    if (mx <= 0) mx = 1;
    if (val < 0) val = 0;
    if (val > mx) val = mx;
    oled.drawRect(x, y, w, h, SSD1306_WHITE);
    int16_t fw = ((int32_t)(w - 2) * val) / mx;
    oled.fillRect(x + 1, y + 1, fw, h - 2, SSD1306_WHITE);
}

static const char *modeName(Mode m) {
    switch (m) {
        case MODE_DRONE:  return "DRONE";
        case MODE_RHYTHM: return "RHYTHM";
        case MODE_DRUMS:  return "DRUMS";
        default:          return "?";
    }
}

// ─── M7: page draw functions ─────────────────────────────────────────────
static void drawPageLive() {
    char title[16];
    snprintf(title, sizeof(title), "%-6s", modeName(currentMode));
    drawPageHeader(title);

    // Badges in the header strip — bypass takes precedence, then link state.
    bool linkUp = seqInit && (millis() - lastRxMs) < NO_SIGNAL_MS;
    int16_t bx = OLED_W;
    if (bypassed)      { bx -= drawBadge(OLED_W - 30, 0, "MUTE") + 1; }
    else if (linkUp)   { bx -= drawBadge(OLED_W - 22, 0, "RX")  + 1; }

    // Distance bar
    oled.setCursor(0, 14);
    oled.printf("d %4d", (int)distMm);
    drawSegBar(36, 14, 90, 8, distCC, 127);

    // Velocity bar
    oled.setCursor(0, 24);
    oled.printf("v %+4d", (int)liveVel);
    drawSegBar(36, 24, 90, 8, velCC, 127);

    drawHLine(36);

    // Centroid X/Y dots + coverage
    oled.setCursor(0, 40);
    oled.printf("cX %+3d  cY %+3d  cov %2u",
                (int)centroidX, (int)centroidY, (unsigned)coverage);

    // Last note + edit indicator
    oled.setCursor(0, 50);
    if (lastNoteN) oled.printf("N%3u v%3u", (unsigned)lastNoteN, (unsigned)lastNoteV);
    else           oled.print("--- ---");

    if (configMode) drawBadge(80, 50, "CFG");

    drawPageDots(0, NUM_PAGES);
}

static void drawPageMode() {
    drawPageHeader("MODE");
    oled.setCursor(0, 16);
    oled.print("Cycle:");
    for (uint8_t m = 0; m < MODE_COUNT; m++) {
        int16_t y = 28 + m * 10;
        if (m == currentMode) {
            oled.fillRect(0, y - 1, 60, 9, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        }
        oled.setCursor(4, y);
        oled.print(modeName((Mode)m));
        oled.setTextColor(SSD1306_WHITE);
    }
    oled.setCursor(64, 28);
    oled.print("rotate");
    oled.setCursor(64, 38);
    oled.print("or tap");
    drawPageDots(1, NUM_PAGES);
}

static void formatSettingValue(const Setting &s, char *out, size_t n) {
    int32_t v = settingGetInt(s);
    switch (s.type) {
        case T_BOOL: snprintf(out, n, v ? "ON" : "OFF"); break;
        default:     snprintf(out, n, "%ld", (long)v);   break;
    }
}

static void drawPageSettings() {
    drawPageHeader(editMode ? "SET *EDIT*" : "SETTINGS");

    // 3-row window centred on settingIdx
    int8_t top = (int8_t)settingIdx - 1;
    for (int8_t r = 0; r < 4; r++) {
        int8_t i = top + r;
        if (i < 0 || i >= NUM_SETTINGS) continue;
        const Setting &s = SETTINGS[(uint8_t)i];
        int16_t y = 14 + r * 10;
        bool sel = (i == (int8_t)settingIdx);
        if (sel) {
            oled.fillRect(0, y - 1, OLED_W, 9, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        }
        char val[12];
        formatSettingValue(s, val, sizeof(val));
        oled.setCursor(2,  y);  oled.print(s.name);
        oled.setCursor(78, y);  oled.print(val);
        oled.setTextColor(SSD1306_WHITE);
    }
    drawPageDots(2, NUM_PAGES);
}

static void drawPagePresets() {
    drawPageHeader("PRESETS");
    refreshPresetsExist();

    // 2x4 grid of slots
    for (uint8_t i = 0; i < 8; i++) {
        int16_t x = (i % 4) * 32;
        int16_t y = 14 + (i / 4) * 18;
        bool sel  = (i == presetSlot);
        bool have = (presetsExistMask & (1 << i));
        if (sel) oled.fillRect(x, y, 30, 14, SSD1306_WHITE);
        else     oled.drawRect(x, y, 30, 14, SSD1306_WHITE);
        oled.setTextColor(sel ? SSD1306_BLACK : SSD1306_WHITE);
        oled.setCursor(x + 6, y + 4);
        oled.printf("%u%c", (unsigned)(i + 1), have ? '*' : ' ');
        oled.setTextColor(SSD1306_WHITE);
    }
    oled.setCursor(0, 50);
    oled.print("tap=load hold=save");
    drawPageDots(3, NUM_PAGES);
}

static void drawPageInfo() {
    drawPageHeader("INFO");
    oled.setCursor(0, 14);
    oled.printf("Me %02X%02X%02X%02X%02X%02X",
                selfMac[0], selfMac[1], selfMac[2],
                selfMac[3], selfMac[4], selfMac[5]);

    oled.setCursor(0, 24);
    if (seqInit) {
        oled.printf("Tx %02X%02X%02X%02X%02X%02X",
                    lastPeerMac[0], lastPeerMac[1], lastPeerMac[2],
                    lastPeerMac[3], lastPeerMac[4], lastPeerMac[5]);
    } else {
        oled.print("Tx (none)");
    }

    oled.setCursor(0, 36);
    oled.printf("pkt %lu  drp %lu", (unsigned long)pktTotal, (unsigned long)pktDropped);
    oled.setCursor(0, 46);
    oled.printf("pps %lu  heap %uk",
                (unsigned long)lastRatePps, (unsigned)(ESP.getFreeHeap() / 1024));

    if (configMode) drawBadge(96, 14, "AP");
    drawPageDots(4, NUM_PAGES);
}

static void refreshOLED() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    switch (currentPage) {
        case PG_LIVE:     drawPageLive();     break;
        case PG_MODE:     drawPageMode();     break;
        case PG_SETTINGS: drawPageSettings(); break;
        case PG_PRESETS:  drawPagePresets();  break;
        case PG_INFO:     drawPageInfo();     break;
    }
    if (toastText && millis() < toastUntil) {
        int16_t tw = strlen(toastText) * 6 + 8;
        int16_t tx = (OLED_W - tw) / 2;
        oled.fillRoundRect(tx, 24, tw, 16, 3, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(tx + 4, 28);
        oled.print(toastText);
        oled.setTextColor(SSD1306_WHITE);
    }
    oled.display();
    oledDirty = false;
}

// ─── Intro ───────────────────────────────────────────────────────────────
static void playIntro() {
    oled.clearDisplay();
    oled.display();
    const char *title = "H E A T  v6";
    int16_t len = strlen(title);
    int16_t startX = (OLED_W - len * 6) / 2;
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    for (int16_t i = 0; i < len; i++) {
        oled.setCursor(startX + i * 6, 28);
        oled.print(title[i]);
        if (title[i] != ' ') { oled.display(); delay(35); }
    }
    delay(350);
}

// ─── Setup / loop ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
#if DEBUG
    delay(BOOT_SERIAL_WAIT_MS);
    Serial.println("\n[boot] tof_midi_v6");
#endif

    pinMode(ENC_A,     INPUT_PULLUP);
    pinMode(ENC_B,     INPUT_PULLUP);
    pinMode(ENC_PUSH,  INPUT_PULLUP);
    pinMode(BTN_MUTE,  INPUT_PULLUP);
    pinMode(BTN_MODE,  INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
    delay(50);

    // I²C bus warm-up scan (same workaround as v5/v6_rx)
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
#endif

    bool oledOK = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    oled.setRotation(2);                       // panel mounted upside-down on the dongle PCB
    if (oledOK) playIntro();

    // NeoPixel feedback
    initNeoPixel();

    // USB-MIDI — enableDFU before begin (ESP32-S3 TinyUSB quirk)
    USB.enableDFU();
    usbMidi.begin();
    USB.begin();

    // DIN + TRS MIDI OUT — Serial1 TX only, 31250 baud
    Serial1.begin(31250, SERIAL_8N1, -1, MIDI_TX_PIN);

    // ESP-NOW
    bool nowOK = initEspNow();
    if (!nowOK) showToast("ESPNOW FAIL");

    // Expression output (MCP4561 digipot)
    if (!initDigipot()) showToast("DIGIPOT FAIL");

    // VL53L5CX (blob upload ~1 s — covered by WDT)
    if (!initToF()) showToast("ToF FAIL");

    // NVS settings + preset bitmask
    loadSettings();
    refreshPresetsExist();

    // 10 s WDT
    const esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = 10000,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_init(&wdtCfg);
    esp_task_wdt_add(nullptr);

    rateWindowStart = millis();
    oledDirty = true;

#if DEBUG
    Serial.printf("[boot] mode=%u  midiCh=%u\n",
                  (unsigned)currentMode, (unsigned)midiCh);
#endif
}

void loop() {
    esp_task_wdt_reset();
    const uint32_t now = millis();

    handleEncoder();
    handleButtons();

    pollToF();
    drainRx();
    routeMode();          // mode → MIDI; also drives the digipot expression out
    updateNeoPixel();     // gesture visual feedback

    // NVS save debounce — accumulate setting changes, flush after 2 s idle.
    if (settingsDirty && (now - dirtyTime) > NVS_SAVE_DEBOUNCE_MS) {
        saveSettings();
    }

    // Rolling pps over 1 s for INFO page
    if ((int32_t)(now - rateWindowStart) >= 1000) {
        uint32_t inWin = pktTotal - rateWindowPkts;
        uint32_t dt    = (now - rateWindowStart);
        lastRatePps    = (dt > 0) ? (inWin * 1000UL / dt) : 0;
        rateWindowPkts = pktTotal;
        rateWindowStart = now;
        if (currentPage == PG_INFO || currentPage == PG_LIVE) oledDirty = true;
    }

    if (toastText) {
        if (now < toastUntil) oledDirty = true;
        else                  toastText = nullptr;
    }
    if (currentPage == PG_LIVE) oledDirty = true;

    if (oledDirty && (now - tOled) >= OLED_REFRESH_MS) {
        tOled = now;
        refreshOLED();
    }
}

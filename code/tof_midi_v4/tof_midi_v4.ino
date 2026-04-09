// tof_midi_v4.ino — ToF MIDI controller with visual UI
//
// VL53L1X — long-range hand gestures (10–500mm)
//
// MIDI: CC20 (distance), CC21 (velocity), Pitch Bend (14-bit)
//
// Board: XIAO_ESP32S3,  USB: USB-OTG (TinyUSB)
// Libraries: SparkFun VL53L1X, Adafruit NeoPixel, Adafruit SSD1306, Adafruit GFX

// ─── Config ──────────────────────────────────────────────────────────────────

#define DEBUG           1

#define I2C_SDA         5
#define I2C_SCL         6
#define I2C_FREQ        400000

#define MIDI_CH         1
#define MIDI_CH0        (MIDI_CH - 1)
#define CC_DIST         20
#define CC_VEL          21
#define BLE_NAME        "MidiGuitar"

#define DIST_MIN_MM     10
#define DIST_MAX_MM     500
#define VL53_ADDR       0x30
#define XSHUT_VL53      43

#define POLL_MS         20
#define EMA_ALPHA       0.2f
#define VEL_DEAD_MM     2.0f
#define VEL_MAX_MM      30.0f

#define CURVE_LINEAR    0
#define CURVE_LOG       1
#define CURVE_EXP       2
#define CURVE           CURVE_LINEAR

#define STRIP_PIN       2
#define SW_TRANSPORT    3
#define BTN_BYPASS      4
#define ENC_A           7
#define ENC_B           8
#define ENC_PUSH        9

#define NUM_PIXELS      16
#define STRIP_BRIGHT    40

#define OLED_W          128
#define OLED_H          64
#define OLED_ADDR       0x3C

#define NUM_PAGES       3
#define DEBOUNCE_MS     180

// ─── Includes ────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <SparkFun_VL53L1X.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ─── Types ───────────────────────────────────────────────────────────────────

enum Mode      : uint8_t { MODE_CC, MODE_PB, MODE_COUNT };
enum Transport : uint8_t { TR_USB, TR_BLE, TR_BOTH, TR_COUNT };
enum Page      : uint8_t { PG_LIVE, PG_TRANSPORT, PG_MODE };

// ─── BLE MIDI ────────────────────────────────────────────────────────────────

#define BLE_MIDI_SVC "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLE_MIDI_CHR "7772E5DB-3868-4112-A1A9-F2669D106BF3"

static bool bleConnected = false;
static bool bleInitDone  = false;

class MidiCB : public BLEServerCallbacks {
    void onConnect(BLEServer *)    override { bleConnected = true; }
    void onDisconnect(BLEServer *) override {
        bleConnected = false;
        BLEDevice::startAdvertising();
    }
};
static MidiCB midiCB;

// ─── Globals ─────────────────────────────────────────────────────────────────

static USBMIDI           usbMidi;
static SFEVL53L1X        sensor;
static Adafruit_NeoPixel ring(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_SSD1306  oled(OLED_W, OLED_H, &Wire, -1);
static BLECharacteristic *bleChr = nullptr;

static Transport transport = TR_USB;
static Mode      mode      = MODE_CC;
static bool      bypassed  = false;

static float   ema      = -1.0f;
static float   prevEma  = -1.0f;
static int16_t prevDist = -1;
static int16_t prevVel  = -1;
static int16_t prevBend = -1;

static int16_t liveDist = 0;
static int16_t liveVel  = 0;
static int16_t liveBend = 0;

static unsigned long tPoll = 0;
static unsigned long tOled = 0;

// ─── Encoder ─────────────────────────────────────────────────────────────────

static volatile int8_t encDelta = 0;
static unsigned long   encPushTime = 0;
static bool            encPushPrev = true;  // HIGH = not pressed (INPUT_PULLUP)

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

// ─── Helpers ─────────────────────────────────────────────────────────────────

static float applyCurve(float t) {
#if CURVE == CURVE_LOG
    return log1pf(t * (M_E - 1.0f));
#elif CURVE == CURVE_EXP
    return (expf(t) - 1.0f) / (M_E - 1.0f);
#else
    return t;
#endif
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

static bool wantsUSB() { return transport == TR_USB || transport == TR_BOTH; }
static bool wantsBLE() { return transport == TR_BLE || transport == TR_BOTH; }

// ─── BLE send ────────────────────────────────────────────────────────────────

static void bleSend(uint8_t st, uint8_t d1, uint8_t d2) {
    if (!bleConnected || !bleChr) return;
    uint16_t ts = millis() & 0x1FFF;
    uint8_t p[5] = {
        (uint8_t)(0x80 | (ts >> 7)), (uint8_t)(0x80 | (ts & 0x7F)),
        st, d1, d2
    };
    bleChr->setValue(p, 5);
    bleChr->notify();
}

// ─── MIDI output ─────────────────────────────────────────────────────────────

static void sendCC(uint8_t cc, uint8_t val) {
    if (bypassed) return;
    if (wantsUSB()) usbMidi.controlChange(cc, val, MIDI_CH);
    if (wantsBLE()) bleSend(0xB0 | MIDI_CH0, cc, val);
}

static void sendPB(uint16_t bend) {
    if (bypassed) return;
    if (wantsUSB()) usbMidi.pitchBend(bend, MIDI_CH);
    if (wantsBLE()) bleSend(0xE0 | MIDI_CH0, bend & 0x7F, (bend >> 7) & 0x7F);
}

// ─── Sensor init ─────────────────────────────────────────────────────────────

static bool initSensor() {
    pinMode(XSHUT_VL53, OUTPUT);
    digitalWrite(XSHUT_VL53, LOW);
    delay(10);
    digitalWrite(XSHUT_VL53, HIGH);
    delay(10);
    sensor.begin(Wire);
    sensor.setI2CAddress(VL53_ADDR);
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

// ─── BLE init ────────────────────────────────────────────────────────────────

static void initBLE() {
    if (bleInitDone) return;
    BLEDevice::init(BLE_NAME);
    BLEServer *srv = BLEDevice::createServer();
    srv->setCallbacks(&midiCB);
    BLEService *svc = srv->createService(BLE_MIDI_SVC);
    bleChr = svc->createCharacteristic(BLE_MIDI_CHR,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_WRITE_NR);
    bleChr->addDescriptor(new BLE2902());
    svc->start();
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_MIDI_SVC);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->start();
    bleInitDone = true;
}

// ─── NeoPixel ────────────────────────────────────────────────────────────────

static uint32_t modeColor() {
    if (bypassed) return ring.Color(40, 40, 0);
    if (wantsUSB() && !wantsBLE())
        return mode == MODE_CC ? ring.Color(0, 255, 0)   : ring.Color(180, 0, 255);
    if (wantsBLE() && !wantsUSB())
        return mode == MODE_CC ? ring.Color(0, 80, 255)  : ring.Color(0, 220, 220);
    return mode == MODE_CC     ? ring.Color(0, 200, 100) : ring.Color(100, 0, 200);
}

static void showRing(int16_t val) {
    static int16_t last = -1;
    static bool lastBp = false;
    if (val == last && bypassed == lastBp) return;
    last = val; lastBp = bypassed;
    int n = constrain((val * NUM_PIXELS + 63) / 127, 0, NUM_PIXELS);
    uint32_t col = modeColor();
    ring.clear();
    for (int i = 0; i < n; i++) ring.setPixelColor(i, col);
    ring.show();
}

static void showChase() {
    static uint8_t p = 0;
    ring.clear();
    ring.setPixelColor(p % NUM_PIXELS, ring.Color(0, 40, 200));
    ring.setPixelColor((p + 1) % NUM_PIXELS, ring.Color(0, 15, 80));
    ring.show();
    p++;
}

static void showError() {
    ring.fill((millis() / 100) % 2 ? ring.Color(255, 0, 0) : 0);
    ring.show();
}

static void updateRing() {
    if (wantsBLE() && !bleConnected && !wantsUSB()) { showChase(); return; }
    static bool lastBp = false;
    if (bypassed != lastBp) {
        ring.setBrightness(bypassed ? STRIP_BRIGHT / 2 : STRIP_BRIGHT);
        lastBp = bypassed;
    }
    showRing(liveDist);
}

// ─── OLED drawing — Digitakt-style dense UI ─────────────────────────────────

// Page dots — bottom edge
static void drawPageDots(uint8_t current, uint8_t total) {
    int16_t startX = OLED_W / 2 - (total * 8) / 2;
    for (uint8_t i = 0; i < total; i++) {
        int16_t dx = startX + i * 8 + 3;
        if (i == current)
            oled.fillRect(dx - 1, 62, 4, 2, SSD1306_WHITE);
        else
            oled.drawPixel(dx, 63, SSD1306_WHITE);
    }
}

// Horizontal separator line
static void drawHLine(int16_t y) {
    oled.drawLine(0, y, OLED_W - 1, y, SSD1306_WHITE);
}

// ─── OLED pages ──────────────────────────────────────────────────────────────

static void drawPageLive() {
    // ── Row 0: status bar (8px) ──
    // Transport
    oled.setCursor(0, 0);
    if (wantsUSB() && wantsBLE()) oled.print("U+B");
    else if (wantsUSB())          oled.print("USB");
    else                          oled.print("BLE");

    // Mode
    oled.setCursor(24, 0);
    oled.print(mode == MODE_CC ? "CC" : "PB");

    // Channel
    oled.setCursor(42, 0);
    oled.printf("Ch%d", MIDI_CH);

    // BLE status
    if (wantsBLE()) {
        oled.setCursor(72, 0);
        if (bleConnected) oled.print("*");  // connected
        else if ((millis() / 400) % 2) oled.print("~");  // scanning
    }

    // MUTE — inverted badge
    if (bypassed) {
        oled.fillRect(OLED_W - 24, 0, 24, 8, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(OLED_W - 22, 0);
        oled.print("MUTE");
        oled.setTextColor(SSD1306_WHITE);
    }

    drawHLine(9);

    // ── Row 1: DIST bar (main control) ──
    int16_t r1 = 12;
    oled.setCursor(0, r1);
    oled.print(mode == MODE_CC ? "DST" : "BND");

    // Bar: 20px to 90px (70px wide), 7px tall
    int16_t bx = 20, by = r1, bw = 70, bh = 7;
    int16_t fill = constrain((int32_t)liveDist * bw / 127, 0, bw);
    oled.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    if (fill > 0) oled.fillRect(bx + 1, by + 1, fill - 1, bh - 2, SSD1306_WHITE);

    // Value — large
    oled.setTextSize(2);
    oled.setCursor(94, r1 - 2);
    oled.printf("%3d", liveDist);
    oled.setTextSize(1);

    drawHLine(21);

    // ── Row 2: VEL bar ──
    int16_t r2 = 24;
    oled.setCursor(0, r2);
    oled.print("VEL");

    int16_t vFill = constrain((int32_t)liveVel * bw / 127, 0, bw);
    oled.drawRect(bx, r2, bw, bh, SSD1306_WHITE);
    if (vFill > 0) oled.fillRect(bx + 1, r2 + 1, vFill - 1, bh - 2, SSD1306_WHITE);

    oled.setTextSize(2);
    oled.setCursor(94, r2 - 2);
    oled.printf("%3d", liveVel);
    oled.setTextSize(1);

    drawHLine(33);

    // ── Row 3: secondary info ──
    if (mode == MODE_PB) {
        oled.setCursor(0, 36);
        oled.printf("BEND %5d", liveBend);
    } else {
        // Show CC numbers
        oled.setCursor(0, 36);
        oled.printf("CC%d  CC%d", CC_DIST, CC_VEL);
    }

    // Distance in mm (raw sensor feedback)
    oled.setCursor(80, 36);
    if (ema >= 0) oled.printf("%3dmm", (int)ema);

    // ── Row 4: visual mini-graph (last 44px of screen) ──
    // Tiny bar-graph style — fill proportional to dist
    int16_t gy = 46;
    int16_t gw = OLED_W;
    int16_t gh = 14;
    int16_t gFill = constrain((int32_t)liveDist * gw / 127, 0, gw);
    oled.fillRect(0, gy, gFill, gh, SSD1306_WHITE);
    // Tick marks every 25%
    for (int i = 1; i < 4; i++) {
        int16_t tx = gw * i / 4;
        oled.drawLine(tx, gy, tx, gy + 2, gFill > tx ? SSD1306_BLACK : SSD1306_WHITE);
    }

    drawPageDots(0, NUM_PAGES);
}

static void drawPageTransport() {
    oled.setCursor(0, 0);
    oled.print("OUTPUT");
    drawHLine(9);

    // Three slots — Digitakt style: filled block = selected
    const char *labels[] = { "USB", "BLE", "BOTH" };
    int16_t slotW = OLED_W / 3;
    int16_t slotY = 14;
    int16_t slotH = 38;

    for (uint8_t i = 0; i < TR_COUNT; i++) {
        int16_t sx = i * slotW;
        bool sel = ((uint8_t)transport == i);

        if (sel) {
            // Selected: filled block, black text
            oled.fillRect(sx + 1, slotY, slotW - 2, slotH, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            // Unselected: outlined
            oled.drawRect(sx + 1, slotY, slotW - 2, slotH, SSD1306_WHITE);
            oled.setTextColor(SSD1306_WHITE);
        }

        // Icon inside slot
        int16_t iconCx = sx + slotW / 2;
        int16_t iconY = slotY + 5;
        uint16_t col = sel ? SSD1306_BLACK : SSD1306_WHITE;

        if (i == 0) {
            // USB: trident shape
            oled.drawLine(iconCx, iconY, iconCx, iconY + 14, col);
            oled.drawLine(iconCx, iconY, iconCx - 3, iconY + 3, col);
            oled.drawLine(iconCx, iconY, iconCx + 3, iconY + 3, col);
            oled.drawLine(iconCx, iconY + 8, iconCx - 4, iconY + 5, col);
            oled.drawCircle(iconCx - 4, iconY + 4, 1, col);
            oled.drawLine(iconCx, iconY + 10, iconCx + 4, iconY + 7, col);
            oled.fillRect(iconCx + 3, iconY + 6, 3, 3, col);
            oled.drawCircle(iconCx, iconY + 15, 2, col);
        } else if (i == 1) {
            // BLE: rune
            oled.drawLine(iconCx, iconY, iconCx, iconY + 16, col);
            oled.drawLine(iconCx, iconY, iconCx + 4, iconY + 4, col);
            oled.drawLine(iconCx + 4, iconY + 4, iconCx - 4, iconY + 12, col);
            oled.drawLine(iconCx - 4, iconY + 4, iconCx + 4, iconY + 12, col);
            oled.drawLine(iconCx + 4, iconY + 12, iconCx, iconY + 16, col);
        } else {
            // BOTH: U+B text large
            oled.setTextSize(2);
            int16_t tx = sx + (slotW - 24) / 2;
            oled.setCursor(tx, iconY + 2);
            oled.print("UB");
            oled.setTextSize(1);
        }

        // Label at bottom of slot
        int16_t lw = strlen(labels[i]) * 6;
        oled.setCursor(sx + (slotW - lw) / 2, slotY + slotH - 10);
        oled.print(labels[i]);
        oled.setTextColor(SSD1306_WHITE);
    }

    drawPageDots(1, NUM_PAGES);
}

static void drawPageMode() {
    oled.setCursor(0, 0);
    oled.print("MODE");
    drawHLine(9);

    const char *labels[] = { "CC", "BEND" };
    int16_t half = OLED_W / 2;
    int16_t slotY = 14;
    int16_t slotH = 38;

    for (uint8_t i = 0; i < MODE_COUNT; i++) {
        int16_t sx = i * half;
        bool sel = ((uint8_t)mode == i);

        if (sel) {
            oled.fillRect(sx + 1, slotY, half - 2, slotH, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.drawRect(sx + 1, slotY, half - 2, slotH, SSD1306_WHITE);
            oled.setTextColor(SSD1306_WHITE);
        }

        int16_t iconCx = sx + half / 2;
        int16_t iconY = slotY + 4;
        uint16_t col = sel ? SSD1306_BLACK : SSD1306_WHITE;

        if (i == 0) {
            // Sine wave
            for (int16_t j = 0; j < 30; j++) {
                float t = (float)j / 29.0f * 2.0f * PI;
                int16_t py = iconY + 8 - (int16_t)(6.0f * sinf(t));
                oled.drawPixel(sx + (half - 30) / 2 + j, py, col);
                oled.drawPixel(sx + (half - 30) / 2 + j, py + 1, col);
            }
        } else {
            // Pitch bend arrow
            oled.drawLine(iconCx, iconY + 16, iconCx, iconY + 2, col);
            oled.drawLine(iconCx, iconY + 2, iconCx - 4, iconY + 6, col);
            oled.drawLine(iconCx, iconY + 2, iconCx + 4, iconY + 6, col);
            oled.drawLine(iconCx - 6, iconY + 12, iconCx, iconY + 16, col);
            oled.drawLine(iconCx + 6, iconY + 12, iconCx, iconY + 16, col);
        }

        int16_t lw = strlen(labels[i]) * 6;
        oled.setCursor(sx + (half - lw) / 2, slotY + slotH - 10);
        oled.print(labels[i]);
        oled.setTextColor(SSD1306_WHITE);
    }

    drawPageDots(2, NUM_PAGES);
}

static void refreshOLED() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    switch (currentPage) {
        case PG_LIVE:      drawPageLive();      break;
        case PG_TRANSPORT: drawPageTransport(); break;
        case PG_MODE:      drawPageMode();      break;
    }
    oled.display();
    oledDirty = false;
}

// ─── Intro animation ─────────────────────────────────────────────────────────

static void playIntro() {
    oled.clearDisplay();
    oled.display();

    // Horizontal line sweeps across center
    for (int16_t x = 0; x < OLED_W; x += 4) {
        oled.drawLine(0, 31, x, 31, SSD1306_WHITE);
        oled.display();
    }
    delay(60);

    // Box expands from center line
    for (int16_t h = 0; h <= 28; h += 3) {
        oled.clearDisplay();
        oled.drawRoundRect(4, 32 - h, OLED_W - 8, h * 2, 4, SSD1306_WHITE);
        oled.display();
    }
    delay(80);

    // Title types in — textSize 1 (~9px) for clean fit
    const char *title = "M I D I G U I T A R";
    uint8_t len = strlen(title);
    int16_t totalW = len * 6;
    int16_t startX = (OLED_W - totalW) / 2;

    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    // Type each real character (skip spaces for timing)
    for (uint8_t i = 0; i < len; i++) {
        oled.setCursor(startX + i * 6, 28);
        oled.print(title[i]);
        if (title[i] != ' ') {
            oled.display();
            delay(40);
        }
    }

    delay(150);
    oled.setCursor((OLED_W - 12) / 2, 40);
    oled.print("v4");
    oled.display();

    delay(400);

    // Fade out: progressively draw black rectangles
    for (int16_t i = 0; i < OLED_H / 2; i += 2) {
        oled.fillRect(0, i, OLED_W, 2, SSD1306_BLACK);
        oled.fillRect(0, OLED_H - i - 2, OLED_W, 2, SSD1306_BLACK);
        oled.display();
        delay(15);
    }

    oled.clearDisplay();
    oled.display();
}

// ─── Encoder + menu ──────────────────────────────────────────────────────────

static void handleEncoder() {
    noInterrupts();
    int8_t delta = encDelta;
    encDelta = 0;
    interrupts();

    // Push = next page (edge-triggered: only on press-down, not while held)
    bool pushed = false;
    bool btnNow = digitalRead(ENC_PUSH) == LOW;
    if (btnNow && !encPushPrev && millis() - encPushTime > DEBOUNCE_MS) {
        pushed = true;
        encPushTime = millis();
    }
    encPushPrev = btnNow;

    if (delta == 0 && !pushed) return;
    oledDirty = true;

    if (pushed) {
        // Cycle pages: Live -> Transport -> Mode -> Live ...
        currentPage = (Page)wrap((int8_t)currentPage + 1, NUM_PAGES);
#if DEBUG
        Serial.printf("Page -> %d\n", currentPage);
#endif
        return;
    }

    // Rotate = change setting on current page
    if (delta != 0) {
        switch (currentPage) {
            case PG_TRANSPORT: {
                transport = (Transport)wrap((int8_t)transport + (delta > 0 ? 1 : -1), TR_COUNT);
                if (wantsBLE() && !bleInitDone) initBLE();
#if DEBUG
                Serial.printf("Transport -> %d\n", transport);
#endif
            } break;

            case PG_MODE: {
                mode = (Mode)wrap((int8_t)mode + (delta > 0 ? 1 : -1), MODE_COUNT);
                prevDist = -1;
                prevBend = -1;
                liveDist = 0;
#if DEBUG
                Serial.printf("Mode -> %d\n", mode);
#endif
            } break;

            default: break; // Live page: rotate does nothing
        }
    }
}

// ─── Bypass ──────────────────────────────────────────────────────────────────

static unsigned long btnTime = 0;

static void readBypass() {
    if (digitalRead(BTN_BYPASS) == LOW && millis() - btnTime > DEBOUNCE_MS) {
        bypassed = !bypassed;
        btnTime = millis();
        oledDirty = true;
    }
}

// ─── Sensor poll ─────────────────────────────────────────────────────────────

static void pollSensor() {
    if (!sensor.checkForDataReady()) return;
    int16_t dist = -1;
    if (sensor.getRangeStatus() == 0) dist = sensor.getDistance();
    sensor.clearInterrupt();
    if (dist < 0) return;

    prevEma = ema;
    updateEma(ema, (float)dist, EMA_ALPHA);
    float curved = normCurve(ema, DIST_MIN_MM, DIST_MAX_MM);

    if (mode == MODE_CC) {
        int16_t cc = constrain((int16_t)lroundf(curved * 127.0f), 0, 127);
        liveDist = cc;
        if (cc != prevDist) { prevDist = cc; sendCC(CC_DIST, cc); }
    } else {
        int16_t b = constrain((int16_t)lroundf(curved * 16383.0f), (int16_t)0, (int16_t)16383);
        liveBend = b;
        liveDist = b / (16383 / 127);
        if (b != prevBend) { prevBend = b; sendPB(b); }
    }

    float d = fabsf(ema - prevEma);
    if (d < VEL_DEAD_MM) d = 0.0f;
    int16_t v = (int16_t)lroundf(constrain(d / VEL_MAX_MM * 127.0f, 0.0f, 127.0f));
    liveVel = v;
    if (v != prevVel) { prevVel = v; sendCC(CC_VEL, v); }
}

// ─── Setup & loop ────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(SW_TRANSPORT, INPUT_PULLUP);
    transport = digitalRead(SW_TRANSPORT) == HIGH ? TR_USB : TR_BLE;
    pinMode(BTN_BYPASS, INPUT_PULLUP);
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_PUSH, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);

    USB.enableDFU();
    usbMidi.begin();
    USB.begin();
    if (wantsBLE()) initBLE();

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        playIntro();
    }

    ring.begin();
    ring.setBrightness(STRIP_BRIGHT);
    ring.show();

    if (!initSensor()) {
        Serial.println("ERR: VL53L1X");
        oled.clearDisplay();
        oled.setTextSize(2);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(8, 10);
        oled.print("SENSOR");
        oled.setCursor(8, 32);
        oled.print("ERROR");
        oled.setTextSize(1);
        oled.setCursor(8, 52);
        oled.print("Check wiring");
        oled.display();
        while (true) { showError(); delay(50); }
    }

#if DEBUG
    Serial.printf("Ready v4 — %d %d CC%d+%d ch%d\n",
                  transport, mode, CC_DIST, CC_VEL, MIDI_CH);
#endif
    oledDirty = true;
}

void loop() {
    handleEncoder();
    readBypass();

    if (millis() - tPoll >= POLL_MS) {
        tPoll = millis();
        pollSensor();
        updateRing();
        if (currentPage == PG_LIVE) oledDirty = true;
    }

    if (oledDirty && millis() - tOled >= 66) {
        tOled = millis();
        refreshOLED();
    }
}

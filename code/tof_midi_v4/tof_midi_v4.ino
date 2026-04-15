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

// Hardware pins & constants (true compile-time)
#define BLE_NAME        "MidiGuitar"
#define VL53_ADDR       0x30
#define XSHUT_VL53      43
#define VL53_INT        1
#define POLL_MS         20
#define EMA_ALPHA       0.2f
#define VEL_DEAD_MM     2.0f
#define VEL_MAX_MM      30.0f
#define CURVE_LINEAR    0
#define CURVE_LOG       1
#define CURVE_EXP       2
#define STRIP_PIN       9
#define BTN_EDIT        44
#define ENC_A           7
#define ENC_B           8
#define ENC_PUSH        4
#define NUM_PIXELS      16
#define OLED_W          128
#define OLED_H          64
#define OLED_ADDR       0x3C
#define NUM_PAGES       4
#define DEBOUNCE_MS     180

// Defaults (for factory reset)
#define MIDI_CH_DEFAULT      1
#define CC_DIST_DEFAULT      20
#define CC_VEL_DEFAULT       21
#define DIST_MIN_DEFAULT     10
#define DIST_MAX_DEFAULT     500
#define CURVE_DEFAULT        CURVE_LINEAR
#define STRIP_BRIGHT_DEFAULT 40

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
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ─── Types ───────────────────────────────────────────────────────────────────

enum Mode      : uint8_t { MODE_CC, MODE_PB, MODE_COUNT };
enum Transport : uint8_t { TR_USB, TR_BLE, TR_BOTH, TR_COUNT };
enum Page      : uint8_t { PG_LIVE, PG_TRANSPORT, PG_MODE, PG_SETTINGS };

// ─── BLE MIDI ────────────────────────────────────────────────────────────────

#define BLE_MIDI_SVC "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLE_MIDI_CHR "7772E5DB-3868-4112-A1A9-F2669D106BF3"

static bool bleConnected = false;
static bool bleInitDone  = false;
static unsigned long bleFlashUntil = 0;
static unsigned long wipeUntil     = 0;
static uint32_t      wipeColor     = 0;

class MidiCB : public BLEServerCallbacks {
    void onConnect(BLEServer *)    override {
        bleConnected = true;
        bleFlashUntil = millis() + 200;
    }
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
static Preferences       prefs;

// Settings page state
#define NUM_SETTINGS    8
static int8_t  settingsCursor  = 0;
static bool    settingsEditing = false;   // true = rotate edits value
static bool    settingsDirty   = false;
static unsigned long dirtyTime = 0;
static unsigned long editBtnTime = 0;
static bool    editBtnPrev     = true;    // INPUT_PULLUP

static Transport transport = TR_USB;
static Mode      mode      = MODE_CC;
static bool      bypassed  = false;
static bool      sensorOK  = false;

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
static unsigned long lastActivity = 0;  // screensaver timer
static bool          oledAsleep   = false;

// ─── Sensor interrupt ────────────────────────────────────────────────────────

static volatile bool sensorDataReady = false;

static void IRAM_ATTR sensorISR() {
    sensorDataReady = true;
}

// ─── Encoder ─────────────────────────────────────────────────────────────────

static volatile int8_t encDelta = 0;
static unsigned long   encPushTime  = 0;
static unsigned long   encPushDown  = 0;      // timestamp of press-down (for long-press)
static bool            encPushPrev  = true;    // HIGH = not pressed (INPUT_PULLUP)
static bool            encHeld      = false;   // true while button physically held

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

// ─── Toast overlay ──────────────────────────────────────────────────────────

static const char   *toastText  = nullptr;
static unsigned long toastUntil = 0;

static void showToast(const char *msg) {
    toastText  = msg;
    toastUntil = millis() + 500;
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
    if (wantsUSB()) usbMidi.controlChange(cc, val, midiCh);
    if (wantsBLE()) bleSend(0xB0 | (midiCh - 1), cc, val);
}

static void sendPB(uint16_t bend) {
    if (bypassed) return;
    if (wantsUSB()) usbMidi.pitchBend(bend, midiCh);
    if (wantsBLE()) bleSend(0xE0 | (midiCh - 1), bend & 0x7F, (bend >> 7) & 0x7F);
}

// ─── Sensor init ─────────────────────────────────────────────────────────────

static bool initSensor() {
    pinMode(XSHUT_VL53, OUTPUT);
    digitalWrite(XSHUT_VL53, LOW);
    delay(10);
    digitalWrite(XSHUT_VL53, HIGH);
    delay(10);
    if (sensor.begin(Wire) != 0) return false;
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

    // Attach data-ready interrupt (INT pin active LOW)
    pinMode(VL53_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(VL53_INT), sensorISR, FALLING);

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

// Extract HSV hue from mode/transport combo (for gradient)
static uint16_t modeHue() {
    if (wantsUSB() && !wantsBLE())
        return mode == MODE_CC ? 21845  : 49152;  // green : purple
    if (wantsBLE() && !wantsUSB())
        return mode == MODE_CC ? 38229  : 32768;  // blue : cyan
    return mode == MODE_CC     ? 28000  : 45000;  // teal : violet
}

// Trigger a mode/transport wipe animation
static void triggerWipe() {
    wipeColor = ring.ColorHSV(modeHue(), 255, 200);
    wipeUntil = millis() + 150;
}

// Gradient arc — leading LED bright, trailing fades to 30%
static void showRing(int16_t val) {
    int n = constrain((val * NUM_PIXELS + 63) / 127, 0, NUM_PIXELS);
    uint16_t hue = modeHue();
    ring.clear();
    for (int i = 0; i < n; i++) {
        uint8_t bright = (n > 1) ? map(i, 0, n - 1, 76, 255) : 255;
        ring.setPixelColor(i, ring.ColorHSV(hue, 255, bright));
    }
    // Velocity pulse — flash leading LED white on fast movement
    if (liveVel > 100 && n > 0) {
        ring.setPixelColor(n - 1, ring.Color(255, 255, 255));
    }
    ring.show();
}

// Bypass breathing — sine-wave amber glow
static void showBreathe() {
    float phase = sinf(millis() * TWO_PI / 2000.0f);
    uint8_t b = 10 + (uint8_t)(30.0f * (1.0f + phase) / 2.0f);
    uint32_t col = ring.Color(b, b, 0);
    for (int i = 0; i < NUM_PIXELS; i++) ring.setPixelColor(i, col);
    ring.show();
}

// BLE scanning chase — two-LED spinner
static void showChase() {
    static uint8_t p = 0;
    ring.clear();
    ring.setPixelColor(p % NUM_PIXELS, ring.Color(0, 40, 200));
    ring.setPixelColor((p + 1) % NUM_PIXELS, ring.Color(0, 15, 80));
    ring.show();
    p++;
}

static void updateRing() {
    ring.setBrightness(stripBright);
    // BLE connection flash — overrides everything for 200ms
    if (millis() < bleFlashUntil) {
        ring.fill(ring.Color(0, 60, 255));
        ring.show();
        return;
    }

    // Mode/transport change wipe — clockwise fill in new color
    if (millis() < wipeUntil) {
        unsigned long elapsed = 150 - (wipeUntil - millis());
        int n = (int)(elapsed * NUM_PIXELS / 150);
        ring.clear();
        for (int i = 0; i < n && i < NUM_PIXELS; i++)
            ring.setPixelColor(i, wipeColor);
        ring.show();
        return;
    }

    // BLE scanning chase (only when BLE-only and not connected)
    if (wantsBLE() && !bleConnected && !wantsUSB()) { showChase(); return; }

    // Bypass breathing
    if (bypassed) { showBreathe(); return; }

    showRing(liveDist);
}

// ─── OLED drawing — Elektron-style dense UI ─────────────────────────────────

// Page indicators — active: tall filled block, inactive: thin dash
static void drawPageDots(uint8_t current, uint8_t total) {
    int16_t startX = OLED_W / 2 - (total * 8) / 2;
    for (uint8_t i = 0; i < total; i++) {
        int16_t dx = startX + i * 8;
        if (i == current)
            oled.fillRect(dx, 60, 5, 4, SSD1306_WHITE);   // tall bright block
        else
            oled.fillRect(dx + 1, 63, 3, 1, SSD1306_WHITE); // thin low dash
    }
}

// Separator line
static void drawHLine(int16_t y) {
    oled.drawFastHLine(0, y, OLED_W, SSD1306_WHITE);
}

// Inverted rounded badge — returns width consumed
static int16_t drawBadge(int16_t x, int16_t y, const char *txt) {
    int16_t w = strlen(txt) * 6 + 4;
    oled.fillRoundRect(x, y, w, 9, 2, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(x + 2, y + 1);
    oled.print(txt);
    oled.setTextColor(SSD1306_WHITE);
    return w;
}

// Segmented bar graph — Elektron signature look
// 3px filled segments with 1px gaps
static void drawSegBar(int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t val, int16_t maxVal) {
    int16_t fillW = constrain((int32_t)val * w / maxVal, 0, w);
    // Draw segments within filled region
    for (int16_t sx = 0; sx < w; sx += 4) {
        int16_t segW = (sx + 3 <= w) ? 3 : w - sx;
        if (sx < fillW) {
            // Filled segment
            int16_t fw = (sx + 3 <= fillW) ? segW : max((int16_t)0, (int16_t)(fillW - sx));
            if (fw > 0) oled.fillRect(x + sx, y, fw, h, SSD1306_WHITE);
        }
        // Draw dim outline tick for empty segments (every other gap)
        if (sx >= fillW && (sx / 4) % 2 == 0) {
            oled.drawPixel(x + sx, y + h / 2, SSD1306_WHITE);
        }
    }
}

// Diagonal mute stripes overlay — visual bypass cue
static void drawMuteStripes(int16_t x, int16_t y, int16_t w, int16_t h) {
    for (int16_t i = 0; i < w + h; i += 4) {
        int16_t x0 = x + i;
        int16_t y0 = y;
        int16_t x1 = x + i - h;
        int16_t y1 = y + h;
        // Clip to bounds
        if (x0 > x + w) { y0 += x0 - (x + w); x0 = x + w; }
        if (x1 < x)      { y1 -= x - x1; x1 = x; }
        if (y0 < y + h && y1 > y)
            oled.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
    }
}

// ─── OLED pages ──────────────────────────────────────────────────────────────

static void drawPageLive() {
    // ── Row 0: status bar (0–8) — inverted badges ──
    int16_t bx = 0;
    // Transport badge
    const char *trLabel = wantsUSB() && wantsBLE() ? "U+B"
                        : wantsUSB()               ? "USB"
                        :                            "BLE";
    bx += drawBadge(0, 0, trLabel) + 2;

    // Mode badge
    bx += drawBadge(bx, 0, mode == MODE_CC ? "CC" : "PB") + 2;

    // Channel — plain text (less important)
    oled.setCursor(bx, 1);
    oled.printf("Ch%d", midiCh);

    // BLE connection indicator
    if (wantsBLE()) {
        if (bleConnected) {
            oled.fillCircle(82, 4, 2, SSD1306_WHITE); // solid dot = linked
        } else if ((millis() / 300) % 2) {
            oled.drawCircle(82, 4, 2, SSD1306_WHITE); // pulsing ring = scanning
        }
    }

    // MUTE or SENSOR badge — right-aligned
    if (!sensorOK) {
        drawBadge(OLED_W - 22, 0, "ERR");
    } else if (bypassed) {
        drawBadge(OLED_W - 28, 0, "MUTE");
    }

    drawHLine(10);

    // ── Row 1: main parameter bar (12–22) ──
    int16_t r1 = 12;
    oled.setCursor(0, r1);
    oled.print(mode == MODE_CC ? "DST" : "BND");

    // Segmented bar: 20–89 (70px), 7px tall
    int16_t barX = 20, barW = 70, barH = 7;
    if (bypassed) {
        drawMuteStripes(barX, r1, barW, barH);
    } else {
        drawSegBar(barX, r1, barW, barH, liveDist, 127);
    }

    // Value — right-aligned
    oled.setCursor(94, r1 - 1);
    oled.printf("%3d", liveDist);

    drawHLine(22);

    // ── Row 2: velocity bar (24–34) ──
    int16_t r2 = 24;
    oled.setCursor(0, r2);
    oled.print("VEL");

    if (bypassed) {
        drawMuteStripes(barX, r2, barW, barH);
    } else {
        drawSegBar(barX, r2, barW, barH, liveVel, 127);
    }

    oled.setCursor(94, r2 - 1);
    oled.printf("%3d", liveVel);

    drawHLine(34);

    // ── Row 3: secondary info (36–44) ──
    if (mode == MODE_PB) {
        oled.setCursor(0, 36);
        oled.printf("BEND %5d", liveBend);
    } else {
        oled.setCursor(0, 36);
        oled.printf("CC%d  CC%d", ccDist, ccVel);
    }

    // Raw distance — right side
    oled.setCursor(80, 36);
    if (ema >= 0) oled.printf("%3dmm", (int)ema);

    drawHLine(45);

    // ── Row 4: full-width segmented activity meter (46–59) ──
    int16_t gy = 47, gh = 12;
    if (bypassed) {
        // Muted: show dashed outline
        for (int16_t dx = 0; dx < OLED_W; dx += 6) {
            oled.drawFastHLine(dx, gy, 3, SSD1306_WHITE);
            oled.drawFastHLine(dx, gy + gh - 1, 3, SSD1306_WHITE);
        }
    } else {
        drawSegBar(0, gy, OLED_W, gh, liveDist, 127);
    }
    // Quarter ticks — always visible
    for (int i = 1; i < 4; i++) {
        int16_t tx = OLED_W * i / 4;
        int16_t gFill = constrain((int32_t)liveDist * OLED_W / 127, 0, OLED_W);
        uint16_t tc = (gFill > tx) ? SSD1306_BLACK : SSD1306_WHITE;
        oled.drawFastVLine(tx, gy, 3, tc);
        oled.drawFastVLine(tx, gy + gh - 3, 3, tc);
    }

    drawPageDots(0, NUM_PAGES);
}

static void drawPageTransport() {
    // ── Full-width inverted header ──
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("OUTPUT");
    oled.setTextColor(SSD1306_WHITE);

    // ── Three Digitakt-style slots ──
    const char *labels[] = { "USB", "BLE", "BOTH" };
    int16_t slotW = 40;  // 40px per slot, centered in 128px
    int16_t margin = (OLED_W - slotW * 3 - 4) / 2;  // gap between slots = 2px
    int16_t slotY = 14;
    int16_t slotH = 40;

    for (uint8_t i = 0; i < TR_COUNT; i++) {
        int16_t sx = margin + i * (slotW + 2);
        bool sel = ((uint8_t)transport == i);

        if (sel) {
            oled.fillRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.drawRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE);
            oled.setTextColor(SSD1306_WHITE);
        }

        int16_t iconCx = sx + slotW / 2;
        int16_t iconY = slotY + 6;
        uint16_t col = sel ? SSD1306_BLACK : SSD1306_WHITE;

        if (i == 0) {
            // USB icon — bold trident
            oled.drawLine(iconCx, iconY, iconCx, iconY + 14, col);
            oled.drawLine(iconCx + 1, iconY, iconCx + 1, iconY + 14, col); // double line
            oled.drawLine(iconCx, iconY, iconCx - 4, iconY + 4, col);
            oled.drawLine(iconCx + 1, iconY, iconCx + 5, iconY + 4, col);
            oled.fillCircle(iconCx - 4, iconY + 5, 1, col);
            oled.fillRect(iconCx + 4, iconY + 4, 3, 3, col);
            oled.drawCircle(iconCx, iconY + 16, 3, col);
            oled.drawCircle(iconCx + 1, iconY + 16, 3, col);
        } else if (i == 1) {
            // BLE icon — bold Bluetooth rune
            oled.drawLine(iconCx, iconY, iconCx, iconY + 18, col);
            oled.drawLine(iconCx + 1, iconY, iconCx + 1, iconY + 18, col); // double
            oled.drawLine(iconCx, iconY, iconCx + 5, iconY + 4, col);
            oled.drawLine(iconCx + 5, iconY + 4, iconCx - 5, iconY + 14, col);
            oled.drawLine(iconCx - 5, iconY + 4, iconCx + 5, iconY + 14, col);
            oled.drawLine(iconCx + 5, iconY + 14, iconCx, iconY + 18, col);
            // Signal arcs
            oled.drawPixel(iconCx + 7, iconY + 9, col);
            oled.drawPixel(iconCx + 8, iconY + 8, col);
            oled.drawPixel(iconCx + 8, iconY + 10, col);
        } else {
            // BOTH — stacked icons compact
            oled.setTextSize(2);
            oled.setCursor(sx + (slotW - 12) / 2, iconY + 2);
            oled.print("&");
            oled.setTextSize(1);
        }

        // Label — bottom of slot
        int16_t lw = strlen(labels[i]) * 6;
        oled.setCursor(sx + (slotW - lw) / 2, slotY + slotH - 10);
        oled.print(labels[i]);
        oled.setTextColor(SSD1306_WHITE);
    }

    drawPageDots(1, NUM_PAGES);
}

static void drawPageMode() {
    // ── Full-width inverted header ──
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("MODE");
    oled.setTextColor(SSD1306_WHITE);

    // ── Two Digitakt-style slots ──
    const char *labels[] = { "CC", "BEND" };
    int16_t slotW = 58;
    int16_t gap = OLED_W - slotW * 2;
    int16_t slotY = 14;
    int16_t slotH = 40;

    for (uint8_t i = 0; i < MODE_COUNT; i++) {
        int16_t sx = gap / 2 + i * (slotW + 2);
        bool sel = ((uint8_t)mode == i);

        if (sel) {
            oled.fillRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.drawRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE);
            oled.setTextColor(SSD1306_WHITE);
        }

        int16_t iconCx = sx + slotW / 2;
        int16_t iconY = slotY + 5;
        uint16_t col = sel ? SSD1306_BLACK : SSD1306_WHITE;

        if (i == 0) {
            // CC: stepped waveform (staircase) — integer look, no float
            int16_t ox = sx + 6;
            int16_t steps[] = {6, 3, 0, 2, 5, 7, 4, 1};
            for (int16_t j = 0; j < 8; j++) {
                int16_t py = iconY + 2 + steps[j] * 2;
                int16_t px = ox + j * 5;
                oled.fillRect(px, py, 5, 2, col);
                if (j < 7) {
                    int16_t ny = iconY + 2 + steps[j + 1] * 2;
                    oled.drawFastVLine(px + 5, min(py, ny), abs(ny - py), col);
                }
            }
        } else {
            // Pitch bend: double-headed arrow with center line
            // Center reference line
            int16_t cy = iconY + 9;
            oled.drawFastHLine(iconCx - 10, cy, 20, col);
            // Up arrow
            oled.drawLine(iconCx, iconY + 2, iconCx, cy - 2, col);
            oled.drawLine(iconCx + 1, iconY + 2, iconCx + 1, cy - 2, col);
            oled.drawLine(iconCx, iconY + 2, iconCx - 4, iconY + 6, col);
            oled.drawLine(iconCx, iconY + 2, iconCx + 4, iconY + 6, col);
            // Down arrow
            oled.drawLine(iconCx, cy + 2, iconCx, iconY + 16, col);
            oled.drawLine(iconCx + 1, cy + 2, iconCx + 1, iconY + 16, col);
            oled.drawLine(iconCx, iconY + 16, iconCx - 4, iconY + 12, col);
            oled.drawLine(iconCx, iconY + 16, iconCx + 4, iconY + 12, col);
        }

        // Label at bottom
        int16_t lw = strlen(labels[i]) * 6;
        oled.setCursor(sx + (slotW - lw) / 2, slotY + slotH - 10);
        oled.print(labels[i]);
        oled.setTextColor(SSD1306_WHITE);
    }

    drawPageDots(2, NUM_PAGES);
}

// ─── Settings page — scrollable parameter list ──────────────────────────────

// Returns the label and formatted value for each setting row
static void getSettingInfo(uint8_t idx, char *label, char *val, size_t vLen) {
    switch (idx) {
        case 0: strcpy(label, "MIDI Ch");  snprintf(val, vLen, "%d", midiCh);       break;
        case 1: strcpy(label, "CC Dist");  snprintf(val, vLen, "%d", ccDist);        break;
        case 2: strcpy(label, "CC Vel");   snprintf(val, vLen, "%d", ccVel);         break;
        case 3: strcpy(label, "Dist Min"); snprintf(val, vLen, "%d", distMinMm);     break;
        case 4: strcpy(label, "Dist Max"); snprintf(val, vLen, "%d", distMaxMm);     break;
        case 5: strcpy(label, "Curve");
            snprintf(val, vLen, "%s", curve == CURVE_LOG ? "LOG" : curve == CURVE_EXP ? "EXP" : "LIN");
            break;
        case 6: strcpy(label, "LED Brt");  snprintf(val, vLen, "%d", stripBright);   break;
        case 7: strcpy(label, "Invert");   snprintf(val, vLen, "%s", distInvert ? "ON" : "OFF"); break;
    }
}

static void adjustSetting(uint8_t idx, int8_t dir) {
    switch (idx) {
        case 0: midiCh     = constrain(midiCh + dir, 1, 16);            break;
        case 1: ccDist     = constrain(ccDist + dir, 0, 127);           break;
        case 2: ccVel      = constrain(ccVel + dir, 0, 127);            break;
        case 3: distMinMm  = constrain(distMinMm + dir * 5, 5, 200);   break;
        case 4: distMaxMm  = constrain(distMaxMm + dir * 10, 100, 1000); break;
        case 5: curve      = wrap(curve + dir, 3);                       break;
        case 6: stripBright = constrain((int16_t)stripBright + dir * 5, 5, 255);
                ring.setBrightness(stripBright);                         break;
        case 7: distInvert = !distInvert;                                break;
    }
    settingsDirty = true;
    dirtyTime = millis();
}

static void drawPageSettings() {
    // Full-width inverted header
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("SETTINGS");
    // Editing indicator
    if (settingsEditing) {
        oled.setCursor(OLED_W - 24, 2);
        oled.print("EDIT");
    }
    oled.setTextColor(SSD1306_WHITE);

    // Show 5 visible rows, scrolling with cursor
    int16_t rowH = 10;
    int16_t startY = 13;
    int8_t scrollTop = max(0, settingsCursor - 2);  // keep cursor in middle-ish
    if (scrollTop + 5 > NUM_SETTINGS) scrollTop = max(0, NUM_SETTINGS - 5);

    for (int8_t i = 0; i < 5 && scrollTop + i < NUM_SETTINGS; i++) {
        int8_t idx = scrollTop + i;
        int16_t ry = startY + i * rowH;
        bool sel = (idx == settingsCursor);
        char label[10], val[8];
        getSettingInfo(idx, label, val, sizeof(val));

        if (sel) {
            oled.fillRect(0, ry, OLED_W, rowH, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.setTextColor(SSD1306_WHITE);
        }

        oled.setCursor(2, ry + 1);
        oled.print(label);

        // Right-align value — blink when editing this row
        int16_t vw = strlen(val) * 6;
        bool blink = sel && settingsEditing && ((millis() / 300) % 2);
        if (!blink) {
            oled.setCursor(OLED_W - vw - 4, ry + 1);
            oled.print(val);
        }
        // Show arrows when editing selected row
        if (sel && settingsEditing) {
            oled.setCursor(OLED_W - vw - 10, ry + 1);
            oled.print("<");
            oled.setCursor(OLED_W - 4, ry + 1);
            oled.print(">");
        }

        oled.setTextColor(SSD1306_WHITE);
    }

    // Scroll bar
    if (NUM_SETTINGS > 5) {
        int16_t sbH = 50 * 5 / NUM_SETTINGS;
        int16_t sbY = startY + (50 - sbH) * scrollTop / (NUM_SETTINGS - 5);
        oled.drawFastVLine(OLED_W - 1, startY, 50, SSD1306_WHITE);
        oled.fillRect(OLED_W - 2, sbY, 2, sbH, SSD1306_WHITE);
    }

    drawPageDots(3, NUM_PAGES);
}

// ─── NVS persistence ────────────────────────────────────────────────────────

static void loadSettings() {
    prefs.begin("midi_guitar", true);  // read-only
    midiCh     = prefs.getUChar("midiCh",    MIDI_CH_DEFAULT);
    ccDist     = prefs.getUChar("ccDist",     CC_DIST_DEFAULT);
    ccVel      = prefs.getUChar("ccVel",      CC_VEL_DEFAULT);
    distMinMm  = prefs.getShort("distMin",    DIST_MIN_DEFAULT);
    distMaxMm  = prefs.getShort("distMax",    DIST_MAX_DEFAULT);
    curve      = prefs.getUChar("curve",      CURVE_DEFAULT);
    stripBright = prefs.getUChar("bright",    STRIP_BRIGHT_DEFAULT);
    distInvert = prefs.getBool("invert",      false);
    uint8_t tr = prefs.getUChar("transport", TR_USB);
    transport  = (tr < TR_COUNT) ? (Transport)tr : TR_USB;
    prefs.end();
}

static void saveSettings() {
    prefs.begin("midi_guitar", false);  // read-write
    prefs.putUChar("midiCh",   midiCh);
    prefs.putUChar("ccDist",   ccDist);
    prefs.putUChar("ccVel",    ccVel);
    prefs.putShort("distMin",  distMinMm);
    prefs.putShort("distMax",  distMaxMm);
    prefs.putUChar("curve",    curve);
    prefs.putUChar("bright",   stripBright);
    prefs.putBool("invert",    distInvert);
    prefs.putUChar("transport", transport);
    prefs.end();
    settingsDirty = false;
}

static void refreshOLED() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    switch (currentPage) {
        case PG_LIVE:      drawPageLive();      break;
        case PG_TRANSPORT: drawPageTransport(); break;
        case PG_MODE:      drawPageMode();      break;
        case PG_SETTINGS:  drawPageSettings();  break;
    }
    // Toast overlay — centered inverted badge on top of any page
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

// ─── Intro animation ─────────────────────────────────────────────────────────

static void playIntro() {
    oled.clearDisplay();
    oled.display();

    uint16_t hue = modeHue();

    // Phase 1: Line sweep + ring spinner
    for (int16_t x = 0; x < OLED_W; x += 4) {
        oled.drawLine(0, 31, x, 31, SSD1306_WHITE);
        oled.display();
        // Spin single LED around ring
        uint8_t p = (x / 4) % NUM_PIXELS;
        ring.clear();
        ring.setPixelColor(p, ring.ColorHSV(hue, 255, 120));
        ring.show();
    }
    delay(60);

    // Phase 2: Box expand + ring fills outward
    for (int16_t h = 0; h <= 28; h += 3) {
        oled.clearDisplay();
        oled.drawRoundRect(4, 32 - h, OLED_W - 8, h * 2, 4, SSD1306_WHITE);
        oled.display();
        // Fill ring proportional to expansion
        int n = h * NUM_PIXELS / 28;
        ring.clear();
        for (int i = 0; i < n; i++)
            ring.setPixelColor(i, ring.ColorHSV(hue, 255, 60 + i * 12));
        ring.show();
    }
    delay(80);

    // Phase 3: Typewriter + ring gentle pulse
    const char *title = "M I D I G U I T A R";
    uint8_t len = strlen(title);
    int16_t totalW = len * 6;
    int16_t startX = (OLED_W - totalW) / 2;

    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    for (uint8_t i = 0; i < len; i++) {
        oled.setCursor(startX + i * 6, 28);
        oled.print(title[i]);
        if (title[i] != ' ') {
            oled.display();
            delay(40);
            // Pulse ring brightness with each character
            uint8_t b = 40 + (i % 3) * 30;
            ring.fill(ring.ColorHSV(hue, 200, b));
            ring.show();
        }
    }

    delay(150);
    oled.setCursor((OLED_W - 12) / 2, 40);
    oled.print("v4");
    oled.display();

    // Ring holds steady during pause
    ring.fill(ring.ColorHSV(hue, 200, 80));
    ring.show();
    delay(400);

    // Phase 4: Fade out — OLED curtain close + ring fade
    for (int16_t i = 0; i < OLED_H / 2; i += 2) {
        oled.fillRect(0, i, OLED_W, 2, SSD1306_BLACK);
        oled.fillRect(0, OLED_H - i - 2, OLED_W, 2, SSD1306_BLACK);
        oled.display();
        // Fade ring to black
        uint8_t b = 80 - (80 * i / (OLED_H / 2));
        ring.fill(ring.ColorHSV(hue, 200, b));
        ring.show();
        delay(15);
    }

    oled.clearDisplay();
    oled.display();
    ring.clear();
    ring.show();
}

// ─── Shared actions ──────────────────────────────────────────────────────────

static void cycleTransport(int8_t dir) {
    transport = (Transport)wrap((int8_t)transport + dir, TR_COUNT);
    if (wantsBLE() && !bleInitDone) initBLE();
    triggerWipe();
    settingsDirty = true;
    dirtyTime = millis();
    const char *labels[] = { "USB", "BLE", "BOTH" };
    showToast(labels[transport]);
}

static void cycleMode(int8_t dir) {
    mode = (Mode)wrap((int8_t)mode + dir, MODE_COUNT);
    prevDist = -1;
    prevBend = -1;
    liveDist = 0;
    triggerWipe();
    showToast(mode == MODE_CC ? "CC" : "BEND");
}

static void toggleBypass() {
    bypassed = !bypassed;
    showToast(bypassed ? "MUTED" : "ACTIVE");
}

// ─── Encoder + menu ──────────────────────────────────────────────────────────

static void handleEncoder() {
    noInterrupts();
    int8_t delta = encDelta;
    encDelta = 0;
    interrupts();

    // ── Button state machine: short-press = page, long-press = bypass ──
    bool btnNow = digitalRead(ENC_PUSH) == LOW;
    bool shortPress = false;

    if (btnNow && !encPushPrev) {
        // Press-down edge
        encPushDown = millis();
        encHeld = true;
    }
    if (!btnNow && encPushPrev && encHeld) {
        // Release edge
        unsigned long held = millis() - encPushDown;
        encHeld = false;
        if (held < 500 && millis() - encPushTime > DEBOUNCE_MS) {
            shortPress = true;
            encPushTime = millis();
        }
    }
    // Long-press: toggle bypass while still held
    if (encHeld && (millis() - encPushDown >= 500)) {
        toggleBypass();
        encHeld = false;   // consume the gesture
        oledDirty = true;
#if DEBUG
        Serial.printf("Bypass -> %d (long-press)\n", bypassed);
#endif
    }
    encPushPrev = btnNow;

    if (delta == 0 && !shortPress) return;
    oledDirty = true;

    if (shortPress) {
        currentPage = (Page)wrap((int8_t)currentPage + 1, NUM_PAGES);
#if DEBUG
        Serial.printf("Page -> %d\n", currentPage);
#endif
        return;
    }

    // ── Rotate = change setting on current page ──
    if (delta != 0) {
        switch (currentPage) {
            case PG_LIVE: {
                // Brightness control
                stripBright = constrain((int16_t)stripBright + delta * 5, 5, 255);
                ring.setBrightness(stripBright);
            } break;

            case PG_TRANSPORT: cycleTransport(delta > 0 ? 1 : -1); break;
            case PG_MODE:      cycleMode(delta > 0 ? 1 : -1);      break;

            case PG_SETTINGS: {
                if (settingsEditing) {
                    // Edit mode: rotate changes value
                    adjustSetting(settingsCursor, delta > 0 ? 1 : -1);
                } else {
                    // Navigate mode: rotate moves cursor
                    settingsCursor = constrain(settingsCursor + (delta > 0 ? 1 : -1), 0, NUM_SETTINGS - 1);
                }
            } break;

            default: break;
        }
    }
}

// ─── Button (SW1) — context-sensitive action per page ───────────────────────

static void readEditBtn() {
    bool btnNow = digitalRead(BTN_EDIT) == LOW;
    if (btnNow && !editBtnPrev && millis() - editBtnTime > DEBOUNCE_MS) {
        editBtnTime = millis();
        switch (currentPage) {
            case PG_LIVE:      toggleBypass();       break;
            case PG_TRANSPORT: cycleTransport(1);    break;
            case PG_MODE:      cycleMode(1);         break;
            case PG_SETTINGS:  settingsEditing = !settingsEditing; break;
        }
        oledDirty = true;
    }
    editBtnPrev = btnNow;
    // Auto-exit edit mode when leaving settings page
    if (currentPage != PG_SETTINGS && settingsEditing) {
        settingsEditing = false;
    }
}

// ─── Sensor poll ─────────────────────────────────────────────────────────────

static void pollSensor() {
    if (!sensorDataReady) return;
    sensorDataReady = false;
    int16_t dist = -1;
    if (sensor.getRangeStatus() == 0) dist = sensor.getDistance();
    sensor.clearInterrupt();
    if (dist < 0) return;

    prevEma = ema;
    updateEma(ema, (float)dist, EMA_ALPHA);
    float curved = normCurve(ema, distMinMm, distMaxMm);
    if (distInvert) curved = 1.0f - curved;

    if (mode == MODE_CC) {
        int16_t cc = constrain((int16_t)lroundf(curved * 127.0f), 0, 127);
        liveDist = cc;
        if (cc != prevDist) { prevDist = cc; sendCC(ccDist, cc); }
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
    if (v != prevVel) { prevVel = v; sendCC(ccVel, v); }
}

// ─── Setup & loop ────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);

    // Load saved settings from NVS
    loadSettings();

    pinMode(BTN_EDIT, INPUT_PULLUP);
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
    ring.setBrightness(stripBright);
    ring.show();

    // Sensor init with retries — no hard halt
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        if (initSensor()) { sensorOK = true; break; }
        Serial.printf("Sensor init attempt %d failed\n", attempt + 1);
        delay(200);
    }
    if (!sensorOK) {
        Serial.println("ERR: VL53L1X — running in display-only mode");
        showToast("NO SENSOR");
    }

    // Watchdog — auto-reset on 5s hang
    const esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = 5000,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_init(&wdtCfg);
    esp_task_wdt_add(NULL);

    lastActivity = millis();

#if DEBUG
    Serial.printf("Ready v4 — %d %d CC%d+%d ch%d sensor=%d\n",
                  transport, mode, ccDist, ccVel, midiCh, sensorOK);
#endif
    oledDirty = true;
}

void loop() {
    esp_task_wdt_reset();

    // Snapshot encoder delta before handlers consume it (for screensaver wake)
    bool encMoved = (encDelta != 0);

    handleEncoder();
    readEditBtn();

    // Process sensor data as soon as interrupt fires (no polling delay)
    if (sensorOK) pollSensor();

    // Ring + OLED refresh at ~50fps
    if (millis() - tPoll >= POLL_MS) {
        tPoll = millis();
        updateRing();
        if (currentPage == PG_LIVE && sensorOK) oledDirty = true;
        if (currentPage == PG_SETTINGS && settingsEditing) oledDirty = true;
    }

    // Wake from screensaver on any encoder/button/sensor activity
    if (encMoved || !encPushPrev || liveVel > 5) {
        lastActivity = millis();
        if (oledAsleep) { oledAsleep = false; oledDirty = true; }
    }

    // Debounced NVS save — 2s after last setting change
    if (settingsDirty && millis() - dirtyTime > 2000) {
        saveSettings();
#if DEBUG
        Serial.println("Settings saved to NVS");
#endif
    }

    // Keep refreshing while toast is visible
    if (toastText && millis() < toastUntil) oledDirty = true;

    // Screensaver — blank OLED after 60s inactivity
    if (!oledAsleep && millis() - lastActivity > 60000) {
        oledAsleep = true;
        oled.clearDisplay();
        oled.display();
    }

    if (!oledAsleep && oledDirty && millis() - tOled >= 66) {
        tOled = millis();
        refreshOLED();
    }
}

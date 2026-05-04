// tof_midi_v5.ino — ToF MIDI controller with WiFi config mode
//
// VL53L1X — long-range hand gestures (10–500mm)
// MIDI: CC20 (distance), CC21 (velocity), Pitch Bend (14-bit)
// Config: triple-tap SW1 to open WiFi AP at 192.168.4.1
//
// Board: XIAO_ESP32S3, USB: USB-OTG (TinyUSB)
// Libs: SparkFun VL53L1X, Adafruit NeoPixel, Adafruit SSD1306, Adafruit GFX

// ─── Config ──────────────────────────────────────────────────────────────────

#define DEBUG           1

// Pin map — XIAO ESP32S3 socket M1
#define I2C_SDA         43
#define I2C_SCL         6
#define I2C_FREQ        400000

#define BLE_NAME        "MidiGuitar"
#define VL53_ADDR       0x30
#define XSHUT_VL53      5
#define VL53_INT        9
#define POLL_MS         20
#define EMA_ALPHA       0.2f
#define VEL_DEAD_MM     2.0f
#define VEL_MAX_MM      30.0f
#define CURVE_LINEAR    0
#define CURVE_LOG       1
#define CURVE_EXP       2
#define STRIP_PIN       1
#define BTN_EDIT        7
#define ENC_A           3
#define ENC_B           2
#define ENC_PUSH        44
#define NUM_PIXELS      16
#define OLED_W          128
#define OLED_H          64
#define OLED_ADDR       0x3C
#define NUM_PAGES       4
#define DEBOUNCE_MS     180

// WiFi config mode
#define AP_SSID         "MidiGuitar-Config"
#define AP_PASS         "12345678"
#define CFG_TAP_WINDOW  600
#define CFG_TAP_COUNT   3

// Defaults
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
#include <WiFi.h>
#include <WebServer.h>

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
    void onConnect(BLEServer *)    override { bleConnected = true; bleFlashUntil = millis() + 200; }
    void onDisconnect(BLEServer *) override { bleConnected = false; BLEDevice::startAdvertising(); }
};
static MidiCB midiCB;

// ─── Globals ─────────────────────────────────────────────────────────────────

static USBMIDI           usbMidi;
static SFEVL53L1X        sensor;
static Adafruit_NeoPixel ring(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_SSD1306  oled(OLED_W, OLED_H, &Wire, -1);
static BLECharacteristic *bleChr = nullptr;
static Preferences       prefs;

// WiFi config mode state
static WebServer        webServer(80);
static bool             configMode  = false;
static TaskHandle_t     webTaskH    = nullptr;
static uint8_t          tapCount    = 0;
static unsigned long    firstTapTime = 0;

// Settings page state
#define NUM_SETTINGS    8
static int8_t  settingsCursor  = 0;
static bool    settingsEditing = false;
static bool    settingsDirty   = false;
static unsigned long dirtyTime = 0;
static unsigned long editBtnTime = 0;
static bool    editBtnPrev     = true;

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
static unsigned long lastActivity = 0;
static bool          oledAsleep   = false;

// ─── Encoder ─────────────────────────────────────────────────────────────────

static volatile int8_t encDelta = 0;
static unsigned long   encPushTime  = 0;
static unsigned long   encPushDown  = 0;
static bool            encPushPrev  = true;
static bool            encHeld      = false;

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
    digitalWrite(XSHUT_VL53, LOW);  delay(10);
    digitalWrite(XSHUT_VL53, HIGH); delay(10);
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
    pinMode(VL53_INT, INPUT_PULLUP);
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

// ─── WiFi config mode ───────────────────────────────────────────────────────

static void handleRoot() {
    char buf[1500];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>MIDI Guitar</title><style>"
        "body{font-family:sans-serif;background:#111;color:#eee;padding:20px;max-width:400px;margin:auto}"
        "h1{color:#0fa}label{display:block;margin:14px 0 4px;font-size:13px;opacity:.7;letter-spacing:.1em}"
        "input,select{width:100%%;padding:10px;font-size:16px;background:#222;color:#eee;"
        "border:1px solid #444;border-radius:4px;box-sizing:border-box}"
        "button{margin-top:24px;padding:14px;width:100%%;font-size:18px;background:#0a6;"
        "color:#fff;border:0;border-radius:4px;cursor:pointer}"
        ".live{margin-top:30px;padding:16px;background:#1a1a1a;border-radius:6px;font-family:monospace}"
        "</style></head><body><h1>MIDI Guitar</h1>"
        "<form action='/save'>"
        "<label>MIDI CHANNEL</label><input name=ch type=number min=1 max=16 value=%d>"
        "<label>CC DISTANCE</label><input name=ccd type=number min=0 max=127 value=%d>"
        "<label>CC VELOCITY</label><input name=ccv type=number min=0 max=127 value=%d>"
        "<label>DISTANCE MIN (mm)</label><input name=dmin type=number value=%d>"
        "<label>DISTANCE MAX (mm)</label><input name=dmax type=number value=%d>"
        "<label>CURVE</label><select name=cv>"
        "<option value=0 %s>Linear</option><option value=1 %s>Log</option><option value=2 %s>Exp</option>"
        "</select><button type=submit>SAVE</button></form>"
        "<div class=live>Live: <span id=d>--</span> mm</div>"
        "<script>setInterval(async()=>{"
        "try{document.getElementById('d').textContent=await(await fetch('/live')).text();}catch(e){}"
        "},200);</script></body></html>",
        midiCh, ccDist, ccVel, distMinMm, distMaxMm,
        curve == 0 ? "selected" : "", curve == 1 ? "selected" : "", curve == 2 ? "selected" : "");
    webServer.send(200, "text/html", buf);
}

static void handleSave() {
    if (webServer.hasArg("ch"))   midiCh    = constrain(webServer.arg("ch").toInt(),   1, 16);
    if (webServer.hasArg("ccd"))  ccDist    = constrain(webServer.arg("ccd").toInt(),  0, 127);
    if (webServer.hasArg("ccv"))  ccVel     = constrain(webServer.arg("ccv").toInt(),  0, 127);
    if (webServer.hasArg("dmin")) distMinMm = constrain(webServer.arg("dmin").toInt(), 5, 500);
    if (webServer.hasArg("dmax")) distMaxMm = constrain(webServer.arg("dmax").toInt(), 50, 2000);
    if (webServer.hasArg("cv"))   curve     = constrain(webServer.arg("cv").toInt(),   0, 2);
    settingsDirty = true;
    dirtyTime = millis();
    oledDirty = true;
    webServer.sendHeader("Location", "/");
    webServer.send(303);
}

static void handleLive() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)(ema >= 0 ? ema : 0));
    webServer.send(200, "text/plain", buf);
}

static void webTask(void*) {
    esp_task_wdt_add(nullptr);
    while (configMode) {
        webServer.handleClient();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
}

static void enterConfigMode() {
    if (configMode) return;
    configMode = true;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // save heap
    delay(100);
    IPAddress ip = WiFi.softAPIP();

    webServer.on("/",     handleRoot);
    webServer.on("/save", handleSave);
    webServer.on("/live", handleLive);
    webServer.begin();

    xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, &webTaskH, 0);

    static char ipMsg[24];
    snprintf(ipMsg, sizeof(ipMsg), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    showToast(ipMsg);
#if DEBUG
    Serial.printf("Config mode: http://%s\n", ipMsg);
    Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
#endif
}

static void exitConfigMode() {
    if (!configMode) return;
    configMode = false;
    delay(50);
    webServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    showToast("CFG OFF");
#if DEBUG
    Serial.println("Config mode off");
#endif
}

static void checkConfigGesture() {
    static bool prev = true;
    bool now = digitalRead(BTN_EDIT) == LOW;
    if (now && !prev) {
        unsigned long t = millis();
        if (t - firstTapTime > CFG_TAP_WINDOW) {
            tapCount = 1;
            firstTapTime = t;
        } else {
            tapCount++;
        }
        if (tapCount >= CFG_TAP_COUNT) {
            tapCount = 0;
            configMode ? exitConfigMode() : enterConfigMode();
        }
    }
    prev = now;
}

// ─── NeoPixel ────────────────────────────────────────────────────────────────

static uint16_t modeHue() {
    if (configMode) return 21845;  // green during config
    if (wantsUSB() && !wantsBLE()) return mode == MODE_CC ? 21845 : 49152;
    if (wantsBLE() && !wantsUSB()) return mode == MODE_CC ? 38229 : 32768;
    return mode == MODE_CC ? 28000 : 45000;
}

static void triggerWipe() {
    wipeColor = ring.ColorHSV(modeHue(), 255, 200);
    wipeUntil = millis() + 150;
}

static void showRing(int16_t val) {
    int n = constrain((val * NUM_PIXELS + 63) / 127, 0, NUM_PIXELS);
    uint16_t hue = modeHue();
    ring.clear();
    for (int i = 0; i < n; i++) {
        uint8_t bright = (n > 1) ? map(i, 0, n - 1, 76, 255) : 255;
        ring.setPixelColor(i, ring.ColorHSV(hue, 255, bright));
    }
    if (liveVel > 100 && n > 0) ring.setPixelColor(n - 1, ring.Color(255, 255, 255));
    ring.show();
}

static void showBreathe() {
    float phase = sinf(millis() * TWO_PI / 2000.0f);
    uint8_t b = 10 + (uint8_t)(30.0f * (1.0f + phase) / 2.0f);
    uint32_t col = ring.Color(b, b, 0);
    for (int i = 0; i < NUM_PIXELS; i++) ring.setPixelColor(i, col);
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

// New: rainbow pulse during config mode
static void showConfig() {
    uint16_t hue = (millis() / 4) & 0xFFFF;
    float phase = sinf(millis() * TWO_PI / 1500.0f);
    uint8_t b = 30 + (uint8_t)(50.0f * (1.0f + phase) / 2.0f);
    for (int i = 0; i < NUM_PIXELS; i++) {
        uint16_t h = hue + (i * 65536 / NUM_PIXELS);
        ring.setPixelColor(i, ring.ColorHSV(h, 255, b));
    }
    ring.show();
}

static void updateRing() {
    ring.setBrightness(stripBright);
    if (millis() < bleFlashUntil) {
        ring.fill(ring.Color(0, 60, 255)); ring.show(); return;
    }
    if (millis() < wipeUntil) {
        unsigned long elapsed = 150 - (wipeUntil - millis());
        int n = (int)(elapsed * NUM_PIXELS / 150);
        ring.clear();
        for (int i = 0; i < n && i < NUM_PIXELS; i++) ring.setPixelColor(i, wipeColor);
        ring.show();
        return;
    }
    if (configMode)            { showConfig();  return; }
    if (wantsBLE() && !bleConnected && !wantsUSB()) { showChase(); return; }
    if (bypassed)              { showBreathe(); return; }
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

// New: dedicated OLED page for config mode
static void drawPageConfig() {
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("CONFIG MODE");
    oled.setTextColor(SSD1306_WHITE);

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

    oled.setCursor(0, 54);
    oled.print("Triple-tap to exit");
}

static void drawPageLive() {
    int16_t bx = 0;
    const char *trLabel = wantsUSB() && wantsBLE() ? "U+B" : wantsUSB() ? "USB" : "BLE";
    bx += drawBadge(0, 0, trLabel) + 2;
    bx += drawBadge(bx, 0, mode == MODE_CC ? "CC" : "PB") + 2;
    oled.setCursor(bx, 1);
    oled.printf("Ch%d", midiCh);

    if (wantsBLE()) {
        if (bleConnected) oled.fillCircle(82, 4, 2, SSD1306_WHITE);
        else if ((millis() / 300) % 2) oled.drawCircle(82, 4, 2, SSD1306_WHITE);
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

static void drawPageTransport() {
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("OUTPUT");
    oled.setTextColor(SSD1306_WHITE);

    const char *labels[] = { "USB", "BLE", "BOTH" };
    int16_t slotW = 40, margin = (OLED_W - slotW * 3 - 4) / 2, slotY = 14, slotH = 40;

    for (uint8_t i = 0; i < TR_COUNT; i++) {
        int16_t sx = margin + i * (slotW + 2);
        bool sel = ((uint8_t)transport == i);
        if (sel) { oled.fillRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
        else     { oled.drawRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE); oled.setTextColor(SSD1306_WHITE); }

        int16_t lw = strlen(labels[i]) * 6;
        oled.setCursor(sx + (slotW - lw) / 2, slotY + slotH - 10);
        oled.print(labels[i]);
        oled.setTextColor(SSD1306_WHITE);
    }
    drawPageDots(1, NUM_PAGES);
}

static void drawPageMode() {
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("MODE");
    oled.setTextColor(SSD1306_WHITE);

    const char *labels[] = { "CC", "BEND" };
    int16_t slotW = 58, gap = OLED_W - slotW * 2, slotY = 14, slotH = 40;

    for (uint8_t i = 0; i < MODE_COUNT; i++) {
        int16_t sx = gap / 2 + i * (slotW + 2);
        bool sel = ((uint8_t)mode == i);
        if (sel) { oled.fillRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
        else     { oled.drawRoundRect(sx, slotY, slotW, slotH, 3, SSD1306_WHITE); oled.setTextColor(SSD1306_WHITE); }

        int16_t lw = strlen(labels[i]) * 6;
        oled.setCursor(sx + (slotW - lw) / 2, slotY + slotH - 10);
        oled.print(labels[i]);
        oled.setTextColor(SSD1306_WHITE);
    }
    drawPageDots(2, NUM_PAGES);
}

static void getSettingInfo(uint8_t idx, char *label, char *val, size_t vLen) {
    switch (idx) {
        case 0: strcpy(label, "MIDI Ch");  snprintf(val, vLen, "%d", midiCh);    break;
        case 1: strcpy(label, "CC Dist");  snprintf(val, vLen, "%d", ccDist);    break;
        case 2: strcpy(label, "CC Vel");   snprintf(val, vLen, "%d", ccVel);     break;
        case 3: strcpy(label, "Dist Min"); snprintf(val, vLen, "%d", distMinMm); break;
        case 4: strcpy(label, "Dist Max"); snprintf(val, vLen, "%d", distMaxMm); break;
        case 5: strcpy(label, "Curve");
            snprintf(val, vLen, "%s", curve == CURVE_LOG ? "LOG" : curve == CURVE_EXP ? "EXP" : "LIN");
            break;
        case 6: strcpy(label, "LED Brt");  snprintf(val, vLen, "%d", stripBright); break;
        case 7: strcpy(label, "Invert");   snprintf(val, vLen, "%s", distInvert ? "ON" : "OFF"); break;
    }
}

static void adjustSetting(uint8_t idx, int8_t dir) {
    switch (idx) {
        case 0: midiCh    = constrain(midiCh + dir, 1, 16); break;
        case 1: ccDist    = constrain(ccDist + dir, 0, 127); break;
        case 2: ccVel     = constrain(ccVel + dir, 0, 127); break;
        case 3: distMinMm = constrain(distMinMm + dir * 5, 5, 200); break;
        case 4: distMaxMm = constrain(distMaxMm + dir * 10, 100, 1000); break;
        case 5: curve     = wrap(curve + dir, 3); break;
        case 6: stripBright = constrain((int16_t)stripBright + dir * 5, 5, 255);
                ring.setBrightness(stripBright); break;
        case 7: distInvert = !distInvert; break;
    }
    settingsDirty = true;
    dirtyTime = millis();
}

static void drawPageSettings() {
    oled.fillRect(0, 0, OLED_W, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2);
    oled.print("SETTINGS");
    if (settingsEditing) { oled.setCursor(OLED_W - 24, 2); oled.print("EDIT"); }
    oled.setTextColor(SSD1306_WHITE);

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
    drawPageDots(3, NUM_PAGES);
}

// ─── NVS ─────────────────────────────────────────────────────────────────────

static void loadSettings() {
    prefs.begin("midi_guitar", true);
    midiCh      = prefs.getUChar("midiCh",   MIDI_CH_DEFAULT);
    ccDist      = prefs.getUChar("ccDist",   CC_DIST_DEFAULT);
    ccVel       = prefs.getUChar("ccVel",    CC_VEL_DEFAULT);
    distMinMm   = prefs.getShort("distMin",  DIST_MIN_DEFAULT);
    distMaxMm   = prefs.getShort("distMax",  DIST_MAX_DEFAULT);
    curve       = prefs.getUChar("curve",    CURVE_DEFAULT);
    stripBright = prefs.getUChar("bright",   STRIP_BRIGHT_DEFAULT);
    distInvert  = prefs.getBool("invert",    false);
    uint8_t tr  = prefs.getUChar("transport", TR_USB);
    transport   = (tr < TR_COUNT) ? (Transport)tr : TR_USB;
    prefs.end();
}

static void saveSettings() {
    prefs.begin("midi_guitar", false);
    prefs.putUChar("midiCh",    midiCh);
    prefs.putUChar("ccDist",    ccDist);
    prefs.putUChar("ccVel",     ccVel);
    prefs.putShort("distMin",   distMinMm);
    prefs.putShort("distMax",   distMaxMm);
    prefs.putUChar("curve",     curve);
    prefs.putUChar("bright",    stripBright);
    prefs.putBool("invert",     distInvert);
    prefs.putUChar("transport", transport);
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
            case PG_LIVE:      drawPageLive();      break;
            case PG_TRANSPORT: drawPageTransport(); break;
            case PG_MODE:      drawPageMode();      break;
            case PG_SETTINGS:  drawPageSettings();  break;
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

// ─── Actions ─────────────────────────────────────────────────────────────────

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
    prevDist = -1; prevBend = -1; liveDist = 0;
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

    bool btnNow = digitalRead(ENC_PUSH) == LOW;
    bool shortPress = false;
    if (btnNow && !encPushPrev) { encPushDown = millis(); encHeld = true; }
    if (!btnNow && encPushPrev && encHeld) {
        unsigned long held = millis() - encPushDown;
        encHeld = false;
        if (held < 500 && millis() - encPushTime > DEBOUNCE_MS) {
            shortPress = true;
            encPushTime = millis();
        }
    }
    if (encHeld && (millis() - encPushDown >= 500)) {
        toggleBypass();
        encHeld = false;
        oledDirty = true;
    }
    encPushPrev = btnNow;

    if (delta == 0 && !shortPress) return;
    oledDirty = true;

    if (configMode) return;  // encoder disabled in config mode

    if (shortPress) {
        currentPage = (Page)wrap((int8_t)currentPage + 1, NUM_PAGES);
        return;
    }
    if (delta != 0) {
        switch (currentPage) {
            case PG_LIVE:
                stripBright = constrain((int16_t)stripBright + delta * 5, 5, 255);
                ring.setBrightness(stripBright);
                break;
            case PG_TRANSPORT: cycleTransport(delta > 0 ? 1 : -1); break;
            case PG_MODE:      cycleMode(delta > 0 ? 1 : -1);      break;
            case PG_SETTINGS:
                if (settingsEditing) adjustSetting(settingsCursor, delta > 0 ? 1 : -1);
                else settingsCursor = constrain(settingsCursor + (delta > 0 ? 1 : -1), 0, NUM_SETTINGS - 1);
                break;
            default: break;
        }
    }
}

static void readEditBtn() {
    bool btnNow = digitalRead(BTN_EDIT) == LOW;
    if (btnNow && !editBtnPrev && millis() - editBtnTime > DEBOUNCE_MS) {
        editBtnTime = millis();
        // Triple-tap detection runs first; if it fires, single-press action is ignored
        if (tapCount > 0 && tapCount < CFG_TAP_COUNT) {
            // mid-sequence — don't trigger single action
        } else if (!configMode) {
            switch (currentPage) {
                case PG_LIVE:      toggleBypass();       break;
                case PG_TRANSPORT: cycleTransport(1);    break;
                case PG_MODE:      cycleMode(1);         break;
                case PG_SETTINGS:  settingsEditing = !settingsEditing; break;
            }
            oledDirty = true;
        }
    }
    editBtnPrev = btnNow;
    if (currentPage != PG_SETTINGS && settingsEditing) settingsEditing = false;
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

// ─── Setup / loop ────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);

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

    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) playIntro();

    ring.begin();
    ring.setBrightness(stripBright);
    ring.show();

    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        if (initSensor()) { sensorOK = true; break; }
        delay(200);
    }
    if (!sensorOK) showToast("NO SENSOR");

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
}

void loop() {
    esp_task_wdt_reset();

    bool encMoved = (encDelta != 0);

    checkConfigGesture();
    handleEncoder();
    readEditBtn();

    if (sensorOK) pollSensor();

    if (millis() - tPoll >= POLL_MS) {
        tPoll = millis();
        updateRing();
        if (currentPage == PG_LIVE && sensorOK) oledDirty = true;
        if (currentPage == PG_SETTINGS && settingsEditing) oledDirty = true;
        if (configMode) oledDirty = true;
    }

    if (encMoved || !encPushPrev || liveVel > 5) {
        lastActivity = millis();
        if (oledAsleep) { oledAsleep = false; oledDirty = true; }
    }

    if (settingsDirty && millis() - dirtyTime > 2000) saveSettings();

    if (toastText && millis() < toastUntil) oledDirty = true;

    // Disable screensaver during config mode
    if (!configMode && !oledAsleep && millis() - lastActivity > 60000) {
        oledAsleep = true;
        oled.clearDisplay();
        oled.display();
    }

    if (!oledAsleep && oledDirty && millis() - tOled >= 66) {
        tOled = millis();
        refreshOLED();
    }
}

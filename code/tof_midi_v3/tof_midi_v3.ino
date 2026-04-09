// tof_midi_v3.ino — Dual ToF MIDI controller for electric guitar
//
// VL53L1X  (main cavity)  — long-range hand gestures (10–500mm)
// VL6180X  (small cavity) — close-range finger gestures (5–100mm)
//
// MIDI output:
//   CC20        — VL53L1X distance
//   CC21        — VL53L1X velocity
//   CC22        — VL6180X distance
//   Pitch Bend  — VL53L1X distance (14-bit, mode B)
//
// Hardware:
//   Seeed XIAO ESP32-S3
//   VL53L1X + VL6180X ToF sensors     (I2C)
//   16-LED WS2812B NeoPixel ring
//   SSD1306 128x64 OLED               (I2C)
//   Rotary encoder with push           (EC11-style)
//   SPDT slide switch, tactile button
//
//                         XIAO ESP32-S3
//                        ┌─────────────┐
//                   5V ──┤ 5V      GND ├── GND bus
//                  GND ──┤ GND     3V3 ├── 3V3 bus
//          I2C SDA    ──┤ D4      D10 ├── Encoder push
//          I2C SCL    ──┤ D5       D9 ├── Encoder B
//   VL53L1X XSHUT ──────┤ D6       D8 ├── Encoder A
//   VL6180X XSHUT ──────┤ D7            │
//     NeoPixel DIN ──────┤ D1            │
//     Transport SW ──────┤ D2            │
//     Bypass BTN ────────┤ D3            │
//                        └─────────────┘
//
// Board: XIAO_ESP32S3,  USB: USB-OTG (TinyUSB)
//
// Libraries: SparkFun VL53L1X, Adafruit VL6180X, Adafruit NeoPixel,
//            Adafruit SSD1306, Adafruit GFX

// ─── Configuration ───────────────────────────────────────────────────────────

#define DEBUG           1

#define I2C_SDA         5
#define I2C_SCL         6
#define I2C_FREQ        400000

#define MIDI_CH         1
#define MIDI_CH0        (MIDI_CH - 1)
#define CC_DIST         20
#define CC_VEL          21
#define CC_CLOSE        22
#define BLE_NAME        "MidiGuitar"

#define DIST_MIN_MM     10
#define DIST_MAX_MM     500
#define VL53_ADDR       0x30
#define XSHUT_VL53      43
#define XSHUT_VL6       44

#define CLOSE_MIN_MM    5
#define CLOSE_MAX_MM    100

#define POLL_MS         20
#define EMA_ALPHA       0.2f
#define EMA_ALPHA_CLOSE 0.3f

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

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDR       0x3C

#define NUM_PAGES       3
#define DEBOUNCE_MS     200

// ─── Includes ────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <SparkFun_VL53L1X.h>
#include <Adafruit_VL6180X.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ─── Types ───────────────────────────────────────────────────────────────────

enum Mode      : uint8_t { MODE_CC, MODE_PITCHBEND, MODE_COUNT };
enum Transport : uint8_t { TRANSPORT_USB, TRANSPORT_BLE, TRANSPORT_BOTH, TRANSPORT_COUNT };
enum Page      : uint8_t { PAGE_LIVE, PAGE_TRANSPORT, PAGE_MODE };

static const char *transportNames[] = { "USB", "BLE", "BOTH" };
static const char *modeNames[]      = { "CC", "PitchBend" };

// ─── BLE MIDI ────────────────────────────────────────────────────────────────

#define BLE_MIDI_SVC   "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLE_MIDI_CHR   "7772E5DB-3868-4112-A1A9-F2669D106BF3"

static bool bleConnected = false;
static bool bleInitDone  = false;

class MidiServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer *)    override { bleConnected = true;  }
    void onDisconnect(BLEServer *) override {
        bleConnected = false;
        BLEDevice::startAdvertising();
    }
};

static MidiServerCB midiServerCB;

// ─── Globals ─────────────────────────────────────────────────────────────────

static USBMIDI             usbMidi;
static SFEVL53L1X          sensorLong;
static Adafruit_VL6180X    sensorClose;
static bool                sensorCloseOk = false;
static Adafruit_NeoPixel   ring(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_SSD1306    oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
static BLECharacteristic  *bleChr = nullptr;

static Transport transport = TRANSPORT_USB;
static Mode      mode      = MODE_CC;
static bool      bypassed  = false;

// VL53L1X state
static float   ema        = -1.0f;
static float   prevEma    = -1.0f;
static int16_t prevDistCC = -1;
static int16_t prevVelCC  = -1;
static int16_t prevBend   = -1;

// VL6180X state
static float   emaClose    = -1.0f;
static int16_t prevCloseCC = -1;

// Live values (drive ring + OLED)
static int16_t liveDistCC  = 0;
static int16_t liveVelCC   = 0;
static int16_t liveCloseCC = 0;
static int16_t liveBend    = 0;

// Timing
static unsigned long tPoll = 0;
static unsigned long tOled = 0;

// ─── Encoder ─────────────────────────────────────────────────────────────────

static volatile int8_t encDelta = 0;
static unsigned long   encPushTime = 0;

// EC11 encoders produce 4 Gray code transitions per detent.
// Accumulate raw transitions and only emit a step every 4.
static void IRAM_ATTR encISR() {
    static uint8_t prevState = 0;
    static int8_t  rawCount  = 0;
    uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    uint8_t combined = (prevState << 2) | state;
    switch (combined) {
        case 0b0001: case 0b0111: case 0b1110: case 0b1000:
            rawCount++; break;
        case 0b0010: case 0b1011: case 0b1101: case 0b0100:
            rawCount--; break;
    }
    prevState = state;
    if (rawCount >= 4)  { encDelta++;  rawCount -= 4; }
    if (rawCount <= -4) { encDelta--;  rawCount += 4; }
}

// ─── Menu state ──────────────────────────────────────────────────────────────

static Page     currentPage = PAGE_LIVE;
static bool     editing     = false;
static uint8_t  editCursor  = 0;
static bool     oledDirty   = true;

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

static void updateEma(float &val, float sample, float alpha) {
    if (val < 0.0f) val = sample;
    else            val = alpha * sample + (1.0f - alpha) * val;
}

static float normAndCurve(float val, float lo, float hi) {
    float norm = (val - lo) / (hi - lo);
    return applyCurve(constrain(norm, 0.0f, 1.0f));
}

static uint8_t wrapIndex(int8_t val, uint8_t count) {
    if (val < 0) return count - 1;
    if (val >= count) return 0;
    return (uint8_t)val;
}

static bool usesUSB() { return transport == TRANSPORT_USB || transport == TRANSPORT_BOTH; }
static bool usesBLE() { return transport == TRANSPORT_BLE || transport == TRANSPORT_BOTH; }

// ─── BLE MIDI send ──────────────────────────────────────────────────────────

static void bleSend(uint8_t status, uint8_t d1, uint8_t d2) {
    if (!bleConnected || !bleChr) return;
    uint16_t ts  = millis() & 0x1FFF;
    uint8_t  pkt[5] = {
        (uint8_t)(0x80 | (ts >> 7)),
        (uint8_t)(0x80 | (ts & 0x7F)),
        status, d1, d2
    };
    bleChr->setValue(pkt, 5);
    bleChr->notify();
}

// ─── MIDI output ────────────────────────────────────────────────────────────

static void sendCC(uint8_t cc, uint8_t val) {
    if (bypassed) return;
    if (usesUSB()) usbMidi.controlChange(cc, val, MIDI_CH);
    if (usesBLE()) bleSend(0xB0 | (MIDI_CH0 & 0x0F), cc, val);
}

static void sendPitchBend(uint16_t bend) {
    if (bypassed) return;
    if (usesUSB()) usbMidi.pitchBend(bend, MIDI_CH);
    if (usesBLE()) bleSend(0xE0 | (MIDI_CH0 & 0x0F), bend & 0x7F, (bend >> 7) & 0x7F);
}

// ─── Sensor init ─────────────────────────────────────────────────────────────

static bool initSensors() {
    pinMode(XSHUT_VL53, OUTPUT);
    digitalWrite(XSHUT_VL53, LOW);
    pinMode(XSHUT_VL6, OUTPUT);
    digitalWrite(XSHUT_VL6, LOW);
    delay(10);

    // VL6180X first — shares default 0x29 with VL53
    digitalWrite(XSHUT_VL6, HIGH);
    delay(10);
    if (sensorClose.begin()) {
        sensorCloseOk = true;
#if DEBUG
        Serial.println("VL6180X OK at 0x29");
#endif
    } else {
#if DEBUG
        Serial.println("VL6180X not found");
#endif
    }

    // VL53L1X — reassign to 0x30
    digitalWrite(XSHUT_VL53, HIGH);
    delay(10);
    sensorLong.begin(Wire);
    sensorLong.setI2CAddress(VL53_ADDR);
    sensorLong.setTimingBudgetInMs(POLL_MS);
    sensorLong.setIntermeasurementPeriod(POLL_MS);
    sensorLong.startRanging();

    unsigned long t0 = millis();
    while (!sensorLong.checkForDataReady()) {
        if (millis() - t0 > 200) return false;
        delay(5);
    }
    sensorLong.clearInterrupt();
#if DEBUG
    Serial.printf("VL53L1X OK at 0x%02X\n", VL53_ADDR);
#endif
    return true;
}

// ─── BLE init ────────────────────────────────────────────────────────────────

static void initBLE() {
    if (bleInitDone) return;
    BLEDevice::init(BLE_NAME);
    BLEServer *srv = BLEDevice::createServer();
    srv->setCallbacks(&midiServerCB);

    BLEService *svc = srv->createService(BLE_MIDI_SVC);
    bleChr = svc->createCharacteristic(
        BLE_MIDI_CHR,
        BLECharacteristic::PROPERTY_READ   |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    bleChr->addDescriptor(new BLE2902());
    svc->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_MIDI_SVC);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->start();
    bleInitDone = true;
}

// ─── NeoPixel ring ───────────────────────────────────────────────────────────

static uint32_t modeColor() {
    if (bypassed) return ring.Color(40, 40, 0);

    if (usesUSB() && !usesBLE())
        return mode == MODE_CC ? ring.Color(0, 255, 0)   : ring.Color(180, 0, 255);
    if (usesBLE() && !usesUSB())
        return mode == MODE_CC ? ring.Color(0, 80, 255)  : ring.Color(0, 220, 220);
    return mode == MODE_CC     ? ring.Color(0, 200, 100) : ring.Color(100, 0, 200);
}

static void showRing(int16_t val) {
    static int16_t lastShown = -1;
    static bool    lastBypassed = false;
    if (val == lastShown && bypassed == lastBypassed) return;
    lastShown = val;
    lastBypassed = bypassed;

    int numLit = (val * NUM_PIXELS + 63) / 127;
    numLit = constrain(numLit, 0, NUM_PIXELS);
    uint32_t col = modeColor();

    ring.clear();
    for (int i = 0; i < numLit; i++)
        ring.setPixelColor(i, col);
    ring.show();
}

static void showChase() {
    static uint8_t pos = 0;
    ring.clear();
    ring.setPixelColor(pos % NUM_PIXELS, ring.Color(0, 40, 200));
    ring.setPixelColor((pos + 1) % NUM_PIXELS, ring.Color(0, 15, 80));
    ring.show();
    pos++;
}

static void showError() {
    ring.fill((millis() / 100) % 2 ? ring.Color(255, 0, 0) : 0);
    ring.show();
}

static void updateRing() {
    if (usesBLE() && !bleConnected && !usesUSB()) {
        showChase();
        return;
    }

    // Only touch brightness when bypass state changes
    static bool lastBypass = false;
    if (bypassed != lastBypass) {
        ring.setBrightness(bypassed ? STRIP_BRIGHT / 2 : STRIP_BRIGHT);
        lastBypass = bypassed;
    }

    showRing(liveDistCC);
}

// ─── OLED display ────────────────────────────────────────────────────────────

static void drawBar(int16_t x, int16_t y, int16_t w, int16_t val, int16_t maxVal) {
    int16_t filled = constrain((int32_t)val * w / maxVal, 0, w);
    oled.drawRect(x, y, w, 8, SSD1306_WHITE);
    oled.fillRect(x, y, filled, 8, SSD1306_WHITE);
}

static void drawPageLive() {
    // Status line: transport, mode, mute
    oled.setCursor(0, 0);
    oled.print(transportNames[transport]);
    oled.print(" ");
    oled.print(modeNames[mode]);
    oled.print("  Ch");
    oled.print(MIDI_CH);
    if (usesBLE()) {
        oled.setCursor(80, 0);
        oled.print(bleConnected ? "BLE:ON" : "BLE:..");
    }
    if (bypassed) {
        oled.setCursor(OLED_WIDTH - 24, 0);
        oled.print("MUTE");
    }

    // CC bars — full width, more vertical space
    oled.setCursor(0, 14);
    oled.printf("CC%d:", CC_DIST);
    drawBar(30, 14, 82, liveDistCC, 127);
    oled.setCursor(115, 14);
    oled.printf("%3d", liveDistCC);

    oled.setCursor(0, 27);
    oled.printf("CC%d:", CC_VEL);
    drawBar(30, 27, 82, liveVelCC, 127);
    oled.setCursor(115, 27);
    oled.printf("%3d", liveVelCC);

    oled.setCursor(0, 40);
    if (sensorCloseOk) {
        oled.printf("CC%d:", CC_CLOSE);
        drawBar(30, 40, 82, liveCloseCC, 127);
        oled.setCursor(115, 40);
        oled.printf("%3d", liveCloseCC);
    } else {
        oled.print("CC22: --");
    }

    if (mode == MODE_PITCHBEND) {
        oled.setCursor(0, 55);
        oled.printf("PB: %5d", liveBend);
    }
}

// Shared list-selection page renderer
static void drawSelectPage(const char *title, const char **names,
                            uint8_t count, uint8_t activeIdx) {
    oled.setCursor(0, 0);
    oled.print(title);

    for (uint8_t i = 0; i < count; i++) {
        oled.setCursor(8, 16 + i * 14);
        if (editing && editCursor == i)
            oled.print("> ");
        else if (!editing && activeIdx == i)
            oled.print("* ");
        else
            oled.print("  ");
        oled.print(names[i]);
        if (activeIdx == i) oled.print(" [active]");
    }

    oled.setCursor(0, 55);
    oled.print(editing ? "Turn=select Push=set" : "Push to edit");
}

static void refreshOLED() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    switch (currentPage) {
        case PAGE_LIVE:      drawPageLive(); break;
        case PAGE_TRANSPORT: drawSelectPage("Transport output:",
                                 transportNames, TRANSPORT_COUNT,
                                 (uint8_t)transport); break;
        case PAGE_MODE:      drawSelectPage("MIDI mode:",
                                 modeNames, MODE_COUNT,
                                 (uint8_t)mode); break;
    }

    oled.display();
    oledDirty = false;
}

// ─── Menu logic ──────────────────────────────────────────────────────────────

static void handleEncoder() {
    // Atomic read-then-clear
    noInterrupts();
    int8_t delta = encDelta;
    encDelta = 0;
    interrupts();

    bool pushed = false;
    if (digitalRead(ENC_PUSH) == LOW && millis() - encPushTime > DEBOUNCE_MS) {
        pushed = true;
        encPushTime = millis();
    }

    if (delta == 0 && !pushed) return;
    oledDirty = true;

    if (!editing) {
        if (delta != 0)
            currentPage = (Page)wrapIndex((int8_t)currentPage + (delta > 0 ? 1 : -1), NUM_PAGES);

        if (pushed && currentPage != PAGE_LIVE) {
            editing = true;
            editCursor = (currentPage == PAGE_TRANSPORT) ? (uint8_t)transport : (uint8_t)mode;
        }
    } else {
        if (delta != 0) {
            uint8_t maxItems = (currentPage == PAGE_TRANSPORT) ? TRANSPORT_COUNT : MODE_COUNT;
            editCursor = wrapIndex((int8_t)editCursor + (delta > 0 ? 1 : -1), maxItems);
        }

        if (pushed) {
            if (currentPage == PAGE_TRANSPORT) {
                Transport newT = (Transport)editCursor;
                if (newT != transport) {
                    transport = newT;
                    if (usesBLE() && !bleInitDone) initBLE();
#if DEBUG
                    Serial.printf("Transport -> %s\n", transportNames[transport]);
#endif
                }
            } else if (currentPage == PAGE_MODE) {
                Mode newM = (Mode)editCursor;
                if (newM != mode) {
                    mode = newM;
                    prevDistCC = -1;
                    prevBend   = -1;
                    liveDistCC = -1;
#if DEBUG
                    Serial.printf("Mode -> %s\n", modeNames[mode]);
#endif
                }
            }
            editing = false;
        }
    }
}

// ─── Bypass button ──────────────────────────────────────────────────────────

static unsigned long btnTime = 0;

static void readBypass() {
    if (digitalRead(BTN_BYPASS) == LOW && millis() - btnTime > DEBOUNCE_MS) {
        bypassed = !bypassed;
        btnTime = millis();
        oledDirty = true;
#if DEBUG
        Serial.printf("Bypass -> %s\n", bypassed ? "ON" : "OFF");
#endif
    }
}

// ─── Poll VL53L1X → CC20/CC21/PitchBend ─────────────────────────────────────

static void pollLong() {
    if (!sensorLong.checkForDataReady()) return;

    int16_t dist = -1;
    if (sensorLong.getRangeStatus() == 0)
        dist = sensorLong.getDistance();
    sensorLong.clearInterrupt();
    if (dist < 0) return;

    prevEma = ema;
    updateEma(ema, (float)dist, EMA_ALPHA);

    float curved = normAndCurve(ema, DIST_MIN_MM, DIST_MAX_MM);

    if (mode == MODE_CC) {
        int16_t cc = constrain((int16_t)lroundf(curved * 127.0f), 0, 127);
        liveDistCC = cc;
        if (cc != prevDistCC) {
            prevDistCC = cc;
            sendCC(CC_DIST, cc);
#if DEBUG
            Serial.printf("long=%d ema=%.1f CC%d=%d\n", dist, ema, CC_DIST, cc);
#endif
        }
    } else {
        int16_t bend = constrain((int16_t)lroundf(curved * 16383.0f), (int16_t)0, (int16_t)16383);
        liveBend = bend;
        liveDistCC = bend / (16383 / 127);
        if (bend != prevBend) {
            prevBend = bend;
            sendPitchBend(bend);
#if DEBUG
            Serial.printf("long=%d ema=%.1f bend=%d\n", dist, ema, bend);
#endif
        }
    }

    float delta = fabsf(ema - prevEma);
    if (delta < VEL_DEAD_MM) delta = 0.0f;
    int16_t velCC = (int16_t)lroundf(constrain(delta / VEL_MAX_MM * 127.0f, 0.0f, 127.0f));
    liveVelCC = velCC;
    if (velCC != prevVelCC) {
        prevVelCC = velCC;
        sendCC(CC_VEL, velCC);
#if DEBUG
        if (velCC > 0) Serial.printf("  vel=%.1f CC%d=%d\n", delta, CC_VEL, velCC);
#endif
    }
}

// ─── Poll VL6180X → CC22 ────────────────────────────────────────────────────

static void pollClose() {
    if (!sensorCloseOk) return;
    if (sensorClose.readRangeStatus() != VL6180X_ERROR_NONE) return;

    int16_t dist = sensorClose.readRange();
    if (dist < CLOSE_MIN_MM || dist > CLOSE_MAX_MM) return;

    updateEma(emaClose, (float)dist, EMA_ALPHA_CLOSE);
    float curved = normAndCurve(emaClose, CLOSE_MIN_MM, CLOSE_MAX_MM);

    int16_t cc = constrain((int16_t)lroundf(curved * 127.0f), 0, 127);
    liveCloseCC = cc;
    if (cc != prevCloseCC) {
        prevCloseCC = cc;
        sendCC(CC_CLOSE, cc);
#if DEBUG
        Serial.printf("close=%d ema=%.1f CC%d=%d\n", dist, emaClose, CC_CLOSE, cc);
#endif
    }
}

// ─── Arduino entry points ────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(SW_TRANSPORT, INPUT_PULLUP);
    transport = digitalRead(SW_TRANSPORT) == HIGH ? TRANSPORT_USB : TRANSPORT_BLE;

    pinMode(BTN_BYPASS, INPUT_PULLUP);

    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_PUSH, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);

    USB.enableDFU();
    usbMidi.begin();
    USB.begin();

    if (usesBLE()) initBLE();

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
#if DEBUG
        Serial.println("OLED not found");
#endif
    } else {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(20, 25);
        oled.println("MidiGuitar");
        oled.setCursor(30, 40);
        oled.println("starting...");
        oled.display();
    }

    ring.begin();
    ring.setBrightness(STRIP_BRIGHT);
    ring.show();

    if (!initSensors()) {
        Serial.println("ERR: VL53L1X not responding");
        oled.clearDisplay();
        oled.setCursor(0, 25);
        oled.println("VL53L1X ERROR");
        oled.println("Check wiring!");
        oled.display();
        while (true) { showError(); delay(50); }
    }

#if DEBUG
    Serial.printf("Ready — %s %s  CC%d+%d+%s  ch%d\n",
                  transportNames[transport], modeNames[mode],
                  CC_DIST, CC_VEL,
                  sensorCloseOk ? String(CC_CLOSE).c_str() : "none",
                  MIDI_CH);
#endif

    oledDirty = true;
}

void loop() {
    handleEncoder();
    readBypass();

    if (millis() - tPoll >= POLL_MS) {
        tPoll = millis();
        pollLong();
        pollClose();
        updateRing();
        if (currentPage == PAGE_LIVE) oledDirty = true;
    }

    if (oledDirty && millis() - tOled >= 66) {
        tOled = millis();
        refreshOLED();
    }
}

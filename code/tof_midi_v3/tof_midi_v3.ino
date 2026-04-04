// tof_midi_v3.ino — Dual ToF MIDI controller for electric guitar
//
// Two time-of-flight sensors give independent MIDI control axes:
//   VL53L1X  (main cavity)  — long-range hand gestures (10–500mm)
//   VL6180X  (small cavity) — close-range finger gestures (5–100mm)
//
// MIDI output:
//   CC20        — VL53L1X distance    (wah, filter, delay)
//   CC21        — VL53L1X velocity    (resonance, attack)
//   CC22        — VL6180X distance    (fine modulation, vibrato)
//   Pitch Bend  — VL53L1X distance    (14-bit, mode B)
//
// Hardware:
//   Seeed XIAO ESP32-S3
//   VL53L1X ToF sensor                (I2C, reassigned to 0x30)
//   VL6180X ToF sensor                (I2C, stays at 0x29)
//   16-LED WS2812B NeoPixel ring
//   2x SPDT slide switches
//   LiPo battery (optional)
//
// Wiring:
//                         XIAO ESP32-S3
//                        ┌─────────────┐
//                   5V ──┤ 5V      GND ├── GND bus
//                  GND ──┤ GND     3V3 ├── 3V3 bus
//          I2C SDA    ──┤ D4      D10 ├──
//          I2C SCL    ──┤ D5       D9 ├──
//   VL53L1X XSHUT ──────┤ D6       D8 ├──
//                        ┤ D7       D7 ├──
//     NeoPixel DIN ──────┤ D1       D6 ├──
//     Transport SW ──────┤ D2            │
//     Mode SW ───────────┤ D3            │
//                        └─────────────┘
//
//   VL53L1X (5 wires — main cavity, long range):
//     VCC → 3V3    GND → GND    SDA → D4    SCL → D5    XSHUT → D6
//
//   VL6180X (4 wires — small cavity, close range):
//     VCC → 3V3    GND → GND    SDA → D4    SCL → D5
//
//   NeoPixel ring (3 wires):
//     VCC → 3V3    GND → GND    DIN → D1
//
//   Transport switch (SW1):
//     Common → D2    Throw A → 3V3 (USB)    Throw B → GND (BLE)
//
//   Mode switch (SW2):
//     Common → D3    Throw A → 3V3 (CC)     Throw B → GND (PitchBend)
//
// Arduino IDE:
//   Board : XIAO_ESP32S3
//   USB   : USB-OTG (TinyUSB)
//
// Libraries (Arduino Library Manager):
//   SparkFun VL53L1X
//   Adafruit VL6180X
//   Adafruit NeoPixel

// ─── Configuration ───────────────────────────────────────────────────────────

#define DEBUG           1

// I2C
#define I2C_SDA         5
#define I2C_SCL         6
#define I2C_FREQ        400000

// MIDI
#define MIDI_CH         1
#define CC_DIST         20          // VL53L1X distance (long range)
#define CC_VEL          21          // VL53L1X hand velocity
#define CC_CLOSE        22          // VL6180X distance (close range)
#define BLE_NAME        "MidiGuitar"

// VL53L1X — long-range sensor (main cavity)
#define DIST_MIN_MM     10
#define DIST_MAX_MM     500
#define VL53_ADDR       0x30        // reassigned at boot (default 0x29 → 0x30)
#define XSHUT_PIN       43          // GPIO43 (XIAO D6) — hold LOW to sleep VL53L1X

// VL6180X — close-range sensor (small cavity)
#define CLOSE_MIN_MM    5
#define CLOSE_MAX_MM    100
// stays at default address 0x29

// Timing & smoothing
#define POLL_MS         20          // ~50 Hz
#define EMA_ALPHA       0.2f
#define EMA_ALPHA_CLOSE 0.3f        // lighter smoothing for close sensor (faster response)

// Velocity
#define VEL_DEAD_MM     2.0f
#define VEL_MAX_MM      30.0f

// Response curve
#define CURVE_LINEAR    0
#define CURVE_LOG       1
#define CURVE_EXP       2
#define CURVE           CURVE_LINEAR

// Pins
#define STRIP_PIN       2           // GPIO2 (XIAO D1)
#define SW_TRANSPORT    3           // GPIO3 (XIAO D2)
#define SW_MODE         4           // GPIO4 (XIAO D3)

// NeoPixel ring
#define NUM_PIXELS      16
#define STRIP_BRIGHT    40

// ─── Includes ────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <SparkFun_VL53L1X.h>
#include <Adafruit_VL6180X.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ─── Types ───────────────────────────────────────────────────────────────────

enum Mode      : uint8_t { MODE_CC, MODE_PITCHBEND, MODE_COUNT };
enum Transport : uint8_t { TRANSPORT_USB, TRANSPORT_BLE };

// ─── BLE MIDI ────────────────────────────────────────────────────────────────

#define BLE_MIDI_SVC   "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLE_MIDI_CHR   "7772E5DB-3868-4112-A1A9-F2669D106BF3"

static bool bleConnected = false;

class MidiServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer *)    override { bleConnected = true;  }
    void onDisconnect(BLEServer *) override {
        bleConnected = false;
        BLEDevice::startAdvertising();
    }
};

// ─── Globals ─────────────────────────────────────────────────────────────────

static USBMIDI             usbMidi;
static SFEVL53L1X          sensorLong;          // VL53L1X — main, long range
static Adafruit_VL6180X    sensorClose;         // VL6180X — small cavity, close range
static bool                sensorCloseOk = false;
static Adafruit_NeoPixel   ring(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
static BLECharacteristic  *bleChr = nullptr;

// Transport & mode
static Transport transport = TRANSPORT_USB;
static Mode      mode      = MODE_CC;

// VL53L1X state (long range)
static float   ema        = -1.0f;
static float   prevEma    = -1.0f;
static int16_t prevDistCC = -1;
static int16_t prevVelCC  = -1;
static int16_t prevBend   = -1;

// VL6180X state (close range)
static float   emaClose     = -1.0f;
static int16_t prevCloseCC  = -1;

// Display value for ring (0–127)
static int16_t displayDist = 0;

// Timing
static unsigned long tPoll = 0;

// ─── Response curve ──────────────────────────────────────────────────────────

static float applyCurve(float t) {
#if CURVE == CURVE_LOG
    return log1pf(t * (M_E - 1.0f));
#elif CURVE == CURVE_EXP
    return (expf(t) - 1.0f) / (M_E - 1.0f);
#else
    return t;
#endif
}

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

// ─── Abstracted MIDI output ─────────────────────────────────────────────────

static void sendCC(uint8_t ch, uint8_t cc, uint8_t val) {
    if (transport == TRANSPORT_USB)
        usbMidi.controlChange(cc, val, ch + 1);
    else
        bleSend(0xB0 | (ch & 0x0F), cc, val);
}

static void sendPitchBend(uint8_t ch, uint16_t bend) {
    if (transport == TRANSPORT_USB) {
        usbMidi.pitchBend((uint16_t)bend, (uint8_t)(ch + 1));
    } else {
        bleSend(0xE0 | (ch & 0x0F), bend & 0x7F, (bend >> 7) & 0x7F);
    }
}

// ─── Sensor init ─────────────────────────────────────────────────────────────
// Both sensors default to I2C address 0x29.  At boot we hold the VL53L1X
// in shutdown via XSHUT, init the VL6180X at 0x29, then wake the VL53L1X
// and reassign it to 0x30.

static bool initSensors() {
    // ── Step 1: shut down VL53L1X so only VL6180X is on the bus ──
    pinMode(XSHUT_PIN, OUTPUT);
    digitalWrite(XSHUT_PIN, LOW);
    delay(10);

    // ── Step 2: init VL6180X at default 0x29 ──
    if (sensorClose.begin()) {
        sensorCloseOk = true;
#if DEBUG
        Serial.println("VL6180X OK at 0x29");
#endif
    } else {
#if DEBUG
        Serial.println("VL6180X not found — continuing without close sensor");
#endif
    }

    // ── Step 3: wake VL53L1X and reassign to 0x30 ──
    digitalWrite(XSHUT_PIN, HIGH);
    delay(10);

    sensorLong.begin(Wire);
    sensorLong.setI2CAddress(VL53_ADDR);

    sensorLong.setTimingBudgetInMs(POLL_MS);
    sensorLong.setIntermeasurementPeriod(POLL_MS);
    sensorLong.startRanging();

    // Verify VL53L1X is alive
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
    BLEDevice::init(BLE_NAME);
    BLEServer *srv = BLEDevice::createServer();
    srv->setCallbacks(new MidiServerCB());

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
}

// ─── NeoPixel ring ───────────────────────────────────────────────────────────

static uint32_t modeColor() {
    if (transport == TRANSPORT_USB)
        return mode == MODE_CC ? ring.Color(0, 255, 0)
                               : ring.Color(180, 0, 255);
    else
        return mode == MODE_CC ? ring.Color(0, 80, 255)
                               : ring.Color(0, 220, 220);
}

static void showRing(int16_t val) {
    static int16_t lastShown = -1;
    if (val == lastShown) return;
    lastShown = val;

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
    if (transport == TRANSPORT_BLE && !bleConnected) {
        showChase();
        return;
    }
    showRing(displayDist);
}

// ─── Read switches ───────────────────────────────────────────────────────────

static void readSwitches() {
    Mode newM = digitalRead(SW_MODE) == HIGH
                ? MODE_CC : MODE_PITCHBEND;

    if (newM != mode) {
        mode = newM;
        prevDistCC = -1;
        prevBend   = -1;
        displayDist = -1;
#if DEBUG
        Serial.printf("Mode -> %s\n", mode == MODE_CC ? "CC" : "PitchBend");
#endif
    }
}

// ─── Poll VL53L1X (long range) → CC20/CC21/PitchBend ────────────────────────

static void pollLong() {
    if (!sensorLong.checkForDataReady()) return;

    int16_t dist = -1;
    if (sensorLong.getRangeStatus() == 0)
        dist = sensorLong.getDistance();
    sensorLong.clearInterrupt();

    if (dist < 0) return;

    // ── Update EMA ──
    if (ema < 0.0f) {
        ema     = (float)dist;
        prevEma = ema;
    } else {
        prevEma = ema;
        ema = EMA_ALPHA * dist + (1.0f - EMA_ALPHA) * ema;
    }

    // ── Normalize + curve ──
    float norm   = (ema - DIST_MIN_MM) / (float)(DIST_MAX_MM - DIST_MIN_MM);
    float curved = applyCurve(constrain(norm, 0.0f, 1.0f));
    uint8_t ch   = MIDI_CH - 1;

    // ── Distance → CC or pitch bend ──
    if (mode == MODE_CC) {
        int16_t cc = (int16_t)lroundf(curved * 127.0f);
        cc = constrain(cc, 0, 127);
        displayDist = cc;

        if (cc != prevDistCC) {
            prevDistCC = cc;
            sendCC(ch, CC_DIST, cc);
#if DEBUG
            Serial.printf("long=%d  ema=%.1f  CC%d=%d\n",
                          dist, ema, CC_DIST, cc);
#endif
        }
    } else {
        int16_t bend = (int16_t)lroundf(curved * 16383.0f);
        bend = constrain(bend, (int16_t)0, (int16_t)16383);
        displayDist = bend / (16383 / 127);

        if (bend != prevBend) {
            prevBend = bend;
            sendPitchBend(ch, bend);
#if DEBUG
            Serial.printf("long=%d  ema=%.1f  bend=%d\n", dist, ema, bend);
#endif
        }
    }

    // ── Velocity → CC21 (always active) ──
    float delta = fabsf(ema - prevEma);
    if (delta < VEL_DEAD_MM) delta = 0.0f;

    int16_t velCC = (int16_t)lroundf(
        constrain(delta / VEL_MAX_MM * 127.0f, 0.0f, 127.0f));

    if (velCC != prevVelCC) {
        prevVelCC = velCC;
        sendCC(ch, CC_VEL, velCC);
#if DEBUG
        if (velCC > 0)
            Serial.printf("  velocity=%.1f  CC%d=%d\n", delta, CC_VEL, velCC);
#endif
    }
}

// ─── Poll VL6180X (close range) → CC22 ──────────────────────────────────────

static void pollClose() {
    if (!sensorCloseOk) return;

    uint8_t status = sensorClose.readRangeStatus();
    if (status != VL6180X_ERROR_NONE) return;

    int16_t dist = sensorClose.readRange();
    if (dist < CLOSE_MIN_MM || dist > CLOSE_MAX_MM) return;

    // ── Update EMA ──
    if (emaClose < 0.0f)
        emaClose = (float)dist;
    else
        emaClose = EMA_ALPHA_CLOSE * dist + (1.0f - EMA_ALPHA_CLOSE) * emaClose;

    float norm = (emaClose - CLOSE_MIN_MM) / (float)(CLOSE_MAX_MM - CLOSE_MIN_MM);
    float curved = applyCurve(constrain(norm, 0.0f, 1.0f));

    int16_t cc = (int16_t)lroundf(curved * 127.0f);
    cc = constrain(cc, 0, 127);

    if (cc != prevCloseCC) {
        prevCloseCC = cc;
        sendCC(MIDI_CH - 1, CC_CLOSE, cc);
#if DEBUG
        Serial.printf("close=%d  ema=%.1f  CC%d=%d\n",
                      dist, emaClose, CC_CLOSE, cc);
#endif
    }
}

// ─── Arduino entry points ────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(2000);

    // ── Switches ──
    pinMode(SW_TRANSPORT, INPUT_PULLUP);
    pinMode(SW_MODE, INPUT_PULLUP);
    transport = digitalRead(SW_TRANSPORT) == HIGH
                ? TRANSPORT_USB : TRANSPORT_BLE;
    mode = digitalRead(SW_MODE) == HIGH
           ? MODE_CC : MODE_PITCHBEND;

    // ── USB ──
    USB.enableDFU();
    if (transport == TRANSPORT_USB)
        usbMidi.begin();
    USB.begin();

    // ── BLE ──
    if (transport == TRANSPORT_BLE)
        initBLE();

    // ── I2C + sensors ──
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

    // ── NeoPixel ring ──
    ring.begin();
    ring.setBrightness(STRIP_BRIGHT);
    ring.show();

    // ── Both ToF sensors ──
    if (!initSensors()) {
        Serial.println("ERR: VL53L1X not responding — check wiring");
        while (true) { showError(); delay(50); }
    }

#if DEBUG
    Serial.printf("Ready — %s %s  CC%d+%d+%s  ch%d  [%s]\n",
                  transport == TRANSPORT_USB ? "USB" : "BLE",
                  mode == MODE_CC ? "CC" : "Bend",
                  CC_DIST, CC_VEL,
                  sensorCloseOk ? String(CC_CLOSE).c_str() : "no close",
                  MIDI_CH,
                  CURVE == CURVE_LOG ? "log"
                  : CURVE == CURVE_EXP ? "exp" : "lin");
#endif
}

void loop() {
    if (millis() - tPoll < POLL_MS) return;
    tPoll = millis();

    readSwitches();
    pollLong();
    pollClose();
    updateRing();
}

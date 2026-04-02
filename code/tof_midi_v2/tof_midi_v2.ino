// tof_midi_v2.ino — Time-of-flight MIDI controller for electric guitar
//
// Turns a VL53L1X/VL53L4CD distance sensor into a multi-dimensional
// MIDI controller over USB or Bluetooth LE.  Designed to be embedded
// in a Squier Mustang — wave your hand over the sensor to modulate
// effects in Max/MSP, Ableton, or any MIDI host.
//
// MIDI output (USB or BLE, selected by slide switch):
//   CC20        — hand distance   (continuous: wah, filter, …)
//   CC21        — hand velocity   (speed-based: resonance, attack, …)
//   Pitch Bend  — hand distance   (14-bit, smooth pitch shifting)
//   Touch tap cycles mode:  CC → Pitch Bend → CC → …
//
// Hardware:
//   Seeed XIAO ESP32-S3  +  VL53L1X/VL53L4CD (I2C)
//   Slide switch:  USB / BLE transport select
//   NeoPixel 8-LED strip:  visual feedback (bar graph + status)
//   Copper touch pad:  mode cycling (optional)
//   LiPo battery on XIAO BAT+/BAT- pads (optional)
//
// Wiring:
//   SDA → D4 (GPIO5)    SCL → D5 (GPIO6)     3V3, GND
//   NeoPixel DIN → D1 (GPIO2)
//   Switch common → D2 (GPIO3),  throw A → 3V3,  throw B → GND
//   Touch pad → D0 (GPIO1)
//
// Arduino IDE:
//   Board : XIAO_ESP32S3
//   USB   : USB-OTG (TinyUSB)
//
// Libraries (Arduino Library Manager):
//   SparkFun VL53L1X          ← also works with VL53L4CD
//   Adafruit NeoPixel
//   USB + USBMIDI             ← built into ESP32 Arduino Core 3.x
//   BLE                       ← built into ESP32 Arduino Core 3.x

// ─── Configuration ───────────────────────────────────────────────────────────

#define DEBUG           1

// I2C (ToF sensor)
#define I2C_SDA         5           // GPIO5 (XIAO D4)
#define I2C_SCL         6           // GPIO6 (XIAO D5)
#define I2C_FREQ        400000

// MIDI
#define MIDI_CH         1           // MIDI channel (1–16)
#define CC_DIST         20          // CC for hand distance
#define CC_VEL          21          // CC for hand velocity
#define BLE_NAME        "MidiGuitar"

// Distance range
#define DIST_MIN_MM     10          // closest  → CC 0
#define DIST_MAX_MM     500         // farthest → CC 127

// Timing & smoothing
#define POLL_MS         20          // ~50 Hz
#define EMA_ALPHA       0.2f        // 0 → heavy smoothing, 1 → raw

// Velocity
#define VEL_DEAD_MM     2.0f        // dead zone (mm)
#define VEL_MAX_MM      30.0f       // saturates CC 127

// Response curve: CURVE_LINEAR, CURVE_LOG, or CURVE_EXP
#define CURVE_LINEAR    0
#define CURVE_LOG       1
#define CURVE_EXP       2
#define CURVE           CURVE_LINEAR

// Pins — set to -1 to compile out
#define SWITCH_PIN      3           // GPIO3 (XIAO D2) — HIGH=USB, LOW=BLE
#define STRIP_PIN       2           // GPIO2 (XIAO D1) — NeoPixel data
#define TOUCH_PIN       1           // GPIO1 (XIAO D0) — capacitive touch

#define NUM_PIXELS      8           // LEDs on the NeoPixel strip
#define STRIP_BRIGHT    50          // 0–255, keep low for battery life

// Touch tuning
#define TOUCH_THRESH    40000
#define TOUCH_DEBOUNCE_MS 200

// ─── Includes ────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <SparkFun_VL53L1X.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ─── Types ───────────────────────────────────────────────────────────────────

enum Mode : uint8_t { MODE_CC, MODE_PITCHBEND, MODE_COUNT };
enum Transport : uint8_t { TRANSPORT_USB, TRANSPORT_BLE };

// ─── BLE MIDI constants ─────────────────────────────────────────────────────

#define BLE_MIDI_SVC   "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLE_MIDI_CHR   "7772E5DB-3868-4112-A1A9-F2669D106BF3"

// ─── BLE callbacks ──────────────────────────────────────────────────────────

static bool bleConnected = false;

class MidiServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer *)    override { bleConnected = true;  }
    void onDisconnect(BLEServer *) override {
        bleConnected = false;
        BLEDevice::startAdvertising();   // auto-reconnect
    }
};

// ─── Globals ─────────────────────────────────────────────────────────────────

static USBMIDI             usbMidi;
static SFEVL53L1X          sensor;
static Adafruit_NeoPixel   strip(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
static BLECharacteristic  *bleChr = nullptr;

// Transport & mode
static Transport transport = TRANSPORT_USB;
static Mode      mode      = MODE_CC;

// Distance & velocity state
static float   ema        = -1.0f;
static float   prevEma    = -1.0f;
static int16_t prevDistCC = -1;
static int16_t prevVelCC  = -1;
static int16_t prevBend   = -1;

// The value currently shown on the strip (0–127 or 0–16383 mapped to 0–127)
static int16_t displayVal = 0;

// Touch
#if TOUCH_PIN >= 0
static uint32_t      touchBaseline = 0;
static bool          touchDown     = false;
static unsigned long touchLastTap  = 0;
#endif

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
// BLE MIDI packets: [header, timestamp, status, data1, data2]

static void bleSend(uint8_t status, uint8_t d1, uint8_t d2) {
    if (!bleConnected || !bleChr) return;

    uint16_t ts  = millis() & 0x1FFF;
    uint8_t  pkt[5] = {
        (uint8_t)(0x80 | (ts >> 7)),        // header
        (uint8_t)(0x80 | (ts & 0x7F)),      // timestamp
        status, d1, d2
    };
    bleChr->setValue(pkt, 5);
    bleChr->notify();
}

// ─── Abstracted MIDI output ─────────────────────────────────────────────────

static void sendCC(uint8_t ch, uint8_t cc, uint8_t val) {
    if (transport == TRANSPORT_USB)
        usbMidi.controlChange(ch, cc, val);
    else
        bleSend(0xB0 | (ch & 0x0F), cc, val);
}

static void sendPitchBend(uint8_t ch, uint16_t bend) {
    if (transport == TRANSPORT_USB) {
        usbMidi.pitchBend(ch, bend);
    } else {
        uint8_t lsb = bend & 0x7F;
        uint8_t msb = (bend >> 7) & 0x7F;
        bleSend(0xE0 | (ch & 0x0F), lsb, msb);
    }
}

// ─── Sensor init ─────────────────────────────────────────────────────────────

static bool initSensor() {
    // begin() may reject VL53L4CD model ID — ignore; register init still works
    sensor.begin(Wire);
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

#if DEBUG
    Serial.println("BLE advertising as \"" BLE_NAME "\"");
#endif
}

// ─── Touch init ──────────────────────────────────────────────────────────────

#if TOUCH_PIN >= 0
static void initTouch() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += touchRead(TOUCH_PIN);
        delay(10);
    }
    touchBaseline = sum / 8;
#if DEBUG
    Serial.printf("Touch baseline: %lu\n", (unsigned long)touchBaseline);
#endif
}
#endif

// ─── NeoPixel helpers ────────────────────────────────────────────────────────

static uint32_t modeColor() {
    // Green/purple for USB, blue/cyan for BLE
    if (transport == TRANSPORT_USB)
        return mode == MODE_CC ? strip.Color(0, 255, 0)
                               : strip.Color(180, 0, 255);
    else
        return mode == MODE_CC ? strip.Color(0, 80, 255)
                               : strip.Color(0, 220, 220);
}

static void showBarGraph(int16_t val) {
    static int16_t lastShown = -1;
    if (val == lastShown) return;       // skip redundant SPI transfer
    lastShown = val;

    int numLit = (val * NUM_PIXELS + 63) / 127;
    numLit = constrain(numLit, 0, NUM_PIXELS);
    uint32_t col = modeColor();

    strip.clear();
    for (int i = 0; i < numLit; i++)
        strip.setPixelColor(i, col);
    strip.show();
}

static void showChase() {
    // Blue scanning animation while BLE is advertising
    static uint8_t pos = 0;
    strip.clear();
    strip.setPixelColor(pos % NUM_PIXELS, strip.Color(0, 40, 200));
    strip.setPixelColor((pos + 1) % NUM_PIXELS, strip.Color(0, 15, 80));
    strip.show();
    pos++;
}

static void showError() {
    bool on = (millis() / 100) % 2;
    strip.fill(on ? strip.Color(255, 0, 0) : 0);
    strip.show();
}

static void updateStrip() {
    // BLE mode, not connected → chase animation
    if (transport == TRANSPORT_BLE && !bleConnected) {
        showChase();
        return;
    }
    showBarGraph(displayVal);
}

// ─── Poll ToF → MIDI ────────────────────────────────────────────────────────

static void pollToF() {
    if (!sensor.checkForDataReady()) return;

    int16_t dist = -1;
    if (sensor.getRangeStatus() == 0)
        dist = sensor.getDistance();
    sensor.clearInterrupt();

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
    uint8_t ch   = MIDI_CH - 1;     // MIDI channels are 0-indexed

    // ── Distance → CC or pitch bend ──
    if (mode == MODE_CC) {
        int16_t cc = (int16_t)lroundf(curved * 127.0f);
        cc = constrain(cc, 0, 127);
        displayVal = cc;

        if (cc != prevDistCC) {
            prevDistCC = cc;
            sendCC(ch, CC_DIST, cc);
#if DEBUG
            Serial.printf("dist=%d  ema=%.1f  CC%d=%d\n",
                          dist, ema, CC_DIST, cc);
#endif
        }
    } else {
        int16_t bend = (int16_t)lroundf(curved * 16383.0f);
        bend = constrain(bend, (int16_t)0, (int16_t)16383);
        displayVal = bend / (16383 / 127);  // scale to 0–127 for bar graph

        if (bend != prevBend) {
            prevBend = bend;
            sendPitchBend(ch, bend);
#if DEBUG
            Serial.printf("dist=%d  ema=%.1f  bend=%d\n", dist, ema, bend);
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

// ─── Poll touch ──────────────────────────────────────────────────────────────

#if TOUCH_PIN >= 0
static void pollTouch() {
    bool down = (touchRead(TOUCH_PIN) > touchBaseline + TOUCH_THRESH);
    unsigned long now = millis();

    if (down && !touchDown && (now - touchLastTap > TOUCH_DEBOUNCE_MS)) {
        touchLastTap = now;
        mode = (Mode)((mode + 1) % MODE_COUNT);
        prevDistCC = -1;
        prevBend   = -1;
        displayVal = -1;    // force strip redraw with new mode color
#if DEBUG
        Serial.printf("Mode → %s\n",
                      mode == MODE_CC ? "CC" : "PitchBend");
#endif
    }
    touchDown = down;
}
#endif

// ─── Arduino entry points ────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    // ── Read transport switch ──
    pinMode(SWITCH_PIN, INPUT_PULLUP);
    transport = digitalRead(SWITCH_PIN) == HIGH
                ? TRANSPORT_USB : TRANSPORT_BLE;

    // ── USB (always init for DFU + Serial; MIDI only in USB mode) ──
    USB.enableDFU();
    if (transport == TRANSPORT_USB)
        usbMidi.begin();
    USB.begin();

    // ── BLE (only in BLE mode) ──
    if (transport == TRANSPORT_BLE)
        initBLE();

    // ── I2C + sensor ──
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

    // ── NeoPixel strip ──
    strip.begin();
    strip.setBrightness(STRIP_BRIGHT);
    strip.show();

    if (!initSensor()) {
        Serial.println("ERR: sensor not responding — check wiring");
        while (true) { showError(); delay(50); }
    }

#if TOUCH_PIN >= 0
    initTouch();
#endif

#if DEBUG
    Serial.printf("Ready — %s mode, CC%d + CC%d on ch %d  [%s curve]\n",
                  transport == TRANSPORT_USB ? "USB" : "BLE",
                  CC_DIST, CC_VEL, MIDI_CH,
                  CURVE == CURVE_LOG ? "log"
                  : CURVE == CURVE_EXP ? "exp" : "linear");
#endif
}

void loop() {
    if (millis() - tPoll < POLL_MS) return;
    tPoll = millis();

    pollToF();
#if TOUCH_PIN >= 0
    pollTouch();
#endif
    updateStrip();
}

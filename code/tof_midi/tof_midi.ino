// tof_midi.ino
// XIAO ESP32-S3 Sense + VL53L4CD → USB MIDI CC messages
//
// Libraries needed (Arduino Library Manager):
//   - SparkFun VL53L1X Arduino Library  (search "VL53L1X", install SparkFun's)
//   USB MIDI is built into ESP32 Arduino Core 3.x — no extra lib needed.
//
// Arduino IDE settings:
//   Board    : XIAO_ESP32S3
//   USB Mode : USB-OTG (TinyUSB)   ← critical, or MIDI won't enumerate

// ─── Config ──────────────────────────────────────────────────────────────────
#define DEBUG        1     // 1 = print distance + CC to Serial, 0 = silent

#define NUM_SENSORS  1     // Increase when adding sensors (max 4 with pins below)

// XSHUT pins are only needed for multi-sensor setups (NUM_SENSORS > 1).
// Wire each sensor's XSHUT to a GPIO and add address-reassignment logic then.

#define I2C_SDA      5     // D4 — Qwiic SDA
#define I2C_SCL      6     // D5 — Qwiic SCL
#define I2C_FREQ     400000

#define MIDI_CHANNEL 1     // 1–16
#define CC_BASE      20    // Sensor 0 → CC20, sensor 1 → CC21, …

#define DIST_MIN_MM  10    // Distance mapped to CC 0
#define DIST_MAX_MM  500   // Distance mapped to CC 127

#define POLL_RATE_MS 20    // ~50 Hz
#define SMOOTH_ALPHA 0.2f  // EMA factor: 0 = max smooth, 1 = raw

// ─── Includes ────────────────────────────────────────────────────────────────
#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <SparkFun_VL53L1X.h>

// ─── USB MIDI instance ───────────────────────────────────────────────────────
USBMIDI MIDIusb;

// ─── Sensor state ────────────────────────────────────────────────────────────
static SFEVL53L1X       sensors[NUM_SENSORS];
static bool             sensorValid[NUM_SENSORS];
static int16_t          distances[NUM_SENSORS];

// ─── Mapper state ────────────────────────────────────────────────────────────
static float   smoothed[NUM_SENSORS];   // -1.0f = no reading yet (sentinel)
static int16_t lastCC[NUM_SENSORS];

// ─── Timing ──────────────────────────────────────────────────────────────────
static unsigned long lastPoll = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Sensor init
// Wakes sensors one at a time via XSHUT and assigns each a unique I2C address.
// Sensor i gets address 0x30+i (sensor 0 = 0x30, sensor 1 = 0x31, …).
// ─────────────────────────────────────────────────────────────────────────────
static void scanI2C() {
    Serial.println("I2C scan:");
    bool found = false;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  device at 0x%02X\n", addr);
            found = true;
        }
    }
    if (!found) Serial.println("  no devices found — check wiring");
}

static bool initSensors() {
    bool allOk = true;

#if DEBUG
    scanI2C();
#endif

    for (int i = 0; i < NUM_SENSORS; i++) {
        sensorValid[i] = false;
        distances[i]   = -1;

        // begin() may fail model-ID check on some sensor variants — call it
        // for side-effect initialisation but don't bail on the return value.
        sensors[i].begin(Wire);

        sensors[i].setTimingBudgetInMs(20);
        sensors[i].setIntermeasurementPeriod(20);
        sensors[i].startRanging();
        sensorValid[i] = true;

#if DEBUG
        Serial.printf("[sensor %d] started\n", i);
#endif
    }

    return allOk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Poll sensors — non-blocking, called every POLL_RATE_MS
// ─────────────────────────────────────────────────────────────────────────────
static void pollSensors() {
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (!sensorValid[i]) continue;

        if (!sensors[i].checkForDataReady()) continue;

        // getRangeStatus() == 0 means valid; anything else is an error.
        if (sensors[i].getRangeStatus() != 0) {
            distances[i] = -1;
        } else {
            distances[i] = static_cast<int16_t>(sensors[i].getDistance());
        }
        sensors[i].clearInterrupt();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Process readings → smooth → map → send MIDI CC (if changed)
// ─────────────────────────────────────────────────────────────────────────────
static void processSensors() {
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (distances[i] == -1) continue;

        // Seed smoother on first valid reading to avoid a ramp from the initial value.
        if (smoothed[i] < 0.0f)
            smoothed[i] = static_cast<float>(distances[i]);
        else
            smoothed[i] = SMOOTH_ALPHA * distances[i]
                        + (1.0f - SMOOTH_ALPHA) * smoothed[i];

        int16_t ccVal = static_cast<int16_t>(
            constrain(map(static_cast<long>(smoothed[i]),
                          DIST_MIN_MM, DIST_MAX_MM, 0, 127),
                      0, 127));

        if (ccVal == lastCC[i]) continue;
        lastCC[i] = ccVal;

        uint8_t ccNum = CC_BASE + i;
        MIDIusb.controlChange(MIDI_CHANNEL - 1, ccNum, ccVal); // channel is 0-based

#if DEBUG
        Serial.printf("[mapper %d] dist=%d  smooth=%.1f  CC%d=%d\n",
                      i, distances[i], smoothed[i], ccNum, ccVal);
#endif
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setup / loop
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    USB.enableDFU(); // keeps upload port available without needing BOOT+RESET
    MIDIusb.begin();
    USB.begin();

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

    if (!initSensors()) {
        Serial.println("ERROR: sensor init failed — check wiring");
        while (1) delay(1000);
    }

    for (int i = 0; i < NUM_SENSORS; i++) {
        smoothed[i] = -1.0f; // sentinel: seed EMA on first valid reading
        lastCC[i]   = -1;
    }

    Serial.println("Ready.");
}

void loop() {
    unsigned long now = millis();
    if (now - lastPoll >= POLL_RATE_MS) {
        lastPoll = now;
        pollSensors();
        processSensors();
    }
}

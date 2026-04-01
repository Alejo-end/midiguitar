// tof_midi.ino — Time-of-flight distance sensor → USB MIDI CC
//
// Converts hand distance over a VL53L1X/VL53L4CD sensor into MIDI Control
// Change messages.  Built for a Squier Mustang guitar with the sensor
// mounted near the pickups — wave your hand to modulate effects in
// Max/MSP, Ableton, or any MIDI-capable host.
//
// Hardware:  Seeed XIAO ESP32-S3  +  VL53L1X/VL53L4CD breakout (I2C)
// Wiring:   SDA → D4 (GPIO5),  SCL → D5 (GPIO6),  3V3,  GND
//
// Arduino IDE:
//   Board : XIAO_ESP32S3
//   USB   : USB-OTG (TinyUSB)       ← required for MIDI enumeration
//
// Libraries (Arduino Library Manager):
//   SparkFun VL53L1X               ← also works with VL53L4CD
//   USB + USBMIDI                  ← built into ESP32 Arduino Core 3.x

// ─── Configuration ───────────────────────────────────────────────────────────

#define DEBUG         1           // 1 = serial output, 0 = silent

#define I2C_SDA       5           // GPIO5 (XIAO D4)
#define I2C_SCL       6           // GPIO6 (XIAO D5)
#define I2C_FREQ      400000      // 400 kHz fast-mode

#define MIDI_CH       1           // MIDI channel (1–16)
#define CC_NUM        20          // CC number sent to host

#define DIST_MIN_MM   10          // hand closest  → CC 0
#define DIST_MAX_MM   500         // hand farthest → CC 127

#define POLL_MS       20          // sensor poll interval (~50 Hz)
#define EMA_ALPHA     0.2f        // smoothing: 0 → heavy, 1 → raw

// ─── Includes ────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <SparkFun_VL53L1X.h>

// ─── Globals ─────────────────────────────────────────────────────────────────

static USBMIDI      midi;
static SFEVL53L1X   sensor;

static float         ema    = -1.0f;  // EMA accumulator (−1 = unseeded)
static int16_t       prevCC = -1;     // last CC sent (−1 = none yet)
static unsigned long tPoll  = 0;

// ─── Sensor init ─────────────────────────────────────────────────────────────

static bool initSensor() {
    // begin() runs the full register init sequence.  Its model-ID check
    // rejects VL53L4CD variants — ignore the return since the register-
    // level setup is still valid for that sensor family.
    sensor.begin(Wire);

    sensor.setTimingBudgetInMs(POLL_MS);
    sensor.setIntermeasurementPeriod(POLL_MS);
    sensor.startRanging();

    // Block until the first measurement arrives to confirm the sensor
    // is alive and ranging — more reliable than checking begin()'s
    // return value, which can false-negative on VL53L4CD.
    unsigned long t0 = millis();
    while (!sensor.checkForDataReady()) {
        if (millis() - t0 > 200) return false;
        delay(5);
    }
    sensor.clearInterrupt();

#if DEBUG
    Serial.println("Sensor OK");
#endif
    return true;
}

// ─── Read + send ─────────────────────────────────────────────────────────────

static void pollAndSend() {
    if (!sensor.checkForDataReady()) return;

    int16_t dist = -1;
    if (sensor.getRangeStatus() == 0)
        dist = sensor.getDistance();
    sensor.clearInterrupt();

    if (dist < 0) return;

    // Seed EMA on first valid reading to avoid ramp-up artifact
    if (ema < 0.0f)
        ema = (float)dist;
    else
        ema = EMA_ALPHA * dist + (1.0f - EMA_ALPHA) * ema;

    // map() does not clamp, so constrain to valid CC range
    int16_t cc = constrain(
        map(lroundf(ema), DIST_MIN_MM, DIST_MAX_MM, 0, 127), 0, 127);

    if (cc == prevCC) return;
    prevCC = cc;

    midi.controlChange(MIDI_CH - 1, CC_NUM, cc);   // channel is 0-indexed

#if DEBUG
    Serial.printf("dist=%d  ema=%.1f  CC%d=%d\n", dist, ema, CC_NUM, cc);
#endif
}

// ─── Arduino entry points ────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    USB.enableDFU();    // keeps upload port accessible without BOOT+RESET
    midi.begin();
    USB.begin();

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

    if (!initSensor()) {
        Serial.println("ERR: sensor not responding — check wiring");
        while (true) delay(1000);
    }

#if DEBUG
    Serial.printf("Ready — CC%d on ch %d\n", CC_NUM, MIDI_CH);
#endif
}

void loop() {
    if (millis() - tPoll < POLL_MS) return;
    tPoll = millis();
    pollAndSend();
}

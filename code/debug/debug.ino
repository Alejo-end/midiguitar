// debug.ino — hardware diagnostic for the MIDI Guitar board.
//
// Exercises each peripheral and reports status over Serial. Uses the
// NeoPixel ring as a fallback display so you can read results even if
// the OLED is dead.
//
// Board: XIAO_ESP32S3, USB: USB-OTG (TinyUSB)
// Libs : SparkFun VL53L1X, Adafruit NeoPixel, Adafruit SSD1306, Adafruit GFX
//
// Ring layout (when running):
//   px  0..3   green        = MCU + NeoPixel alive  (always)
//   px  4..7   green / red  = OLED init OK / fail
//   px  8..11  green / red  = ToF  init OK / fail
//   px 12..15  white / blue = SW1 pressed / released  (live)

#include <Wire.h>
#include <SparkFun_VL53L1X.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin map matches finalMidiGuitar schematic (XIAO ESP32S3 socket M1).
#define I2C_SDA     43
#define I2C_SCL     6
#define I2C_FREQ    400000
#define XSHUT_VL53  5
#define VL53_INT    9
#define STRIP_PIN   1
#define BTN_EDIT    7
#define ENC_A       3
#define ENC_B       2
#define ENC_PUSH    44
#define NUM_PIXELS  16
#define OLED_ADDR   0x3C
#define OLED_W      128
#define OLED_H      64

Adafruit_NeoPixel ring(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SSD1306  oled(OLED_W, OLED_H, &Wire);
SFEVL53L1X        sensor;

static bool oledOK   = false;
static bool sensorOK = false;
static uint8_t  i2cFound[16];   // bitmap of addresses 0..127, 8 per byte
static uint8_t  i2cCount = 0;

// Counters (volatile for ISR access)
static volatile int32_t  encCount    = 0;   // quadrature net rotation
static volatile uint32_t encA_irqs   = 0;   // raw A-edge count
static volatile uint32_t encB_irqs   = 0;   // raw B-edge count
static volatile uint8_t  encState    = 0;
// Loop-side edge counters (incremented when raw pin transitions LOW)
static uint32_t btnEdges = 0, epbEdges = 0;

// ToF stats
static int16_t  distLast = -1, distMin = 9999, distMax = -1;
static uint8_t  rangeStatusLast = 255;
static uint16_t signalRate = 0;
static uint16_t ambientRate = 0;
static uint32_t distOK = 0, distFail = 0;

static void IRAM_ATTR encISR_A() {
    encA_irqs++;
    static const int8_t tbl[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    uint8_t s = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    encCount += tbl[(encState << 2) | s];
    encState = s;
}
static void IRAM_ATTR encISR_B() {
    encB_irqs++;
    static const int8_t tbl[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    uint8_t s = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    encCount += tbl[(encState << 2) | s];
    encState = s;
}

static void scanBus() {
    Serial.println("[I2C] scanning...");
    memset(i2cFound, 0, sizeof(i2cFound));
    i2cCount = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("       0x%02X\n", a);
            i2cFound[a >> 3] |= (1 << (a & 7));
            i2cCount++;
        }
    }
    Serial.printf("[I2C] %u device(s) responded\n", i2cCount);
}

static bool i2cHas(uint8_t a) { return i2cFound[a >> 3] & (1 << (a & 7)); }

static void paintRing(bool oledFound, bool sensorFound) {
    ring.clear();
    for (int i = 0;  i < 4;  i++) ring.setPixelColor(i, ring.Color(0, 80, 0));
    for (int i = 4;  i < 8;  i++) ring.setPixelColor(i, oledFound   ? ring.Color(0, 80, 0) : ring.Color(80, 0, 0));
    for (int i = 8;  i < 12; i++) ring.setPixelColor(i, sensorFound ? ring.Color(0, 80, 0) : ring.Color(80, 0, 0));
    bool pressed = digitalRead(BTN_EDIT) == LOW;
    for (int i = 12; i < 16; i++) ring.setPixelColor(i, pressed ? ring.Color(80, 80, 80) : ring.Color(0, 0, 80));
    ring.show();
}

static void printBootSummary() {
    Serial.println("\n----- boot summary -----");
    Serial.printf("  oledOK   : %s (0x3C %sACK)\n", oledOK   ? "yes" : "no",  i2cHas(0x3C) ? ""  : "no ");
    Serial.printf("  sensorOK : %s (0x29 %sACK)\n", sensorOK ? "yes" : "no",  i2cHas(0x29) ? ""  : "no ");
    Serial.printf("  i2c devs : %u\n", i2cCount);
    Serial.printf("  free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("  cpu freq : %u MHz\n", (unsigned)getCpuFrequencyMhz());
    Serial.printf("  uptime   : %lu ms\n", (unsigned long)millis());
    Serial.println("------------------------\n");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n========================================");
    Serial.println("  MidiGuitar — hardware debug");
    Serial.println("========================================");

    // NeoPixel first so there's always *some* visual feedback.
    ring.begin();
    ring.setBrightness(40);
    for (int i = 0; i < NUM_PIXELS; i++) ring.setPixelColor(i, ring.Color(80, 80, 0));
    ring.show();
    Serial.println("[NeoPixel] init OK — ring should be yellow");

    // Inputs
    pinMode(BTN_EDIT, INPUT_PULLUP);
    pinMode(ENC_A,    INPUT_PULLUP);
    pinMode(ENC_B,    INPUT_PULLUP);
    pinMode(ENC_PUSH, INPUT_PULLUP);
    delay(5);
    encState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    attachInterrupt(digitalPinToInterrupt(ENC_A), encISR_A, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encISR_B, CHANGE);
    Serial.printf("[Inputs] raw at boot: BTN=%d EPB=%d ENC_A=%d ENC_B=%d (1=released/idle high)\n",
                  digitalRead(BTN_EDIT), digitalRead(ENC_PUSH),
                  digitalRead(ENC_A),    digitalRead(ENC_B));

    // ToF clean-boot: pulse XSHUT low then high.
    pinMode(XSHUT_VL53, OUTPUT);
    digitalWrite(XSHUT_VL53, LOW);
    delay(10);
    digitalWrite(XSHUT_VL53, HIGH);
    delay(10);
    pinMode(VL53_INT, INPUT_PULLUP);

    // I2C
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
    delay(50);
    scanBus();

    // OLED
    Serial.print("[OLED] init at 0x3C... ");
    oledOK = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    Serial.println(oledOK ? "OK" : "FAIL");
    if (oledOK) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(0, 0);
        oled.println("DEBUG MODE");
        oled.println("OLED ok");
        oled.display();
    }

    // ToF — try default address 0x29 (cleanest after XSHUT cycle).
    Serial.print("[ToF] init at 0x29... ");
    if (sensor.begin() == 0) {
        sensor.setDistanceModeLong();
        sensor.setTimingBudgetInMs(20);
        sensor.setIntermeasurementPeriod(20);
        sensor.startRanging();
        sensorOK = true;
        Serial.println("OK");
    } else {
        Serial.println("FAIL");
    }

    printBootSummary();

    Serial.println("Live readout legend:");
    Serial.println("  BTN/EPB     = SW1 / encoder-push state (1=pressed)");
    Serial.println("  bedge/eedge = transition counts (press to bump)");
    Serial.println("  A B         = raw ENC_A / ENC_B pin states");
    Serial.println("  enc         = quadrature accumulated count");
    Serial.println("  Aint Bint   = raw ISR counts (CHANGE)");
    Serial.println("  dist        = mm  (rs=range status, sig=signal kcps, amb=ambient kcps)");
    Serial.println("  ok/fail     = ToF measurements with status 0 vs anything else");
    Serial.println();
}

void loop() {
    static uint32_t tPrint  = 0;
    static uint32_t tStatus = 0;
    static uint32_t tBoot   = 0;
    static uint32_t tRescan = 0;
    static bool     btnPrev = HIGH, epbPrev = HIGH;

    // Latch input edges (every loop, fast)
    bool btnRaw = digitalRead(BTN_EDIT) == LOW;
    bool epbRaw = digitalRead(ENC_PUSH) == LOW;
    if (btnRaw && !btnPrev) btnEdges++;
    if (epbRaw && !epbPrev) epbEdges++;
    btnPrev = btnRaw;
    epbPrev = epbRaw;

    // Sensor poll
    if (sensorOK && sensor.checkForDataReady()) {
        rangeStatusLast = sensor.getRangeStatus();
        int16_t d = sensor.getDistance();
        signalRate  = sensor.getSignalRate();
        ambientRate = sensor.getAmbientRate();
        sensor.clearInterrupt();
        if (rangeStatusLast == 0) {
            distLast = d;
            if (d < distMin) distMin = d;
            if (d > distMax) distMax = d;
            distOK++;
        } else {
            distFail++;
        }
    }

    // Periodic per-200ms console line + OLED refresh
    if (millis() - tPrint > 200) {
        tPrint = millis();
        bool A = digitalRead(ENC_A);
        bool B = digitalRead(ENC_B);
        Serial.printf(
            "BTN=%d EPB=%d  bedge=%lu eedge=%lu  A=%d B=%d enc=%6ld  Aint=%lu Bint=%lu  "
            "dist=%4d rs=%u sig=%u amb=%u  ok=%lu fail=%lu  heap=%u up=%lus\n",
            btnRaw, epbRaw,
            (unsigned long)btnEdges, (unsigned long)epbEdges,
            A, B, (long)encCount,
            (unsigned long)encA_irqs, (unsigned long)encB_irqs,
            distLast, rangeStatusLast, signalRate, ambientRate,
            (unsigned long)distOK, (unsigned long)distFail,
            (unsigned)ESP.getFreeHeap(),
            (unsigned long)(millis() / 1000));
        if (oledOK) {
            oled.clearDisplay();
            oled.setCursor(0, 0);
            oled.printf("BTN%d EPB%d\n", btnRaw, epbRaw);
            oled.printf("ENC %ld\n", (long)encCount);
            oled.printf("AB  %d %d\n", A, B);
            oled.printf("DST %dmm\n", distLast);
            oled.printf("rs%u ok%lu\n", rangeStatusLast, (unsigned long)distOK);
            oled.printf("hp %u\n", (unsigned)ESP.getFreeHeap());
            oled.printf("up %lus\n", (unsigned long)(millis() / 1000));
            oled.display();
        }
    }

    // Status ring tick
    if (millis() - tStatus > 60) {
        tStatus = millis();
        paintRing(oledOK, sensorOK);
    }

    // Re-scan I2C every 5 seconds (catches devices that come/go)
    if (millis() - tRescan > 5000) {
        tRescan = millis();
        scanBus();
    }

    // Re-print boot summary every 30 seconds (so you don't need to scroll up)
    if (millis() - tBoot > 30000) {
        tBoot = millis();
        printBootSummary();
    }
}

// debug_oled.ino — focused SSD1306 OLED bring-up for the MIDI Guitar board.
//
// Walks the OLED through every common failure mode and reports each step
// over Serial so you can pinpoint where it dies:
//   1. I2C bus scan (does *anything* ACK?)
//   2. SSD1306 init at 0x3C (and 0x3D as a fallback)
//   3. Static text, geometry, full-screen fill, inverted display
//   4. Continuous animation (sweeping bar) so a frozen panel is obvious
//
// Board: XIAO_ESP32S3, USB Mode: USB-OTG (TinyUSB)
// Libs : Adafruit SSD1306, Adafruit GFX
//
// Pin map: the schematic (J4 OLED) has SDA on XIAO D4 = GPIO5 and SCL on
// XIAO D5 = GPIO6. The v5 firmware uses GPIO43/6 instead, which does not
// match this board. Toggle USE_FIRMWARE_PINS if you want to try that map.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define USE_FIRMWARE_PINS 0   // 0 = schematic (correct), 1 = v5 firmware

#if USE_FIRMWARE_PINS
  #define I2C_SDA  43
  #define I2C_SCL  6
#else
  #define I2C_SDA  5            // XIAO D4
  #define I2C_SCL  6            // XIAO D5
#endif
#define I2C_FREQ   400000

#define OLED_W     128
#define OLED_H     64
#define OLED_PRIMARY   0x3C
#define OLED_FALLBACK  0x3D     // some clones strap A0 high

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire);

static bool   oledOK    = false;
static uint8_t oledAddr = 0;
static uint8_t i2cCount = 0;
static bool   sawPrimary  = false;
static bool   sawFallback = false;

static void scanBus() {
    Serial.println("[I2C] scanning 0x01..0x7E ...");
    i2cCount = 0;
    sawPrimary = sawFallback = false;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("       0x%02X\n", a);
            if (a == OLED_PRIMARY)  sawPrimary  = true;
            if (a == OLED_FALLBACK) sawFallback = true;
            i2cCount++;
        }
    }
    Serial.printf("[I2C] %u device(s) total\n", i2cCount);
    Serial.printf("[I2C] 0x3C %s, 0x3D %s\n",
                  sawPrimary  ? "ACK" : "NO-ACK",
                  sawFallback ? "ACK" : "NO-ACK");
}

static bool tryInit(uint8_t addr) {
    Serial.printf("[OLED] init at 0x%02X ... ", addr);
    bool ok = oled.begin(SSD1306_SWITCHCAPVCC, addr);
    Serial.println(ok ? "OK" : "FAIL");
    return ok;
}

static void showBanner() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println("OLED DEBUG");
    oled.printf("addr 0x%02X\n", oledAddr);
    oled.printf("SDA %d  SCL %d\n", I2C_SDA, I2C_SCL);
    oled.printf("%dx%d  i2cN %u\n", OLED_W, OLED_H, i2cCount);
    oled.drawRect(0, 32, OLED_W, OLED_H - 32, SSD1306_WHITE);
    oled.setCursor(4, 36);
    oled.println("text + geometry");
    oled.setCursor(4, 48);
    oled.println("ok if readable");
    oled.display();
}

static void runTestPatterns() {
    Serial.println("[OLED] pattern 1/4: banner");
    showBanner();
    delay(1500);

    Serial.println("[OLED] pattern 2/4: full white fill");
    oled.fillScreen(SSD1306_WHITE);
    oled.display();
    delay(800);

    Serial.println("[OLED] pattern 3/4: inverted");
    oled.invertDisplay(true);
    delay(600);
    oled.invertDisplay(false);

    Serial.println("[OLED] pattern 4/4: geometry sweep");
    oled.clearDisplay();
    for (int i = 0; i < OLED_W; i += 4) {
        oled.drawLine(0, 0, i, OLED_H - 1, SSD1306_WHITE);
        oled.drawLine(OLED_W - 1, 0, OLED_W - 1 - i, OLED_H - 1, SSD1306_WHITE);
    }
    oled.drawCircle(OLED_W / 2, OLED_H / 2, 20, SSD1306_WHITE);
    oled.display();
    delay(1200);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n========================================");
    Serial.println("  MidiGuitar — OLED debug");
    Serial.println("========================================");
    Serial.printf("[cfg] SDA=GPIO%d  SCL=GPIO%d  freq=%d Hz\n",
                  I2C_SDA, I2C_SCL, I2C_FREQ);

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
    delay(50);

    scanBus();

    if (!sawPrimary && !sawFallback) {
        Serial.println("[OLED] no candidate address ACK'd. Likely causes:");
        Serial.println("       - SDA/SCL swapped or wrong GPIO");
        Serial.println("       - VCC not on 3V3, or GND missing");
        Serial.println("       - pull-ups missing (most modules include them)");
        Serial.println("       - wrong I2C address strap");
        Serial.println("[OLED] will still attempt begin() so library errors are visible.");
    }

    if (sawPrimary && tryInit(OLED_PRIMARY)) {
        oledOK = true; oledAddr = OLED_PRIMARY;
    } else if (sawFallback && tryInit(OLED_FALLBACK)) {
        oledOK = true; oledAddr = OLED_FALLBACK;
    } else {
        if (tryInit(OLED_PRIMARY))       { oledOK = true; oledAddr = OLED_PRIMARY; }
        else if (tryInit(OLED_FALLBACK)) { oledOK = true; oledAddr = OLED_FALLBACK; }
    }

    if (!oledOK) {
        Serial.println("[OLED] init failed. Stopping pattern tests.");
        Serial.println("[OLED] will keep rescanning every 2 s — power cycle is OK.");
        return;
    }

    runTestPatterns();
    Serial.println("[OLED] static tests done. Entering live animation.");
}

void loop() {
    static uint32_t tFrame = 0;
    static uint32_t tStatus = 0;
    static uint32_t tRescan = 0;
    static uint16_t frame = 0;

    if (!oledOK) {
        if (millis() - tRescan > 2000) {
            tRescan = millis();
            scanBus();
            if (sawPrimary && tryInit(OLED_PRIMARY))       { oledOK = true; oledAddr = OLED_PRIMARY; runTestPatterns(); }
            else if (sawFallback && tryInit(OLED_FALLBACK)){ oledOK = true; oledAddr = OLED_FALLBACK; runTestPatterns(); }
        }
        return;
    }

    if (millis() - tFrame > 33) {
        tFrame = millis();
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(0, 0);
        oled.printf("OLED 0x%02X live\n", oledAddr);
        oled.printf("frame %u\n", frame);
        oled.printf("up %lus\n", (unsigned long)(millis() / 1000));

        int x = frame % (OLED_W * 2);
        if (x >= OLED_W) x = (OLED_W * 2) - x;
        oled.fillRect(0, OLED_H - 8, OLED_W, 8, SSD1306_BLACK);
        oled.fillRect(x, OLED_H - 8, 8, 8, SSD1306_WHITE);
        oled.drawRect(0, OLED_H - 8, OLED_W, 8, SSD1306_WHITE);
        oled.display();
        frame++;
    }

    if (millis() - tStatus > 1000) {
        tStatus = millis();
        Serial.printf("[live] addr=0x%02X  frame=%u  up=%lus  heap=%u\n",
                      oledAddr, frame,
                      (unsigned long)(millis() / 1000),
                      (unsigned)ESP.getFreeHeap());
    }
}

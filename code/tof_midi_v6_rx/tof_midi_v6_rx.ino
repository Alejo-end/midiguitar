// tof_midi_v6_rx.ino — ESP-NOW → USB-MIDI receiver dongle (no Bluetooth)
//
// The v6 counterpart to tof_midi_v5 (the guitar). Where v5 reads a VL53L1X and
// fans MIDI out over USB + BLE + WiFi, this dongle does the inverse: it RECEIVES
// the guitar's MIDI over ESP-NOW and re-emits it as class-compliant USB-MIDI to
// a host — and it carries its OWN VL53L5CX 8×8 ToF sensor as a second gesture
// zone, fused into the same USB-MIDI stream.
//
//   guitar (v5 + ESP-NOW TX) ── 2.4 GHz ─▶ ┐
//                                          ├─▶ USB-MIDI to host
//   local VL53L5CX 8×8 hand gesture ──────▶ ┘
//
// Why ESP-NOW instead of BLE: connectionless, ~2–4 ms one-way latency, no
// pairing dance, no GATT timestamp gymnastics. There is intentionally NO
// Bluetooth anywhere in this sketch — it was removed wholesale per the v6 plan
// (../tof_midi_v5/CLAUDE.md §4 B5).
//
// PRECONDITION: nothing arrives over ESP-NOW until a transmitter sends 0xCB
// packets on WiFi channel 1. The stock v5 guitar still transmits BLE, not
// ESP-NOW, so the guitar half stays silent ("no ESP-NOW link yet" on screen)
// until the guitar-side ESP-NOW TX is written (CLAUDE.md §4 B5, PR-1). The
// on-board VL53L5CX works independently, so the dongle is testable on its own.
//
// Board: XIAO ESP32-S3 (Heat dongle PCB), USB: USB-OTG (TinyUSB)
// Libs (Arduino IDE Library Manager):
//   SparkFun VL53L5CX Arduino Library
//   Adafruit NeoPixel
//   Adafruit SSD1306, Adafruit GFX
//   (ESP-NOW + USBMIDI ship with the arduino-esp32 core)
//
// ─── Wire-format contract ────────────────────────────────────────────────────
// Fixed 5-byte packet, byte-identical with the guitar TX and tof_midi_v6.ino:
//   magic(1) | seq(1) | status(1) | data1(1) | data2(1)
// `magic` rejects RF garbage of the right length; `seq` is a rolling u8 so we
// can count drops via gaps. The struct is __attribute__((packed)) — keep it so,
// or TX/RX disagree on byte offsets the instant a field's type changes.
//
// ─── Scope of THIS file ──────────────────────────────────────────────────────
// Focused, works-on-first-flash receiver. Mirrors every MIDI message to the 5-pin
// DIN MIDI OUT socket (Serial1 on D6). Deliberately omits the untested Heat extras
// (MCP4561 digipot/expression, DRONE/RHYTHM/DRUMS mode router, WiFi web-config,
// NVS presets) — those live in ../tof_midi_v6/ and can be folded back in once the
// core link + sensor + OLED are confirmed good.

#define DEBUG 1

// ─── Pin map ─ XIAO ESP32-S3, Heat dongle socket. Matches tof_midi_v6.ino
// except BTN_MUTE / BTN_MODE (swapped, see below).
// XIAO Dn → GPIO: D0=1 D1=2 D2=3 D3=4 D4=5 D5=6 D6=43 D7=44 D8=7 D9=8 D10=9
#define I2C_SDA       5     // D4 → VL53L5CX SDA + OLED SDA
#define I2C_SCL       6     // D5 → VL53L5CX SCL + OLED SCL

// 400 kHz — matches v5 (which drives this exact OLED on this hardware) and the
// v6 Heat sketch. An earlier 100 kHz "marginal-bus" workaround turned out to be
// unnecessary here (v5 is fine at 400 kHz) and was dropped. If a future board
// has long/flaky I²C wiring and the OLED init corrupts, drop this to 100000.
#define I2C_FREQ      400000

#define ENC_A         7     // D8 → encoder channel A
#define ENC_B         8     // D9 → encoder channel B
#define ENC_PUSH      4     // D3 → encoder pushbutton to GND

// Heat receiver board rev 2026-10: MUTE moved to D1 so the FOOTSWITCH can reach it (a mono
// 6.35 mm jack in the case wall, wired through the jack board to J6, in parallel with S4);
// MODE moved to D0. Button S4 is now
// MUTE and S3 is MODE (silkscreen matches). tof_midi_v6.ino still uses D0/D1 the
// old way round.
#define BTN_MUTE      2     // D1 → MUTE/panic button S4 + footswitch J6 to GND
                            // (reads HIGH via pull-up if unpopulated — harmless)
#define BTN_MODE      1     // D0 → MODE button S3 to GND: cycles the sensor source
                            // (BOTH → GUITAR → LOCAL). Also optional; pull-up safe.

// Hardware MIDI OUT on a 5-pin DIN socket (Cliff FM6725) on the jack board, wired to pads J5
// (DIN 5 = sink via 100 ohm from D6, DIN 4 = source via 100 ohm from 3V3, DIN 2 = GND).
// Same messages as USB MIDI. At reset the
// ESP32-S3 ROM prints a short boot log on this pin at 115200 baud; MIDI gear
// ignores it (wrong speed). Set to 0 to compile it out.
#define HW_MIDI_OUT   1
#define MIDI_TX_PIN   43    // D6

// Expression pedal on D2: a stereo 6.35 mm jack in the case wall (tip = wiper, ring = supply,
// sleeve = GND). The jack board feeds the ring from 3V3 through 200 ohm (a mono plug only
// shorts that) and filters the wiper with 10k + 100 nF. The internal pull-down holds D2 at
// 0 V with no pedal. The range is learned: CC 11 stays silent until the pedal has moved over
// EXP_MIN_SPAN ADC counts since power-up, so an empty jack never sends anything.
#define EXP_PIN       3     // D2 = GPIO3, ADC1 channel 2 (ADC1 works while ESP-NOW runs)
#define EXP_CC        11    // CC 11 = expression, sent on LOCAL_CH
#define EXP_POLL_MS   5
#define EXP_MIN_SPAN  800   // of 4095 counts: about 20 % of the pedal travel

#define NEOPIXEL_PIN  9     // D10 → level shifter (3.3→5V) → NeoPixel stick DIN
#define NUM_PIXELS    8     // Heat carries an 8-px stick (v5 used a 16-px ring)

#define OLED_W        128
#define OLED_H        64
#define OLED_ADDR     0x3C  // 7-bit addr (silk "0x78" is the 8-bit form, 0x3C<<1)

#define VL53L5CX_ADDR 0x29  // VL53L5CX power-on default; no XSHUT remap on this board

// The on-board VL53L5CX is the local gesture zone. Set to 0 to build the pure
// ESP-NOW→USB-MIDI receiver WITHOUT the SparkFun VL53L5CX library installed
// (handy for a quick bring-up). At 1 you must have "SparkFun VL53L5CX Arduino
// Library" installed (Arduino IDE → Tools → Manage Libraries).
#define USE_LOCAL_TOF 1

#define NUM_PAGES     2     // LIVE / INFO

// ─── Local-sensor → MIDI mapping ─────────────────────────────────────────────
// The on-board VL53L5CX is a SECOND gesture zone, kept on its own MIDI channel
// so the host can tell it apart from the guitar's stream (which arrives over
// ESP-NOW on whatever channel the guitar uses). distance → CC, velocity → CC.
#define LOCAL_CH      2     // MIDI channel (1-16) for the dongle's own sensor
#define LOCAL_CC_DIST 22    // CC# for local distance  (22/23 so they stay distinct
#define LOCAL_CC_VEL  23    // CC# for local velocity   from the guitar's CC 20/21)
#define DIST_MIN_MM   100   // hand range mapped to CC 0..127
#define DIST_MAX_MM   1000
#define DIST_INVERT   0     // 1 → near = 127 (flip the distance ramp)

// ─── Loop tunables ───────────────────────────────────────────────────────────
#define OLED_REFRESH_MS     66      // ~15 fps cap
#define BOOT_SERIAL_WAIT_MS 2000
#define NO_SIGNAL_MS        1500    // mark the ESP-NOW link dead after this gap
#define RATE_WINDOW_MS      1000    // pkts/sec averaged over this window
#define TOF_POLL_MS         10      // ≥100 Hz dataReady check vs the 15 Hz frames
#define NEO_FRAME_MS        33      // ~30 fps NeoPixel cap

// Brightness knobs. The NeoPixel stick is physically right next to the OLED, so
// at high LED brightness it washes the screen out. Bias toward a bright screen
// and a dim ring: bump OLED contrast to max, keep the ring low. Tune to taste.
#define NEOPIXEL_BRIGHTNESS 18      // 0-255 global LED scale (was 60 — too bright)

// OLED brightness. Contrast 0xFF is the max segment current; VCOMH 0x00 is the
// lowest deselect level (brightest) — a second register that pushes the panel
// past what contrast alone gives. Lower OLED_VCOMH = brighter.
#define OLED_CONTRAST       0xFF    // 0x00-0xFF segment current (max = brightest)
#define OLED_VCOMH          0x00    // 0x00/0x20/0x30/0x40 deselect level (0x00 = brightest)

// OLED orientation. A GFX rotation only spins the image 180° (0 ↔ 2). If BOTH
// of those still look wrong, the clone panel is axis-mirrored — fix that with
// the two hardware flips below (rotation can't). Find the right look by trying
// combinations: ROTATION 0/2 × FLIP_V 0/1 × FLIP_H 0/1.
#define OLED_ROTATION       0       // GFX rotation 0/1/2/3 (×90°)
#define OLED_FLIP_V         1       // 1 = mirror TOP↔BOTTOM in hardware (COM scan)
#define OLED_FLIP_H         1       // 1 = mirror LEFT↔RIGHT in hardware (segment remap)
                                    // (FLIP_V/FLIP_H are used only on the SSD1306 path)

// ─── OLED driver ─────────────────────────────────────────────────────────────
// This dongle's 0.96" panel is a 5V SSD1306 module (the SH110X/SH1106 driver
// showed nothing → not an SH1106; the SSD1306 driver shows content → SSD1306).
// It's a 5V part: at 3.3V its on-board LDO browns out → dim/blank (logic still
// ACKs, hence "OLED OK" at boot). Powering VCC at 5V brightens it BUT then its
// I2C pull-ups sit at 5V on the shared bus → corrupts the display (vertical
// lines) AND feeds 5V into the 3.3V ESP32/sensor pins (NOT safe). Proper fix: a
// genuine 3.3V OLED module, or an I2C level shifter between the 3.3V ESP32 and a
// 5V OLED. Keep VCC on 3.3V (dim but safe) until then.
//   0 = SSD1306 via Adafruit_SSD1306 (correct for this panel) ← use this
//   1 = SH1106 via Adafruit_SH110X (showed nothing here — wrong controller)
#define OLED_SH1106 0

// Debug heartbeat — periodic one-line status to Serial (link/sensor/MIDI/heap).
#define DBG_PERIOD_MS       500     // 0 disables the heartbeat even when DEBUG=1
#define DBG_RX_VERBOSE      0       // 1 also logs every forwarded MIDI message (spammy)

// Encoder push gestures
#define ENC_TAP_MAX_MS      400
#define ENC_HOLD_MIN_MS     600

// Button debounce
#define BTN_DEBOUNCE_MS     30
#define BTN_PAIR_HOLD_MS    1200    // hold BTN_MODE this long → pair / unpair

// ─── Includes ────────────────────────────────────────────────────────────────
#include <Wire.h>
#include <USB.h>
#include <USBMIDI.h>
#include <Adafruit_GFX.h>
#if OLED_SH1106
#include <Adafruit_SH110X.h>
#else
#include <Adafruit_SSD1306.h>
#endif
#include <Adafruit_NeoPixel.h>
#if USE_LOCAL_TOF
#include <SparkFun_VL53L5CX_Library.h>
#endif
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>       // esp_read_mac() + ESP_MAC_WIFI_STA — core 3.x moved these here
#include <math.h>
#include <driver/gpio.h>   // gpio_pulldown_en() for the expression input
#include <Preferences.h>   // NVS — persists the paired guitar MAC

// ─── Packet format ───────────────────────────────────────────────────────────
#define ESPNOW_MAGIC   0xCB
// WiFi channel for the ESP-NOW link. MUST match the guitar's ESPNOW_CHANNEL.
// Channel 1 is the most congested 2.4 GHz channel — if packet loss is high in a
// busy WiFi area, try 6 or 11 here AND on the guitar (both must agree).
#define ESPNOW_CHANNEL 1
struct __attribute__((packed)) EspNowMidiPacket {
    uint8_t magic;    // 0xCB sync byte
    uint8_t seq;      // rolling sequence for drop detection
    uint8_t status;   // MIDI status byte (cmd nibble | channel nibble)
    uint8_t data1;    // CC#, note, PB LSB, …
    uint8_t data2;    // value, velocity, PB MSB, 0 if unused
};
static_assert(sizeof(EspNowMidiPacket) == 5,
              "EspNowMidiPacket must stay 5 bytes — packed struct broke");

// ─── Types & globals ─────────────────────────────────────────────────────────
enum Page : uint8_t { PG_LIVE, PG_INFO };

// Per-button debounce state (declared here, before any function signature that
// uses it, so Arduino's auto-prototypes compile). See buttonPressed().
struct BtnDebounce { bool stable = true; bool raw = true; uint32_t lastEdge = 0; };

// Which sensor(s) feed the USB-MIDI output:
//   SRC_BOTH   — guitar (ESP-NOW) + local VL53L5CX  (default; the headline mode)
//   SRC_GUITAR — guitar only; local sensor muted    (pure receiver)
//   SRC_LOCAL  — local only; guitar stream ignored   (standalone gesture pad)
// BTN_MODE cycles through them. Channels don't collide (local is on LOCAL_CH),
// so BOTH just sums the two streams.
enum SensorSrc : uint8_t { SRC_BOTH, SRC_GUITAR, SRC_LOCAL, SRC_COUNT };
static SensorSrc sensorSrc = SRC_BOTH;
// Keep every name ≤4 chars: drawPageLive() prints it in the header gap between
// the title (ends ~x=51) and the status badge (starts x=92), no clipping.
static const char *SRC_NAME[SRC_COUNT] = { "BOTH", "GTR", "LOC" };
static_assert(SRC_COUNT == 3, "SRC_NAME[] must list every SensorSrc value");

static const char *midiCmdName(uint8_t status);   // defined with the OLED helpers;
                                                  // forward-declared for the debug logs

static USBMIDI            usbMidi;
#if OLED_SH1106
static Adafruit_SH1106G   oled(OLED_W, OLED_H, &Wire, -1);  // -1 = no reset pin (4-pin module)
#define OLED_WHITE SH110X_WHITE
#define OLED_BLACK SH110X_BLACK
#else
static Adafruit_SSD1306   oled(OLED_W, OLED_H, &Wire);
#define OLED_WHITE SSD1306_WHITE
#define OLED_BLACK SSD1306_BLACK
#endif
static Adafruit_NeoPixel  strip(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
#if USE_LOCAL_TOF
static SparkFun_VL53L5CX  tof;
static VL53L5CX_ResultsData tofData;
#endif

// RX ring buffer — written in the ESP-NOW callback (WiFi task), drained in
// loop() on core 1. uint8 head/tail; modulo because the buffer is tiny.
#define RX_BUF_LEN 64
static volatile uint8_t   rxHead = 0;
static volatile uint8_t   rxTail = 0;
static EspNowMidiPacket   rxBuf[RX_BUF_LEN];

// Link stats. u32 reads are atomic on ESP32-S3 so loop() can sample without a
// mutex; a one-tick race on the drop math is cosmetic.
static volatile uint32_t  pktTotal      = 0;
static volatile uint32_t  pktDropped    = 0;   // counted via seq gaps
static volatile uint32_t  pktOverflow   = 0;   // RX ring full → packet lost
static volatile uint32_t  pktForeign    = 0;   // dropped: not from the paired guitar
static volatile uint8_t   lastSeq       = 0;
static volatile bool      seqInit       = false;
static volatile uint32_t  lastRxMs      = 0;
static volatile uint8_t   lastPeerMac[6] = {0};
static volatile uint8_t   lastStatus    = 0;
static volatile uint8_t   lastData1     = 0;
static volatile uint8_t   lastData2     = 0;

// Pairing. When `paired`, the recv callback accepts MIDI only from pairedMac
// and drops everyone else (counted as pktForeign); when unpaired it accepts any
// sender (open). Persisted in NVS so the bond survives reboots. Hold BTN_MODE to
// pair to the guitar currently being heard, or to unpair.
static Preferences prefs;
static uint8_t     pairedMac[6] = {0};
static bool        paired       = false;

// Rolling rate (computed in loop, not the callback)
static uint32_t rateWindowStart = 0;
static uint32_t rateWindowPkts  = 0;
static uint32_t lastRatePps     = 0;

// Our own MAC, cached at boot so the INFO page doesn't poke WiFi each redraw.
static uint8_t  selfMac[6] = {0};

// Local VL53L5CX features (filled by pollToF)
static bool     sensorOK   = false;   // sensor present + ranging
static bool     tofValid   = false;   // a hand is currently in range
static bool     tofFresh   = false;   // a new frame landed this iteration
static float    distMm     = 0;       // EMA-smoothed closest zone (mm)
static float    liveVel    = 0;       // EMA-smoothed approach velocity (mm/s)
static uint16_t closestMm  = 0;       // raw closest reading
static uint8_t  distCC     = 0;       // distMm mapped to 0..127
static uint8_t  velCC      = 0;       // |liveVel| mapped to 0..127

// Display + control state
static Page          currentPage = PG_LIVE;
static bool          oledDirty   = true;
static unsigned long tOled       = 0;
static bool          oledOK      = false;   // panel init result; drives the LED fail-tell

// Bypass / mute. When set, drainRx() forwards only NoteOff/AllNotesOff (so any
// in-flight notes can still close on the host) and the local sensor goes silent.
static bool          bypassed    = false;

// Toast — same 1 s overlay as v5/v6.
static const char   *toastText  = nullptr;
static unsigned long toastUntil = 0;
static void showToast(const char *msg) {
    toastText  = msg;
    toastUntil = millis() + 1000;
    oledDirty  = true;
}

// ─── Encoder ─ lifted verbatim from v5 (same physical encoder + wiring) ──────
static volatile int8_t encDelta = 0;
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

// ─── ESP-NOW ─────────────────────────────────────────────────────────────────
// Receive callback. Runs on the WiFi task — KEEP IT SHORT. Anything past
// memcpy + ring enqueue belongs in loop(). (CLAUDE.md §2.1: the hot path is
// sacred; here it's tighter still because the WiFi task feeds it.)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    const uint8_t *mac = info->src_addr;
#else
static void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
    if (len != (int)sizeof(EspNowMidiPacket)) return;
    const EspNowMidiPacket *p = (const EspNowMidiPacket *)data;
    if (p->magic != ESPNOW_MAGIC) return;

    // Pairing filter: once bonded, ignore any guitar but ours. Done before the
    // counters/ring so a foreign sender can't inflate stats or inject MIDI.
    if (paired && memcmp(mac, (const void *)pairedMac, 6) != 0) { pktForeign++; return; }

    pktTotal++;

    // Sequence-gap drop detector. seq jumps by N>1 ⇒ we missed N-1 frames.
    // The 255→0 wrap is a +1 step, so the unsigned subtraction handles it.
    if (seqInit) {
        uint8_t gap = (uint8_t)(p->seq - lastSeq);
        if (gap > 1) pktDropped += (uint32_t)(gap - 1);
    } else {
        seqInit = true;
    }
    lastSeq    = p->seq;
    lastRxMs   = millis();
    // System/realtime (≥0xF0, e.g. the guitar's 0xFE keepalive) keeps the link
    // alive but isn't shown as the "last MIDI" — keep that on real messages.
    if (p->status < 0xF0) {
        lastStatus = p->status;
        lastData1  = p->data1;
        lastData2  = p->data2;
    }
    memcpy((void *)lastPeerMac, mac, 6);

    uint8_t next = (uint8_t)((rxHead + 1) % RX_BUF_LEN);
    if (next == rxTail) { pktOverflow++; return; }  // ring full → drop newest
    rxBuf[rxHead] = *p;
    __sync_synchronize();   // release barrier: commit the slot before publishing
                            // head, so core-1 can't read a half-written packet
    rxHead = next;
}

static bool initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();               // enable the radio without associating to an AP
    // Lock to channel 1; the guitar TX must match. If the guitar later enters a
    // WiFi config mode on a different channel the link drops until both agree —
    // the deliberately-punted coexistence question (CLAUDE.md §4 B5).
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(onEspNowRecv);
    esp_read_mac(selfMac, ESP_MAC_WIFI_STA);

    // Kill modem sleep LAST (after WiFi + ESP-NOW are fully up) so nothing
    // re-enables it. A sleeping STA radio misses most incoming ESP-NOW frames —
    // the classic cause of bursty reception with link flapping up/down.
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    return true;
}

// ─── Pairing (bond the dongle to one guitar) ─────────────────────────────────
static void loadPairing() {
    prefs.begin("tof_rx", true);                    // read-only
    paired = (prefs.getBytes("peer", pairedMac, 6) == 6);
    prefs.end();
    if (!paired) memset(pairedMac, 0, 6);
}

static void savePairing() {
    prefs.begin("tof_rx", false);                   // read-write
    if (paired) prefs.putBytes("peer", pairedMac, 6);
    else        prefs.remove("peer");
    prefs.end();
}

// Hold BTN_MODE: if already bonded, unpair (go open); else bond to whichever
// guitar we're currently hearing. Refuses if no guitar has been heard yet.
static void doPairToggle() {
    if (paired) {
        paired = false;
        savePairing();
        showToast("UNPAIRED");
    } else if (seqInit) {
        memcpy(pairedMac, (const void *)lastPeerMac, 6);
        paired = true;
        savePairing();
        showToast("PAIRED");
    } else {
        showToast("NO GUITAR");
    }
    oledDirty = true;
}

// ─── MIDI forward (ESP-NOW guitar stream → USB + DIN MIDI OUT) ───────────────
// Raw channel messages to the DIN socket; status carries the 0-based channel.
static inline void hwMidi3(uint8_t status, uint8_t d1, uint8_t d2) {
#if HW_MIDI_OUT
    uint8_t m[3] = {status, d1, d2};
    Serial1.write(m, 3);
#endif
}
static inline void hwMidi2(uint8_t status, uint8_t d1) {
#if HW_MIDI_OUT
    uint8_t m[2] = {status, d1};
    Serial1.write(m, 2);
#endif
}

static void allNotesOffAllChannels() {
    for (uint8_t ch = 1; ch <= 16; ch++) {
        usbMidi.controlChange(123, 0, ch);   // All Notes Off
        usbMidi.controlChange(120, 0, ch);   // All Sound Off (belt + suspenders)
        hwMidi3(0xB0 | (ch - 1), 123, 0);
        hwMidi3(0xB0 | (ch - 1), 120, 0);
    }
}

static void drainRx() {
    // In local-only mode the guitar stream is ignored. Still drain the ring so
    // it can't overflow (and stats keep counting) — just don't forward.
    bool guitarOn = (sensorSrc != SRC_LOCAL);

    // Drain everything queued this iteration — cheap, keeps latency low. The
    // bound matches RX_BUF_LEN so a runaway producer can't lock us in.
    for (uint8_t i = 0; i < RX_BUF_LEN; i++) {
        if (rxTail == rxHead) break;
        EspNowMidiPacket p = rxBuf[rxTail];
        rxTail = (uint8_t)((rxTail + 1) % RX_BUF_LEN);

        if (!guitarOn) continue;   // source excludes the guitar — discard

        uint8_t cmd = p.status & 0xF0;
        uint8_t ch  = (uint8_t)((p.status & 0x0F) + 1);   // USBMIDI wants 1-16

#if DEBUG && DBG_RX_VERBOSE
        Serial.printf("[rx] seq=%u %s ch%u %u %u%s\n",
                      (unsigned)p.seq, midiCmdName(p.status), (unsigned)ch,
                      (unsigned)p.data1, (unsigned)p.data2, bypassed ? " (muted)" : "");
#endif

        // Bypass asymmetry: while muted, forward only sound-stopping messages —
        // per-key NoteOff (0x80) and the channel-mode "all off" CCs (120-127,
        // incl. All Sound Off 120 / All Notes Off 123) — so nothing hangs on the
        // host. Anything that could START sound is dropped.
        if (bypassed) {
            bool release = (cmd == 0x80) ||
                           (cmd == 0xB0 && p.data1 >= 120 && p.data1 <= 127);
            if (!release) continue;
        }

        switch (cmd) {
            case 0x80: usbMidi.noteOff       (p.data1, p.data2, ch); break;
            case 0x90: usbMidi.noteOn        (p.data1, p.data2, ch); break;
            case 0xA0: usbMidi.polyPressure  (p.data1, p.data2, ch); break;
            case 0xB0: usbMidi.controlChange (p.data1, p.data2, ch); break;
            case 0xC0: usbMidi.programChange (p.data1, ch);          break;
            case 0xD0: usbMidi.channelPressure(p.data1, ch);         break;
            case 0xE0: usbMidi.pitchBend(
                           (uint16_t)(p.data1 | (p.data2 << 7)), ch); break;
            default: break;   // ignore system/realtime for now
        }
        switch (cmd) {
            case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
                hwMidi3(p.status, p.data1, p.data2); break;
            case 0xC0: case 0xD0:
                hwMidi2(p.status, p.data1); break;
            default: break;
        }
    }
}

// ─── Local VL53L5CX ─ bring-up (lifted from tof_midi_v6.ino) ─────────────────
// 8×8 @ 15 Hz, sharpener 20% (close-range hand zones suffer at the default 5%),
// CLOSEST target order so a hand wins over the back wall. begin() uploads an
// ~84 KB firmware blob (~1 s at 400 kHz) and runs BEFORE the WDT is armed.
#if USE_LOCAL_TOF
static bool initToF() {
    Wire.beginTransmission(VL53L5CX_ADDR);
    if (Wire.endTransmission() != 0) {
#if DEBUG
        Serial.println("[tof] no ACK at 0x29");
#endif
        return false;
    }
    if (!tof.begin(VL53L5CX_ADDR, Wire)) {
#if DEBUG
        Serial.println("[tof] begin() failed");
#endif
        return false;
    }
    tof.setResolution(64);
    tof.setRangingFrequency(15);
    tof.setRangingMode(SF_VL53L5CX_RANGING_MODE::CONTINUOUS);
    tof.setSharpenerPercent(20);
    tof.setTargetOrder(SF_VL53L5CX_TARGET_ORDER::CLOSEST);
    tof.startRanging();
    return true;
}

// Zone status 5 = nominal valid, 9 = valid-with-wraparound (range > sensor max
// but the closest reading is still usable). Either is acceptable.
static inline bool zoneOk(uint8_t s) { return s == 5 || s == 9; }

#define EMA_ALPHA_DIST 0.30f
#define EMA_ALPHA_VEL  0.20f
#define VEL_DEAD_MMS   5.0f
#define VEL_FULL_MMS   500.0f   // |velocity| at/above this maps to CC 127

static void pollToF() {
    tofFresh = false;
    if (!sensorOK) return;

    static uint32_t tNextCheck = 0;
    static uint32_t tLast      = 0;
    static float    prevDist   = 0;
    uint32_t now = millis();
    if (now < tNextCheck) return;
    tNextCheck = now + TOF_POLL_MS;

    if (!tof.isDataReady()) return;
    if (!tof.getRangingData(&tofData)) return;

    // 1. Closest valid zone (≥20 mm rejects the cover-glass self-return).
    uint16_t minD = 0xFFFF;
    for (int i = 0; i < 64; i++) {
        if (!zoneOk(tofData.target_status[i])) continue;
        uint16_t d = (uint16_t)tofData.distance_mm[i];
        if (d >= 20 && d < minD) minD = d;
    }
    if (minD == 0xFFFF) { tofValid = false; return; }
    closestMm = minD;

    // 2. EMA-smoothed distance.
    float newD = (float)minD;
    if (distMm == 0) distMm = newD;                 // seed on first frame
    distMm += EMA_ALPHA_DIST * (newD - distMm);

    // 3. Approach velocity (positive = hand approaching), EMA-smoothed.
    if (tLast > 0) {
        float dt = (now - tLast) * 0.001f;
        if (dt > 0.0001f) {
            float v = (prevDist - distMm) / dt;
            if (fabsf(v) < VEL_DEAD_MMS) v = 0.0f;
            liveVel += EMA_ALPHA_VEL * (v - liveVel);
        }
    }
    prevDist = distMm;
    tLast    = now;
    tofValid = true;

    // 4. CC-domain mappings.
    int   span = DIST_MAX_MM - DIST_MIN_MM;
    float t    = (span > 0) ? ((distMm - DIST_MIN_MM) / (float)span) : 0.0f;
    t = constrain(t, 0.0f, 1.0f);
#if DIST_INVERT
    t = 1.0f - t;
#endif
    distCC = (uint8_t)lroundf(t * 127.0f);

    float av = fabsf(liveVel);
    if (av > VEL_FULL_MMS) av = VEL_FULL_MMS;
    velCC = (uint8_t)lroundf((av / VEL_FULL_MMS) * 127.0f);

    tofFresh = true;
}
#else
static bool initToF() { return false; }   // sensor compiled out (USE_LOCAL_TOF 0)
static void pollToF() {}
#endif // USE_LOCAL_TOF

// Emit the local sensor as CC on LOCAL_CH, only on change, bypass-aware.
static void sendLocalSensorMidi() {
    static int16_t prevDistCC = -1;
    static int16_t prevVelCC  = -1;
    if (sensorSrc == SRC_GUITAR) return;   // local sensor muted in guitar-only mode
    if (!tofFresh || bypassed) return;
    if (distCC != prevDistCC) {
        prevDistCC = distCC;
        usbMidi.controlChange(LOCAL_CC_DIST, distCC, LOCAL_CH);
        hwMidi3(0xB0 | (LOCAL_CH - 1), LOCAL_CC_DIST, distCC);
    }
    if (velCC != prevVelCC) {
        prevVelCC = velCC;
        usbMidi.controlChange(LOCAL_CC_VEL, velCC, LOCAL_CH);
        hwMidi3(0xB0 | (LOCAL_CH - 1), LOCAL_CC_VEL, velCC);
    }
}

// ─── Expression pedal (D2) ───────────────────────────────────────────────────
static uint8_t expCC   = 0;
static bool    expLive = false;   // true once the pedal has moved far enough to learn its range

static void initExp() {
    analogReadResolution(12);
    analogSetPinAttenuation(EXP_PIN, ADC_11db);
    (void)analogRead(EXP_PIN);                  // attaches the pin to the ADC first
    gpio_pullup_dis((gpio_num_t)EXP_PIN);
    gpio_pulldown_en((gpio_num_t)EXP_PIN);      // an empty jack reads 0 V
}

static void pollExp(uint32_t now) {
    static uint32_t tLast = 0;
    static float    ema   = -1.0f;
    static float    lo    = 4095.0f, hi = 0.0f;
    static int16_t  sent  = -1;
    if ((now - tLast) < EXP_POLL_MS) return;
    tLast = now;
    float raw = (float)analogRead(EXP_PIN);
    ema = (ema < 0.0f) ? raw : ema + 0.2f * (raw - ema);
    if (ema < lo) lo = ema;
    if (ema > hi) hi = ema;
    if (hi - lo < EXP_MIN_SPAN) return;
    expLive = true;
    float scaled = (ema - lo) / (hi - lo) * 127.0f;
    // Hysteresis: move only when the reading is 0.6 steps away from the last value sent.
    if (sent >= 0 && fabsf(scaled - (float)sent) < 0.6f) return;
    int16_t v = (int16_t)lroundf(scaled);
    if (v == sent) return;
    sent  = v;
    expCC = (uint8_t)v;
    if (bypassed) return;
    usbMidi.controlChange(EXP_CC, expCC, LOCAL_CH);
    hwMidi3(0xB0 | (LOCAL_CH - 1), EXP_CC, expCC);
}

// Link-alive test, underflow-safe: a packet landing mid-loop can make lastRxMs
// briefly newer than `now`, so use a SIGNED delta — negative (just-arrived) and
// 0..NO_SIGNAL_MS both read as alive; only a real gap reads as dead.
static inline bool linkAlive(uint32_t now) {
    return seqInit && (int32_t)(now - lastRxMs) < (int32_t)NO_SIGNAL_MS;
}

// ─── NeoPixel ────────────────────────────────────────────────────────────────
static void initNeoPixel() {
    strip.begin();
    strip.setBrightness(NEOPIXEL_BRIGHTNESS);
    strip.clear();
    strip.show();
}

// Precedence (most→least urgent): mute → local hand bar → guitar-link activity
// → idle breathe. The hand bar wins over link activity so the performer always
// sees their own gesture; link activity is the fallback "I'm hearing the guitar"
// indicator when no hand is over the local sensor.
static void showLinkPulse(uint32_t now) {
    // Soft blue pulse whose speed rises with packet rate.
    float phase = sinf(now * TWO_PI / 700.0f);
    uint8_t b = 6 + (uint8_t)(24.0f * (1.0f + phase) / 2.0f);
    for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(0, b / 3, b));
}

static void showIdleBreathe(uint32_t now) {
    float phase = sinf(now * TWO_PI / 2400.0f);
    uint8_t b = 3 + (uint8_t)(10.0f * (1.0f + phase) / 2.0f);
    for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(b, b / 2, 0));
}

static void updateNeoPixel() {
    static uint32_t tPix = 0;
    uint32_t now = millis();
    if ((now - tPix) < NEO_FRAME_MS) return;
    tPix = now;

    strip.clear();
    bool linkUp = linkAlive(now);

    // State-driven, NOT sensorSrc-driven: a local hand always wins the ring so
    // the performer sees their gesture regardless of the active source mode.
    if (bypassed) {
        for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(40, 0, 0));
    } else if (tofValid) {
        // Distance → lit pixels; velocity → colour (green slow → red fast).
        int lit  = ((int)distCC * NUM_PIXELS) / 127;
        uint8_t r = (velCC * 2 > 255) ? 255 : (uint8_t)(velCC * 2);
        uint8_t g = 255 - r;
        for (int i = 0; i < lit && i < NUM_PIXELS; i++)
            strip.setPixelColor(i, strip.Color(r, g, 0));
    } else if (linkUp) {
        showLinkPulse(now);
    } else if (!oledOK) {
        // OLED failed to init — persistent purple breathe as the out-of-band tell.
        float phase = sinf(now * TWO_PI / 1500.0f);
        uint8_t b = 6 + (uint8_t)(30.0f * (1.0f + phase) / 2.0f);
        for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(b, 0, b));
    } else {
        showIdleBreathe(now);
    }
    strip.show();
}

// ─── Mute button (BTN_MUTE) ──────────────────────────────────────────────────
// Optional tactile button to GND. Toggles bypass and panics all notes on entry
// so nothing hangs on the host. Encoder-tap on PG_LIVE does the same.
static void toggleBypass() {
    bypassed = !bypassed;
    if (bypassed) allNotesOffAllChannels();
    showToast(bypassed ? "MUTE" : "ACTIVE");
    oledDirty = true;
}

// Debounced press-edge detector for a button wired to GND. Returns true once
// per HIGH→LOW transition. Caller owns the persistent state (one per button).
// (BtnDebounce is declared up in the types section so Arduino's auto-generated
// prototype for this function — inserted near the top — can see it.)
static bool buttonPressed(uint8_t pin, BtnDebounce &b) {
    uint32_t now = millis();
    bool r = (digitalRead(pin) == HIGH);            // true = released (pull-up)
    if (r != b.raw) { b.raw = r; b.lastEdge = now; }
    if ((now - b.lastEdge) < BTN_DEBOUNCE_MS) return false;
    if (r == b.stable) return false;
    b.stable = r;
    return !r;                                       // just became pressed (LOW)
}

static void handleMuteButton() {
    static BtnDebounce b;
    if (buttonPressed(BTN_MUTE, b)) toggleBypass();
}

// ─── Sensor-source mode (BTN_MODE) ───────────────────────────────────────────
// Cycle which sensor(s) drive the output. Panic all channels on each switch so
// notes from the source we're leaving can't hang on the host.
static void cycleSensorSrc() {
    sensorSrc = (SensorSrc)((uint8_t)(sensorSrc + 1) % SRC_COUNT);
    allNotesOffAllChannels();
    showToast(SRC_NAME[sensorSrc]);
    oledDirty = true;
}

// Tap = cycle sensor source. Hold (≥ BTN_PAIR_HOLD_MS) = pair / unpair.
static void handleModeButton() {
    static bool     stable    = true;     // true = released (pull-up HIGH)
    static bool     raw       = true;
    static uint32_t lastEdge  = 0;
    static uint32_t pressTime = 0;
    static bool     longFired = false;
    uint32_t now = millis();
    bool     r   = (digitalRead(BTN_MODE) == HIGH);

    if (r != raw) { raw = r; lastEdge = now; }
    if ((now - lastEdge) >= BTN_DEBOUNCE_MS && raw != stable) {
        stable = raw;
        if (!stable)         { pressTime = now; longFired = false; }  // press edge
        else if (!longFired) cycleSensorSrc();                        // tap (released before hold)
    }
    if (!stable && !longFired && (now - pressTime) >= BTN_PAIR_HOLD_MS) {
        longFired = true;
        doPairToggle();         // long-hold, fires once
    }
}

// ─── Encoder + menu ──────────────────────────────────────────────────────────
// Pared down for a receiver: rotate or long-press = change page; short tap is
// page-contextual (LIVE = toggle bypass, INFO = reset counters).
static void resetCounters() {
    pktTotal = pktDropped = pktOverflow = pktForeign = 0;
    seqInit  = false;
    lastSeq  = 0;             // clear too, else the first post-reset packet phantom-drops
    rateWindowPkts  = 0;
    rateWindowStart = millis();
    lastRatePps     = 0;
    showToast("COUNTERS RST");
}

static void handleEncoder() {
    noInterrupts();
    int8_t delta = encDelta;
    encDelta = 0;
    interrupts();

    static bool     prev       = false;
    static uint32_t pressStart = 0;
    static bool     longFired  = false;

    bool     pushed = (digitalRead(ENC_PUSH) == LOW);
    uint32_t now    = millis();

    if (pushed && !prev) { pressStart = now; longFired = false; }

    if (pushed && !longFired && (now - pressStart) >= ENC_HOLD_MIN_MS) {
        longFired   = true;
        currentPage = (Page)((uint8_t)(currentPage + 1) % NUM_PAGES);
        oledDirty   = true;
    }

    if (!pushed && prev) {
        uint32_t held = now - pressStart;
        if (held < ENC_TAP_MAX_MS && !longFired) {
            switch (currentPage) {
                case PG_LIVE: toggleBypass();   break;
                case PG_INFO: resetCounters();  break;
            }
            oledDirty = true;
        }
    }
    prev = pushed;

    if (delta != 0) {
        if (delta > 0) currentPage = (Page)((uint8_t)(currentPage + 1) % NUM_PAGES);
        else           currentPage = (Page)((uint8_t)(currentPage + NUM_PAGES - 1) % NUM_PAGES);
        oledDirty = true;
    }
}

// ─── OLED drawing (patterns mirror v5) ───────────────────────────────────────
static void drawPageDots(uint8_t current, uint8_t total) {
    int16_t startX = OLED_W / 2 - (total * 8) / 2;
    for (uint8_t i = 0; i < total; i++) {
        int16_t dx = startX + i * 8;
        if (i == current) oled.fillRect(dx, 60, 5, 4, OLED_WHITE);
        else              oled.fillRect(dx + 1, 63, 3, 1, OLED_WHITE);
    }
}

static void drawHLine(int16_t y) { oled.drawFastHLine(0, y, OLED_W, OLED_WHITE); }

static void drawPageHeader(const char *title) {
    oled.fillRect(0, 0, OLED_W, 11, OLED_WHITE);
    oled.setTextColor(OLED_BLACK);
    oled.setCursor(4, 2);
    oled.print(title);
    oled.setTextColor(OLED_WHITE);
}

static int16_t drawBadge(int16_t x, int16_t y, const char *txt) {
    int16_t w = strlen(txt) * 6 + 4;
    oled.fillRoundRect(x, y, w, 9, 2, OLED_WHITE);
    oled.setTextColor(OLED_BLACK);
    oled.setCursor(x + 2, y + 1);
    oled.print(txt);
    oled.setTextColor(OLED_WHITE);
    return w;
}

// Dotted-when-empty, solid-when-filled segmented bar (v5 style).
static void drawSegBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t val, int16_t maxVal) {
    int16_t fillW = constrain((int32_t)val * w / maxVal, 0, w);
    for (int16_t sx = 0; sx < w; sx += 4) {
        if (sx < fillW) {
            int16_t fw = (sx + 3 <= fillW) ? 3 : (fillW - sx);
            if (fw > 0) oled.fillRect(x + sx, y, fw, h, OLED_WHITE);
        } else if ((sx / 4) % 2 == 0) {
            oled.drawPixel(x + sx, y + h / 2, OLED_WHITE);
        }
    }
}

static const char *midiCmdName(uint8_t status) {
    switch (status & 0xF0) {
        case 0x80: return "OFF"; case 0x90: return "ON";  case 0xA0: return "AT";
        case 0xB0: return "CC";  case 0xC0: return "PC";  case 0xD0: return "CP";
        case 0xE0: return "PB";  default:   return "--";
    }
}

static void drawPageLive() {
    drawPageHeader("RECEIVER");
    uint32_t now = millis();
    bool linkUp = linkAlive(now);

    // Active sensor source, in the header gap between the title and the status
    // badge (black text on the white header bar). Always visible so the
    // performer knows which sensor(s) are live.
    oled.setTextColor(OLED_BLACK);
    oled.setCursor(58, 2);
    oled.print(SRC_NAME[sensorSrc]);   // BOTH / GTR / LOC
    oled.setTextColor(OLED_WHITE);

    // Header badge (right-aligned): MUTE (safety-critical) > link state.
    if (bypassed)    drawBadge(OLED_W - 30, 0, "MUTE");
    else if (linkUp) drawBadge(OLED_W - 22, 0, "RX");
    else             drawBadge(OLED_W - 36, 0, "NOSIG");

    // Last received MIDI (from the guitar).
    oled.setCursor(0, 14);
    if (seqInit) {
        oled.printf("%-3s ch%-2u %3u %3u",
                    midiCmdName(lastStatus),
                    (unsigned)((lastStatus & 0x0F) + 1),
                    (unsigned)lastData1, (unsigned)lastData2);
    } else {
        oled.print("no ESP-NOW link yet");
    }
    oled.setCursor(0, 24);
    oled.printf("%lu pps", (unsigned long)lastRatePps);
    oled.setCursor(60, 24);
    if (expLive) oled.printf("EXP %3u", (unsigned)expCC);   // pedal plugged in and moved
    else         oled.printf("%lu pkt", (unsigned long)pktTotal);

    drawHLine(34);

    // Local VL53L5CX gesture zone.
    if (!sensorOK) {
        oled.setCursor(0, 40);
        oled.print("local ToF: none");
    } else {
        int16_t barX = 26, barW = 70, barH = 7;
        oled.setCursor(0, 38);  oled.print("DST");
        drawSegBar(barX, 38, barW, barH, distCC, 127);
        oled.setCursor(100, 38); oled.printf("%3u", (unsigned)distCC);

        oled.setCursor(0, 50);  oled.print("VEL");
        drawSegBar(barX, 50, barW, barH, velCC, 127);
        oled.setCursor(100, 50); oled.printf("%3u", (unsigned)velCC);
    }

    drawPageDots(0, NUM_PAGES);
}

static void drawPageInfo() {
    drawPageHeader("INFO");

    oled.setCursor(0, 14);
    oled.printf("Me %02X:%02X:%02X:%02X:%02X:%02X",
                selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);

    // Bond line: paired MAC + state, else the guitar currently heard (open).
    oled.setCursor(0, 24);
    if (paired) {
        oled.printf("Pair%02X%02X%02X%02X%02X%02X*",
                    pairedMac[0], pairedMac[1], pairedMac[2],
                    pairedMac[3], pairedMac[4], pairedMac[5]);
    } else if (seqInit) {
        oled.printf("open %02X%02X%02X%02X%02X%02X",
                    lastPeerMac[0], lastPeerMac[1], lastPeerMac[2],
                    lastPeerMac[3], lastPeerMac[4], lastPeerMac[5]);
    } else {
        oled.print("open (no guitar)");
    }

    oled.setCursor(0, 36);  oled.printf("pkts %lu", (unsigned long)pktTotal);
    oled.setCursor(72, 36); oled.printf("drop %lu", (unsigned long)pktDropped);
    oled.setCursor(0, 46);  oled.printf("frgn %lu", (unsigned long)pktForeign);
    oled.setCursor(72, 46); oled.printf("heap%uk", (unsigned)(ESP.getFreeHeap() / 1024));

    oled.setCursor(0, 54);
    oled.printf("%s  hold MODE=pair", paired ? "PAIRED" : "OPEN");

    drawPageDots(1, NUM_PAGES);
}

static void refreshOLED() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(OLED_WHITE);
    switch (currentPage) {
        case PG_LIVE: drawPageLive(); break;
        case PG_INFO: drawPageInfo(); break;
    }
    if (toastText && millis() < toastUntil) {
        int16_t tw = strlen(toastText) * 6 + 8;
        int16_t tx = (OLED_W - tw) / 2;
        oled.fillRoundRect(tx, 24, tw, 16, 3, OLED_WHITE);
        oled.setTextColor(OLED_BLACK);
        oled.setCursor(tx + 4, 28);
        oled.print(toastText);
        oled.setTextColor(OLED_WHITE);
    }
    oled.display();
    oledDirty = false;
}

// ─── Intro ─ OLED title sweep + NeoPixel chase (v5 flavour, 8-px stick) ──────
static void playIntro() {
    uint16_t hue = 32000;   // cyan-ish
    oled.clearDisplay();
    oled.display();
    for (int16_t x = 0; x < OLED_W; x += 4) {
        oled.drawLine(0, 31, x, 31, OLED_WHITE);
        oled.display();
        strip.clear();
        strip.setPixelColor((x / 4) % NUM_PIXELS, strip.ColorHSV(hue, 255, 120));
        strip.show();
    }
    const char *title = "R E C E I V E R";
    uint8_t len = strlen(title);
    int16_t startX = (OLED_W - len * 6) / 2;
    oled.setTextSize(1);
    oled.setTextColor(OLED_WHITE);
    for (uint8_t i = 0; i < len; i++) {
        oled.setCursor(startX + i * 6, 24);
        oled.print(title[i]);
        if (title[i] != ' ') {
            oled.display();
            delay(30);
            strip.fill(strip.ColorHSV(hue, 200, 40 + (i % 3) * 30));
            strip.show();
        }
    }
    oled.setCursor((OLED_W - 12) / 2, 40);
    oled.print("v6");
    oled.display();
    delay(350);
    strip.clear();
    strip.show();
}

// ─── Debug heartbeat ─────────────────────────────────────────────────────────
// One throttled status line covering the whole pipeline: ESP-NOW link health,
// last received MIDI, the local sensor's CC outputs, mute, page, and free heap.
// The single most useful bench probe — replaces guessing with a live readout.
#if DEBUG
static void debugHeartbeat(uint32_t now) {
    if (DBG_PERIOD_MS == 0) return;
    static uint32_t tDbg = 0;
    if ((now - tDbg) < DBG_PERIOD_MS) return;
    tDbg = now;

    bool linkUp = linkAlive(now);
    uint32_t age = (seqInit && (int32_t)(now - lastRxMs) > 0) ? (uint32_t)(now - lastRxMs) : 0;
    uint8_t wch = 0; wifi_second_chan_t wsec; esp_wifi_get_channel(&wch, &wsec);

    Serial.printf(
        "[hb] wch=%u %s src=%s link=%s %lupps pkt=%lu drop=%lu frgn=%lu age=%lums | "
        "rx=%s ch%u %u,%u | tof=%s d=%u v=%u | exp=%s %u | %s pg=%u heap=%uk\n",
        (unsigned)wch,
        paired ? "PAIRED" : "open",
        SRC_NAME[sensorSrc],
        linkUp ? "UP" : "down",
        (unsigned long)lastRatePps, (unsigned long)pktTotal,
        (unsigned long)pktDropped, (unsigned long)pktForeign,
        (unsigned long)age,
        seqInit ? midiCmdName(lastStatus) : "--",
        (unsigned)((lastStatus & 0x0F) + 1),
        (unsigned)lastData1, (unsigned)lastData2,
        sensorOK ? (tofValid ? "hand" : "idle") : "none",
        (unsigned)distCC, (unsigned)velCC,
        expLive ? "on" : "off", (unsigned)expCC,
        bypassed ? "MUTE" : "live",
        (unsigned)currentPage,
        (unsigned)(ESP.getFreeHeap() / 1024));
}
#endif

// ─── Setup / loop ────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
#if DEBUG
    delay(BOOT_SERIAL_WAIT_MS);
    Serial.println("\n[boot] tof_midi_v6_rx");
#endif

    pinMode(ENC_A,    INPUT_PULLUP);
    pinMode(ENC_B,    INPUT_PULLUP);
    pinMode(ENC_PUSH, INPUT_PULLUP);
    pinMode(BTN_MUTE, INPUT_PULLUP);
    pinMode(BTN_MODE, INPUT_PULLUP);
    initExp();
    attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);

    // Bus at 100 kHz for reliable OLED init on a marginal clone panel.
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
    delay(50);

    // Warm-up scan — these clone SSD1306s reject their init burst without some
    // dummy I²C traffic first. Load-bearing; keep it even with DEBUG off.
    uint8_t i2cFound = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            i2cFound++;
#if DEBUG
            Serial.printf("[I2C] 0x%02X ACK\n", a);
#endif
        }
    }
#if DEBUG
    Serial.printf("[I2C] %u device(s)\n", i2cFound);
#endif

#if OLED_SH1106
    // SH110X drives the SH1106 correctly: begin() enables the right charge pump
    // (→ full brightness) and uses the 132-col addressing with the 2-px offset
    // handled (→ no line artifacts). No raw command hacks needed.
    oledOK = oled.begin(OLED_ADDR, true);    // i2c addr; reset arg is a no-op (no rst pin)
    oled.setContrast(OLED_CONTRAST);
    oled.setRotation(2);                     // = the 180° (FLIP_V+FLIP_H) mounting you
                                             // had on SSD1306; change to 0 if upside-down
#else
    // Standard SSD1306 init: charge pump on, contrast + VCOMH maxed for brightness,
    // then the hardware orientation flips. No SH1106 (0xAD/0x8B) hacks.
    oledOK = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    oled.setRotation(OLED_ROTATION);
    oled.ssd1306_command(SSD1306_CHARGEPUMP);     oled.ssd1306_command(0x14);
    oled.ssd1306_command(SSD1306_SETCONTRAST);    oled.ssd1306_command(OLED_CONTRAST);
    oled.ssd1306_command(SSD1306_SETVCOMDETECT);  oled.ssd1306_command(OLED_VCOMH);
    oled.ssd1306_command(SSD1306_DISPLAYON);
    oled.ssd1306_command(OLED_FLIP_H ? 0xA0 : 0xA1);
    oled.ssd1306_command(OLED_FLIP_V ? 0xC0 : 0xC8);
#endif
#if DEBUG
    Serial.println(oledOK ? "[boot] OLED OK" : "[boot] OLED FAIL (no ACK at 0x3C)");
#endif

    initNeoPixel();
    playIntro();           // runs even if the OLED is dead — the ring is an
                           // independent "alive" signal (and the idle ring turns
                           // purple when oledOK is false, see updateNeoPixel)

    // USB-MIDI. Deliberately NO USB.enableDFU() — the extra DFU interface in the
    // composite (CDC+DFU+MIDI) can make a host enumerate the serial port but skip
    // the MIDI interface (device shows up for Serial but not in Audio MIDI Setup /
    // Ableton). Dropping it gives a clean CDC+MIDI device. You can still flash
    // normally over the serial bootloader; you just lose USB-DFU flashing.
    usbMidi.begin();
    USB.begin();
#if HW_MIDI_OUT
    Serial1.begin(31250, SERIAL_8N1, -1, MIDI_TX_PIN);   // TX only, DIN MIDI OUT (J5)
#endif

    // ESP-NOW after USB so a noisy radio start can't disturb USB enumeration.
    loadPairing();    // restore the bonded guitar MAC before the recv cb can fire
    bool nowOK = initEspNow();
#if DEBUG
    Serial.printf("[boot] pairing %s\n", paired ? "PAIRED" : "open");
    Serial.println(nowOK ? "[boot] ESP-NOW OK" : "[boot] ESP-NOW FAIL");
    Serial.printf("[boot] MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                  selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);
#endif
    if (!nowOK) showToast("ESPNOW FAIL");

#if USE_LOCAL_TOF
    // VL53L5CX bring-up. begin() uploads an ~84 KB firmware blob (~1 s at
    // 400 kHz), and runs BEFORE the WDT is armed, so it's safe.
    sensorOK = initToF();
#if DEBUG
    Serial.println(sensorOK ? "[boot] VL53L5CX OK" : "[boot] VL53L5CX FAIL");
#endif
    if (!sensorOK) showToast("NO ToF");
#endif // USE_LOCAL_TOF

    // 10 s task WDT. ESP-NOW recv runs on the WiFi task (already IDF-monitored),
    // so only loop() needs registering. Core 3.x pre-inits the TWDT, so we
    // reconfigure to our 10 s rather than re-init (which would no-op).
    const esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = 10000,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_reconfigure(&wdtCfg);
    esp_task_wdt_add(nullptr);

    rateWindowStart = millis();
    oledDirty = true;
}

void loop() {
    esp_task_wdt_reset();
    const uint32_t now = millis();

    handleEncoder();
    handleMuteButton();
    handleModeButton();

    pollToF();
    sendLocalSensorMidi();
    pollExp(now);
    drainRx();
    updateNeoPixel();

#if DEBUG
    debugHeartbeat(now);
#endif

    // Rolling pps over RATE_WINDOW_MS (display only; a one-tick race is invisible).
    if ((int32_t)(now - rateWindowStart) >= (int32_t)RATE_WINDOW_MS) {
        uint32_t total  = pktTotal;
        uint32_t inWin  = total - rateWindowPkts;
        uint32_t winMs  = (now - rateWindowStart);
        lastRatePps     = (winMs > 0) ? (inWin * 1000UL / winMs) : 0;
        rateWindowPkts  = total;
        rateWindowStart = now;
        oledDirty = true;
    }

    // LIVE page is value-driven (link + local sensor), so refresh it freely; the
    // throttle below caps the actual redraw rate.
    if (currentPage == PG_LIVE) oledDirty = true;

    if (toastText) {
        if (now < toastUntil) oledDirty = true;
        else                  toastText = nullptr;
    }

    if (oledDirty && (now - tOled) >= OLED_REFRESH_MS) {
        tOled = now;
        refreshOLED();
    }
}

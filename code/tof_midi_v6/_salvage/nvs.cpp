#include "settings/nvs.h"
#include "config.h"
#include "util/wrap.h"
#include <Preferences.h>

// ─── Runtime state defined in globals.h ──────────────────────────────────────
uint8_t  midiCh      = MIDI_CH_DEFAULT;
uint8_t  ccDist      = CC_DIST_DEFAULT;
uint8_t  ccVel       = CC_VEL_DEFAULT;
uint8_t  ccCentroidX = CC_CENTROID_X_DEFAULT;
uint8_t  ccWarmth    = CC_WARMTH_DEFAULT;
uint8_t  ccPeak      = CC_PEAK_DEFAULT;
int16_t  distMinMm   = DIST_MIN_DEFAULT;
int16_t  distMaxMm   = DIST_MAX_DEFAULT;
uint8_t  curve       = CURVE_DEFAULT;
uint8_t  bright      = BRIGHT_DEFAULT;
bool     distInvert  = false;
uint8_t  trigThresh    = TRIG_THRESH_DEFAULT;
uint8_t  releaseThresh = RELEASE_THRESH_DEFAULT;
uint8_t  velFloor      = VEL_FLOOR_DEFAULT;
uint8_t  drumGateMs    = DRUM_GATE_MS_DEFAULT;

HeatMode mode      = MODE_DRONE;
bool     bypassed  = false;
bool     sensorOK  = false;
bool     configMode = false;

int16_t  liveDist = 0, liveVel = 0;
int16_t  liveCentroidX = -1, liveCentroidY = -1, liveWarmth = 0, livePeak = 0;

bool          oledDirty = true;
unsigned long dirtyTime = 0;
bool          settingsDirty = false;

static Preferences prefs;
const char *const NVS_NS = "heat";

void markSettingsDirty() { settingsDirty = true; dirtyTime = millis(); }

// onChange callbacks ----------------------------------------------------------
static void onBrightChange() {
    // Display module owns the backlight; declare extern to avoid include cycle.
    extern void screenSetBrightness(uint8_t b);
    screenSetBrightness(bright);
}

// ─── SETTINGS table ──────────────────────────────────────────────────────────
const Setting SETTINGS[NUM_SETTINGS] = {
    { "ch",   "MIDI Ch",  "midiCh",  T_U8,    &midiCh,      1, 16,                   1, nullptr        },
    { "ccd",  "CC Dist",  "ccDist",  T_U8,    &ccDist,      0, 127,                  1, nullptr        },
    { "ccv",  "CC Vel",   "ccVel",   T_U8,    &ccVel,       0, 127,                  1, nullptr        },
    { "ccx",  "CC CtrX",  "ccCx",    T_U8,    &ccCentroidX, 0, 127,                  1, nullptr        },
    { "ccw",  "CC Warm",  "ccWm",    T_U8,    &ccWarmth,    0, 127,                  1, nullptr        },
    { "ccp",  "CC Peak",  "ccPk",    T_U8,    &ccPeak,      0, 127,                  1, nullptr        },
    { "dmin", "Dist Min", "distMin", T_I16,   &distMinMm,   DIST_MIN_LO, DIST_MIN_HI, 5, nullptr        },
    { "dmax", "Dist Max", "distMax", T_I16,   &distMaxMm,   DIST_MAX_LO, DIST_MAX_HI,10, nullptr        },
    { "cv",   "Curve",    "curve",   T_CURVE, &curve,       0, 2,                    1, nullptr        },
    { "brt",  "Bright",   "bright",  T_U8,    &bright,      5, 255,                  5, onBrightChange },
    { "inv",  "Invert",   "invert",  T_BOOL,  &distInvert,    0, 1,                  1, nullptr        },
    { "trig", "Trig",     "trig",    T_U8,    &trigThresh,    5, 127,                5, nullptr        },
    { "rls",  "Release",  "rls",     T_U8,    &releaseThresh, 0, 30,                 2, nullptr        },
    { "vfl",  "VelFlr",   "vfl",     T_U8,    &velFloor,      0, 64,                 4, nullptr        },
    { "gms",  "GateMs",   "gms",     T_U8,    &drumGateMs,   10, 200,               10, nullptr        },
};
static_assert(sizeof(SETTINGS)/sizeof(SETTINGS[0]) == NUM_SETTINGS,
              "NUM_SETTINGS must equal SETTINGS table length");

// ─── Getters / setters ──────────────────────────────────────────────────────
int32_t settingGet(const Setting &s) {
    switch (s.type) {
        case T_U8: case T_CURVE: return *(uint8_t*)s.slot;
        case T_I16:              return *(int16_t*)s.slot;
        case T_BOOL:             return *(bool*)s.slot ? 1 : 0;
    }
    return 0;
}

bool settingSet(const Setting &s, int32_t v) {
    bool changed = false;
    switch (s.type) {
        case T_U8: case T_CURVE: {
            uint8_t n = (uint8_t)constrain((int)v, s.lo, s.hi);
            if (n != *(uint8_t*)s.slot) { *(uint8_t*)s.slot = n; changed = true; }
            break;
        }
        case T_I16: {
            int16_t n = (int16_t)constrain((int)v, s.lo, s.hi);
            if (n != *(int16_t*)s.slot) { *(int16_t*)s.slot = n; changed = true; }
            break;
        }
        case T_BOOL: {
            bool n = (v != 0);
            if (n != *(bool*)s.slot) { *(bool*)s.slot = n; changed = true; }
            break;
        }
    }
    if (changed && s.onChange) s.onChange();
    return changed;
}

bool settingAdjust(const Setting &s, int8_t dir) {
    if (s.type == T_BOOL)  return settingSet(s, !*(bool*)s.slot);
    if (s.type == T_CURVE) return settingSet(s, wrap_u8(*(uint8_t*)s.slot + dir, 3));
    return settingSet(s, settingGet(s) + dir * s.step);
}

void settingFormat(const Setting &s, char *out, size_t n) {
    if (s.type == T_BOOL) {
        snprintf(out, n, "%s", *(bool*)s.slot ? "ON" : "OFF");
    } else if (s.type == T_CURVE) {
        static const char *c[] = { "LIN", "LOG", "EXP" };
        uint8_t v = *(uint8_t*)s.slot;
        snprintf(out, n, "%s", v < 3 ? c[v] : "?");
    } else {
        snprintf(out, n, "%d", (int)settingGet(s));
    }
}

const Setting *settingByKey(const char *k) {
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        if (strcmp(SETTINGS[i].jsonKey, k) == 0) return &SETTINGS[i];
    }
    return nullptr;
}

// ─── (De)serialise ───────────────────────────────────────────────────────────
size_t settingsToBytes(uint8_t *buf, size_t cap) {
    size_t off = 0;
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        size_t need = (s.type == T_I16) ? 2 : 1;
        if (off + need > cap) return 0;
        int32_t v = settingGet(s);
        buf[off++] = (uint8_t)(v & 0xFF);
        if (s.type == T_I16) buf[off++] = (uint8_t)((v >> 8) & 0xFF);
    }
    return off;
}

size_t settingsFromBytes(const uint8_t *buf, size_t len) {
    size_t off = 0;
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        size_t need = (s.type == T_I16) ? 2 : 1;
        if (off + need > len) break;
        int32_t v = buf[off++];
        if (s.type == T_I16) v = (int16_t)((uint16_t)v | ((uint16_t)buf[off++] << 8));
        settingSet(s, v);
    }
    return off;
}

// ─── NVS lifecycle ───────────────────────────────────────────────────────────
void loadSettings() {
    prefs.begin(NVS_NS, true);
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        switch (s.type) {
            case T_U8: case T_CURVE: *(uint8_t*)s.slot = prefs.getUChar(s.nvsKey, *(uint8_t*)s.slot); break;
            case T_I16:              *(int16_t*)s.slot = prefs.getShort(s.nvsKey, *(int16_t*)s.slot); break;
            case T_BOOL:             *(bool*)s.slot    = prefs.getBool (s.nvsKey, *(bool*)s.slot); break;
        }
    }
    uint8_t md = prefs.getUChar("mode", MODE_DRONE);
    mode = (md < MODE_COUNT) ? (HeatMode)md : MODE_DRONE;
    prefs.end();
}

void saveSettings() {
    prefs.begin(NVS_NS, false);
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const Setting &s = SETTINGS[i];
        switch (s.type) {
            case T_U8: case T_CURVE: prefs.putUChar(s.nvsKey, *(uint8_t*)s.slot); break;
            case T_I16:              prefs.putShort(s.nvsKey, *(int16_t*)s.slot); break;
            case T_BOOL:             prefs.putBool (s.nvsKey, *(bool*)s.slot); break;
        }
    }
    prefs.putUChar("mode", (uint8_t)mode);
    prefs.end();
    settingsDirty = false;
}

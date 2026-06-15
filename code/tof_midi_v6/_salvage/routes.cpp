#include "mapping/routes.h"
#include "globals.h"
#include "settings/nvs.h"
#include "output/output_mux.h"
#include "sensors/tof_grid.h"
#include "sensors/thermal.h"

static int16_t prevDist = -1, prevVel = -1;
static int16_t prevCx = -1, prevWarm = -1, prevPeak = -1;

static void cc(uint8_t cc_num, uint8_t val, int16_t &prev) {
    if (val == prev) return;
    prev = val;
    outControlChange(cc_num, val);
}

static int8_t   heldNote    = -1;
static int16_t  triggerPrev = 0;
static uint32_t gateUntilMs = 0;

static const uint8_t PENTATONIC[10] = { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22 };
static const uint8_t DRUM_NOTES[4]  = { 36, 38, 42, 45 };

// trigThresh / releaseThresh / velFloor / drumGateMs are NVS settings —
// stage-tunable via encoder or web UI without reflashing.
static constexpr uint8_t  DRONE_ROOT  = 48;     // C3 — fits inside CV's 4-octave range
static constexpr uint8_t  RHYTHM_ROOT = 48;
static constexpr uint8_t  DRONE_RANGE = 24;

static inline uint8_t binByCentroid(int16_t x, uint8_t n) {
    int v = ((int)x * n) / 128;
    if (v < 0) return 0;
    if (v >= n) return n - 1;
    return (uint8_t)v;
}

static void releaseHeld() {
    if (heldNote < 0) return;
    outNoteOff((uint8_t)heldNote);
    outCvGate(false);
    heldNote = -1;
}

void routesNotifyModeChange() {
    releaseHeld();
    triggerPrev = 0;
    gateUntilMs = 0;
}

static void droneTick() {
    if (heldNote < 0) {
        outNoteOn(DRONE_ROOT, 80);
        outCvPitch(DRONE_ROOT);
        outCvGate(true);
        heldNote = (int8_t)DRONE_ROOT;
    }
    uint8_t pitch = DRONE_ROOT + binByCentroid(liveCentroidY, DRONE_RANGE);
    outCvPitch(pitch);
    // CentroidX 0..127 → bend 0..16256, centred 8192 at cx=64.
    outPitchBend((uint16_t)(8192 + ((int)liveCentroidX - 64) * 128));
}

static void rhythmTick(int16_t vel) {
    bool rising  = (triggerPrev < trigThresh    && vel >= trigThresh);
    bool falling = (vel        < releaseThresh);
    if (heldNote < 0 && rising) {
        uint8_t n = RHYTHM_ROOT + PENTATONIC[binByCentroid(liveCentroidX, 10)];
        uint8_t v = (uint8_t)constrain((int)vel + velFloor, 1, 127);
        outNoteOn(n, v);
        outCvPitch(n);
        outCvGate(true);
        heldNote = (int8_t)n;
    } else if (heldNote >= 0 && falling) {
        releaseHeld();
    }
}

static void drumsTick(int16_t vel, uint32_t now) {
    bool rising = (triggerPrev < trigThresh && vel >= trigThresh);
    if (heldNote < 0 && rising) {
        uint8_t n = DRUM_NOTES[binByCentroid(liveCentroidX, 4)];
        uint8_t v = (uint8_t)constrain((int)vel + velFloor, 1, 127);
        outNoteOn(n, v);
        outCvPitch(n);
        outCvGate(true);
        heldNote = (int8_t)n;
        gateUntilMs = now + drumGateMs;
    }
    if (heldNote >= 0 && now >= gateUntilMs) releaseHeld();
}

void routesUpdate() {
    bool tofFresh     = tofDataFresh();
    bool thermalFresh = thermalFrameFresh();

    if (tofFresh) {
        cc(ccDist, (uint8_t)liveDist, prevDist);
        cc(ccVel,  (uint8_t)liveVel,  prevVel);
        outCvModB((uint8_t)liveDist);
    }
    if (thermalFresh) {
        cc(ccCentroidX, (uint8_t)liveCentroidX, prevCx);
        cc(ccWarmth,    (uint8_t)liveWarmth,    prevWarm);
        cc(ccPeak,      (uint8_t)livePeak,      prevPeak);
        outCvModA((uint8_t)liveWarmth);
    }

    // Under bypass: hold no MIDI/CV state, drop the gate, leak no note-on
    // intent. RHYTHM/DRUMS used to commit heldNote even when outNoteOn was
    // silently dropped, causing CV-gate / MIDI desync on un-mute.
    if (bypassed) {
        if (heldNote >= 0) releaseHeld();
        triggerPrev = liveVel;
        return;
    }

    // Skip per-mode tick when nothing relevant changed AND a held note
    // is already alive. heldNote < 0 keeps DRONE's bootstrap path live
    // after a mode change (otherwise the drone is silent for ~125 ms).
    if (!tofFresh && !thermalFresh && heldNote >= 0 && mode != MODE_DRUMS) return;

    int16_t vel = liveVel;
    switch (mode) {
        case MODE_DRONE:  droneTick();             break;
        case MODE_RHYTHM: rhythmTick(vel);         break;
        case MODE_DRUMS:  drumsTick(vel, millis()); break;
        default: break;
    }
    triggerPrev = vel;
}

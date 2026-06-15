#include "sensors/tof_grid.h"
#include "config.h"
#include "globals.h"
#include "util/ema.h"
#include "util/rate_limit.h"
#include "util/debug.h"
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>
#include <math.h>

static SparkFun_VL53L5CX sensor;
static VL53L5CX_ResultsData results;
static bool sensorReady = false;
static bool freshFrame  = false;
// VL53L5CX runs at 15 Hz (8×8). Polling isDataReady at the full loop rate
// burns ~10-50k I²C transactions/sec for ~15 useful frames. Cap at 10 ms
// so the bus stays free for MLX90640 + MCP4728.
static RateLimit pollLimiter(10);

static Ema centreEma(EMA_ALPHA);
static float prevCentre = -1.0f;

static inline bool zoneValid(uint8_t status) {
    return status == TOF_VALID_S1 || status == TOF_VALID_S2;
}

static float applyCurve(float t) {
    t = constrain(t, 0.0f, 1.0f);
    switch (curve) {
        case CURVE_LOG: return log1pf(t * (M_E - 1.0f));
        case CURVE_EXP: return (expf(t) - 1.0f) / (M_E - 1.0f);
        default:        return t;
    }
}

bool tofBegin() {
    DBGLN("[tof] init VL53L5CX ...");
    Wire.beginTransmission(VL53L5_ADDR);
    if (Wire.endTransmission() != 0) {
        DBGLN("[tof] no ACK at 0x29");
        return false;
    }
    // begin() uploads ~85 KB firmware blob over I²C — takes ~1 s. The
    // 10 s task WDT covers it.
    if (!sensor.begin(VL53L5_ADDR, Wire)) {
        DBGLN("[tof] begin failed");
        return false;
    }
    sensor.setResolution(TOF_GRID_SIDE * TOF_GRID_SIDE);
    sensor.setRangingFrequency(TOF_GRID_SIDE == 4 ? 30 : 15);
    sensor.setSharpenerPercent(20);   // default 5% smears hand zones <100 mm
    sensor.setTargetOrder(SF_VL53L5CX_TARGET_ORDER::CLOSEST);   // hand wins over back wall
    sensor.startRanging();
    sensorReady = true;
    return true;
}

static void extractFeatures() {
    const uint8_t side = TOF_GRID_SIDE;
    uint8_t cLo = (side - TOF_CENTER_CROP) / 2;
    uint8_t cHi = cLo + TOF_CENTER_CROP;

    uint32_t sum = 0;
    uint16_t cnt = 0;
    for (uint8_t y = cLo; y < cHi; y++) {
        for (uint8_t x = cLo; x < cHi; x++) {
            uint16_t idx = y * side + x;
            if (!zoneValid(results.target_status[idx])) continue;
            sum += results.distance_mm[idx];
            cnt++;
        }
    }
    if (cnt == 0) return;

    float mean = (float)sum / (float)cnt;
    prevCentre = centreEma.value;
    float ema  = centreEma.update(mean);

    float curved = applyCurve((ema - distMinMm) / float(distMaxMm - distMinMm));
    if (distInvert) curved = 1.0f - curved;
    liveDist = (int16_t)constrain((int)lroundf(curved * 127.0f), 0, 127);

    if (prevCentre > 0.0f) {
        float d = fabsf(prevCentre - ema);
        if (d < VEL_DEAD_MM) d = 0.0f;
        liveVel = (int16_t)lroundf(constrain(d / VEL_MAX_MM * 127.0f, 0.0f, 127.0f));
    }
}

void tofUpdate(uint32_t now_ms) {
    freshFrame = false;
    if (!sensorReady) return;
    if (!pollLimiter.ready(now_ms)) return;
    if (!sensor.isDataReady()) return;
    if (!sensor.getRangingData(&results)) return;
    extractFeatures();
    freshFrame = true;
}

bool tofDataFresh() { return freshFrame; }

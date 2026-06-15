#include "mapping/modes.h"
#include "mapping/routes.h"
#include "output/output_mux.h"
#include "settings/nvs.h"
#include "util/wrap.h"

void modesCycle(int8_t dir) {
    HeatMode next = (HeatMode)wrap_u8((int8_t)mode + dir, MODE_COUNT);
    modesSet(next);
}

void modesSet(HeatMode m) {
    if (m == mode) return;
    // Silence host + clear held route state. Different modes own
    // different notes; carryover causes stuck notes.
    outAllNotesOff();
    routesNotifyModeChange();
    mode = m;
    markSettingsDirty();
    oledDirty = true;
    showToast(modesName(m));
}

const char *modesName(HeatMode m) {
    switch (m) {
        case MODE_DRONE:  return "DRONE";
        case MODE_RHYTHM: return "RHYTHM";
        case MODE_DRUMS:  return "DRUMS";
        default:          return "?";
    }
}

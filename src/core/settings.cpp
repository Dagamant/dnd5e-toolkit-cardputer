#include "settings.h"
#include "../board/board.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <algorithm>

namespace settings {

Settings cur;

void load() {
    Preferences p;
    p.begin("gmtools", true);
    cur.volume = p.getUChar("volume", cur.volume);
    cur.brightness = p.getUChar("bright", cur.brightness);
    cur.sound = p.getBool("sound", cur.sound);
    cur.shakeToRoll = p.getBool("shake", cur.shakeToRoll);
    cur.logRolls = p.getBool("logrolls", cur.logRolls);
    cur.launcherList = p.getBool("launchlist", cur.launcherList);
    cur.screenTimeout = p.getUShort("scrtimeout", cur.screenTimeout);
    cur.screenTimeout = TIMEOUTS[timeoutIndex()];
    cur.kbLights = std::min<uint8_t>(2, p.getUChar("kblights", cur.kbLights));
    p.getString("lastapp", cur.lastApp, sizeof(cur.lastApp));
    p.end();
}

void save() {
    Preferences p;
    p.begin("gmtools", false);
    p.putUChar("volume", cur.volume);
    p.putUChar("bright", cur.brightness);
    p.putBool("sound", cur.sound);
    p.putBool("shake", cur.shakeToRoll);
    p.putBool("logrolls", cur.logRolls);
    p.putUShort("scrtimeout", cur.screenTimeout);
    p.putUChar("kblights", cur.kbLights);
    p.end();
}

void saveLauncher() {
    Preferences p;
    p.begin("gmtools", false);
    p.putBool("launchlist", cur.launcherList);
    p.putString("lastapp", cur.lastApp);
    p.end();
}

int timeoutIndex() {
    if (!cur.screenTimeout) return NTIMEOUTS - 1;
    int best = 0;
    for (int i = 1; i < NTIMEOUTS - 1; i++)
        if (abs(TIMEOUTS[i] - cur.screenTimeout) < abs(TIMEOUTS[best] - cur.screenTimeout)) best = i;
    return best;
}

const char* timeoutLabel(uint16_t s) {
    static char buf[12];
    if (!s) return "Never";
    if (s < 60) snprintf(buf, sizeof(buf), "%u sec", s);
    else snprintf(buf, sizeof(buf), "%u min", s / 60);
    return buf;
}

void apply() {
    M5.Display.setBrightness(cur.brightness);
    M5.Speaker.setVolume(cur.sound ? cur.volume : 0);
#ifdef BOARD_TAB5
    board::keyboardLights(cur.kbLights);
#endif
}

}  // namespace settings

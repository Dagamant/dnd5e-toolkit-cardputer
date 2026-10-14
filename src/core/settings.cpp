#include "settings.h"
#include <M5Unified.h>
#include <Preferences.h>

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
    p.end();
}

void saveLauncher() {
    Preferences p;
    p.begin("gmtools", false);
    p.putBool("launchlist", cur.launcherList);
    p.putString("lastapp", cur.lastApp);
    p.end();
}

void apply() {
    M5.Display.setBrightness(cur.brightness);
    M5.Speaker.setVolume(cur.sound ? cur.volume : 0);
}

}  // namespace settings

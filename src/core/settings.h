#pragma once
#include <stdint.h>

struct Settings {
    uint8_t volume = 140;       // 0-255
    uint8_t brightness = 160;   // 0-255
    bool sound = true;
    bool shakeToRoll = true;
    bool logRolls = true;       // append rolls to SD session log
    bool launcherList = false;  // home screen as a list instead of the carousel
    char lastApp[20] = "";      // the home screen starts on this app
};

namespace settings {
extern Settings cur;
void load();
void save();
void saveLauncher();   // just the home screen fields (written when they change)
void apply();  // push brightness/volume to hardware
}  // namespace settings

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
    uint16_t screenTimeout = 120; // seconds without input before the screen goes dark; 0 never
    uint8_t kbLights = 2;       // Tab5 keyboard LEDs: 0 off, 1 dim, 2 normal
};

namespace settings {
extern Settings cur;
void load();
void save();
void saveLauncher();   // just the home screen fields (written when they change)
void apply();  // push brightness/volume to hardware

// Screen timeout choices, in seconds; 0 (last) is never
constexpr uint16_t TIMEOUTS[] = {30, 60, 120, 180, 300, 600, 900, 0};
constexpr int NTIMEOUTS = sizeof(TIMEOUTS) / sizeof(TIMEOUTS[0]);
int timeoutIndex();            // the choice nearest the current setting
const char* timeoutLabel(uint16_t s);   // "30 sec", "2 min", "Never"

constexpr const char* KB_LIGHTS[] = {"Off", "Dim", "Normal"};
}  // namespace settings

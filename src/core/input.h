#pragma once
#include <Arduino.h>

// Edge-triggered key events with auto-repeat for held keys. The board layer
// (src/board) reports which keys are down and what each one means.
//
// Navigation convention used across all apps:
//   Arrow keys = real arrows, or on the Cardputer the ; . , / keys (printed
//                arrows); in text-entry screens hold Fn for arrows there.
//   Esc        = Esc, or ` on the Cardputer
//   Enter      = OK / confirm
namespace input {

enum Key : uint8_t {
    K_NONE = 0,
    K_UP = 1, K_DOWN, K_LEFT, K_RIGHT,
    K_ENTER, K_BACK /* esc */, K_DEL, K_TAB,
    K_CHAR,  // printable character in Event::ch
    K_TAP,   // touch screen tap at (x, y); the main loop turns it into touch::tap()
};

struct Event {
    Key  key = K_NONE;
    char ch = 0;      // character with shift applied (K_CHAR, and the raw char for arrows)
    char base = 0;    // unshifted character of the physical key
    bool fn = false, shift = false, ctrl = false, opt = false, alt = false;
    bool repeat = false;
    bool gesture = false;   // an arrow made from a touch swipe or drag, not a key
    int16_t x = 0, y = 0;   // K_TAP position, screen pixels
};

void begin();
void update();                 // call once per frame after board::update()
bool poll(Event& e);           // pop next event; false when queue empty

// When true, ; . , / produce characters unless Fn is held.
// When false (menus), they always produce arrows.
void setTextMode(bool on);
bool textMode();

uint32_t lastActivity();
void poke();   // counts as activity (touch holds that don't queue events)
void setAsleep(bool on);   // the screen is dark: a touch only wakes it
bool asleep();

// Queue a synthetic event (used by the serial debug console).
void inject(const Event& e);

}  // namespace input

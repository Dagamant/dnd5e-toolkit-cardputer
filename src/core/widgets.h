#pragma once
#include "ui.h"
#include <functional>

// Big-screen controls for calculator-style screens (Magic Shop, Travel):
// fingertip-sized buttons, steppers and read-outs. Each registers its own
// touch target as it draws.
namespace widgets {

// Small dim capitals over a group of controls.
void caption(int x, int y, const char* text);

// A button; `on` fills it (the chosen one of a group). A disabled one is drawn
// dim and ignores taps.
void button(int x, int y, int w, int h, const String& label, bool on, uint16_t accent, std::function<void()> fn,
            bool enabled = true);

// A read-out: label on top, a big value, an optional line under it.
void stat(int x, int y, int w, int h, const char* label, const String& value, uint16_t color, const String& sub = "");

// label  [-big] [-]  value  [+] [+big]   (the big steps only when big > 0).
// step(d) gets -big, -1, 1 or big; holding a button repeats it. A tap on the
// value runs onValue when given (to type one in).
void stepper(int x, int y, int w, int h, const char* label, const String& value, std::function<void(int)> step,
             int big = 0, std::function<void()> onValue = nullptr, int labelW = 220);

}  // namespace widgets

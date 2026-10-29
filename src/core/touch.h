#pragma once
#include <functional>
#include "input.h"

// Tap targets for the touch screen. Screens register them while they draw,
// in screen coordinates; a tap runs the last-registered target under the
// finger, so overlays drawn later cover what is beneath them. The list is
// rebuilt every frame, so it always matches what is on screen.
//
// On the Cardputer there is no touch screen and nothing is ever tapped.
namespace touch {

void clear();
// A screen that redraws only part of itself (an animation) keeps the targets
// it registered on its last full draw: while frozen, clear() and new targets
// are ignored. A tap unfreezes, so the next frame is drawn in full.
void freeze(bool on);
bool frozen();
void zone(int x, int y, int w, int h, std::function<void()> fn);
// A tap sends this key, as if it was pressed.
void key(int x, int y, int w, int h, input::Key k, char ch = 0, bool fn = false);
// A list row: a tap selects row i and opens it (Enter).
void row(int x, int y, int w, int h, int& sel, int i);
// A row whose Enter does something you wouldn't want by accident: the first
// tap selects it, a second tap on the selected row sends Enter.
void pick(int x, int y, int w, int h, int& sel, int i);
// A setting with a value: the first tap selects it, the next ones step the
// value (Right).
void field(int x, int y, int w, int h, int& sel, int i);
// Like zone, but holding a finger on it repeats fn (after half a second,
// about 11 times a second): +/- steppers.
void repeat(int x, int y, int w, int h, std::function<void()> fn);
// Holding a finger still on (x, y, w, h) for half a second runs fn (once):
// long-press menus. Taps go through to whatever is beneath.
void onHold(int x, int y, int w, int h, std::function<void()> fn);
// A region that scrolls with the finger. *pos is the offset in px (0..max)
// the screen draws from; a drag moves it, a flick coasts, and past either
// end it eases back. The screen only reads *pos (and may change it for keys).
void scroller(int x, int y, int w, int h, float* pos, float max);
// A modal overlay: taps outside (x, y, w, h) send Esc, taps inside do nothing
// unless a later target covers them.
void modal(int x, int y, int w, int h);

// Runs the target under (x, y). False when there is none.
bool tap(int x, int y);
// The board calls this while a finger rests (without dragging) at (x, y),
// held for heldMs. True once a repeat target has fired: the release is then
// not a tap.
bool hold(int x, int y, uint32_t heldMs);
// The board's finger tracking: down, a vertical drag of dy px from where it
// went down (true if a scroller took it), up.
void press();
bool drag(int x0, int y0, int dy);
void release();
// Once a frame: coasting and easing back.
void update(uint32_t dt);
// A scroller is moving: keep drawing.
bool scrolling();
// Where the tap being handled landed, for targets like sliders.
int tapX();
int tapY();

}  // namespace touch

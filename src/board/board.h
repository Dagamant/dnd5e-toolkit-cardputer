#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "../core/input.h"

// Everything that differs between devices. One implementation per board,
// chosen at build time (-DBOARD_CARDPUTER or -DBOARD_TAB5). Display,
// speaker, battery and IMU go through M5Unified's M5.* objects, which work
// on every M5Stack device.
namespace board {

const char* name();

// Start the hardware (M5Unified + keyboard). Call first in setup().
void begin();
// Once per frame: buttons, power state and a keyboard scan.
void update();

// ---- keyboard
// Physical keys held right now, as small board-specific ids.
int keysDown(uint16_t* ids, int max);
// Builds the event for a key id using the current modifiers. Returns false for
// keys that don't produce events by themselves (Shift, Fn, ...).
bool keyEvent(uint16_t id, bool textMode, input::Event& e);
#ifdef BOARD_TAB5
// The keyboard is plugged in (checked every couple of seconds); without it
// text is typed on the on-screen keyboard.
bool hasKeyboard();
// Testing the on-screen keyboard with the real one plugged in ("nokb" on the
// debug console).
void pretendNoKeyboard(bool on);
bool pretendingNoKeyboard();
// The keyboard's two LEDs: 0 off, 1 dim, 2 normal (set again when it's plugged back in).
void keyboardLights(uint8_t level);
#endif
// Enter, Tab and Esc don't auto-repeat when held.
bool keyRepeats(uint16_t id);
// Label of the modifier that sets Event::fn ("Fn" on the Cardputer). Hint and
// toast text is written with "Fn"; the UI swaps in this label when drawing.
const char* fnName();

// ---- microSD (SPI)
struct SdPins { int8_t sck, miso, mosi, cs; uint32_t hz; };
const SdPins& sdPins();

// ---- display
uint8_t displayRotation();   // landscape
// The screen is big enough for the native-resolution UI (ui::WIDE).
bool bigScreen();
// (Re)creates the frame canvas: the 240x135 one, or the big-screen one.
void makeCanvas(M5Canvas& c, bool wide);
// Fills a rectangle of the big canvas by DMA; false if the board can't (then
// draw it normally).
bool fill(M5Canvas& c, int x, int y, int w, int h, uint16_t color);
// Shows the finished frame (the Tab5 scales it up to its big screen).
void present(M5Canvas& frame);
#ifdef BOARD_TAB5
// Shows only (x, y, w, h) of the big canvas; the rest of the screen keeps the
// last frame. Much quicker for animations that stay in one area.
void presentRect(M5Canvas& frame, int x, int y, int w, int h);
#endif

}  // namespace board

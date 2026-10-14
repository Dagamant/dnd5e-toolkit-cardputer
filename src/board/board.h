#pragma once
#include <Arduino.h>
#include "../core/input.h"

// Everything that differs between devices. One implementation per board,
// chosen at build time (-DBOARD_CARDPUTER, later -DBOARD_TAB5). Display,
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

}  // namespace board

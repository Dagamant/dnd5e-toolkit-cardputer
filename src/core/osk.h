#pragma once

// On-screen keyboard for the big screen when the physical keyboard isn't
// plugged in. It types into whatever wants text (input::textMode): a prompt
// opens it, other screens that take typing get a keyboard button in the
// bottom-right corner. Keys are sent as ordinary key events.
namespace osk {

// Prompts call this; it only opens when there is no physical keyboard.
void show(bool on);
bool visible();
// The height it covers at the bottom of the screen (0 when hidden).
int height();
// A screen that takes typing without text mode (a list's type-to-search)
// says so each time it draws.
void offer();
// Drawn last, over everything: the keyboard, or the button that opens it.
void draw();

}  // namespace osk

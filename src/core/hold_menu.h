#pragma once
#include "input.h"
#include <Arduino.h>
#include <functional>
#include <vector>

// The menu a long press opens (big screen): a few actions for the thing under
// the finger, in a box beside it. main.cpp routes input to it while open and
// draws it over the current app.
//
//   touch::onHold(x, y, w, h, [this, i] {
//       holdmenu::open(names_[i], {{"Open", [this, i] { open(i); }},
//                                  {"Delete", [this, i] { remove(i); }, C_BLOOD, true}});
//   });
namespace holdmenu {

struct Item {
    String label;
    std::function<void()> fn;
    uint16_t color = 0;    // 0: the usual text colour
    bool confirm = false;  // needs a second tap ("Tap again to ...")
};

// Opens beside the long press that is being handled.
void open(const String& title, std::vector<Item> items);
bool active();
void close();
bool handle(const input::Event& e);
void draw();

}  // namespace holdmenu

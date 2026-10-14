#pragma once
#include "input.h"
#include <Arduino.h>
#include <vector>

// A modal roll result popup any app can open. main.cpp routes input to it
// while active and draws it on top of the current app.
//
//   rollui::show("Longsword", {{"Attack", "d20+5"}, {"Damage", "1d8+3", "Slashing"}});
//
// If the first line is a d20 roll that comes up a natural 20, later lines
// double their dice (critical hit damage).
namespace rollui {

struct Line {
    String label;
    String expr;
    String suffix;   // shown after the total, e.g. damage type
};

void show(const String& title, const std::vector<Line>& lines);
inline void show(const String& title, const String& expr, const String& suffix = "") {
    show(title, std::vector<Line>{{"", expr, suffix}});
}
bool active();
// Total of line i of the most recent roll (valid right after show()).
int total(size_t i = 0);
// When set, Enter closes instead of rerolling (for rolls already applied, e.g. healing).
void lockResult();
bool handle(const input::Event& e);
void tick(uint32_t dt);
void draw();

// "2d6+1d8+3" -> "4d6+2d8+3"
String doubleDice(const String& expr);

}  // namespace rollui

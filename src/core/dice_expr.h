#pragma once
#include <Arduino.h>
#include <vector>

// Dice expression engine.
//
// Grammar (case-insensitive, spaces ignored):
//   roll   := expr [ ("vs" | "dc" | "dr") NUMBER ]
//   expr   := term { ("+" | "-") term }
//   term   := NUMBER | dice | "adv" | "dis"
//   dice   := [COUNT] "d" (SIDES | "%") { mod }
//   mod    := "kh"[N] | "kl"[N] | "dh"[N] | "dl"[N]   keep/drop highest/lowest
//           | "!"                                    exploding dice
//           | "r"[N]                                 reroll once if <= N (default 1)
// Examples: d20+5, 4d6kh3, 2d20kl1, adv+7, 8d6, d%, 3d6!, d20+4 vs 15
namespace dice {

struct Group {
    int sides = 0;           // 0 = constant
    int sign = 1;
    int constant = 0;
    std::vector<int> vals;   // individual results (exploded dice appended)
    std::vector<bool> kept;
    int subtotal() const;
};

enum class Degree : int8_t { None = -1, CritFail = 0, Fail = 1, Success = 2, CritSuccess = 3 };

struct Result {
    bool ok = false;
    String error;
    String expr;         // normalised expression
    int total = 0;
    int minTotal = 0, maxTotal = 0;
    std::vector<Group> groups;
    int natural = 0;     // value of the lone kept d20, or 0
    bool hasTarget = false;
    int target = 0;
    Degree degree = Degree::None;
    String breakdown() const;   // "[6,5,3,~1~] +2"
    int largestDie() const;
};

// Parses and rolls. With "vs N": success if total >= N; natural 20/1 on a lone d20 are crit/fumble.
Result roll(const String& expr);
const char* degreeName(Degree d);

}  // namespace dice

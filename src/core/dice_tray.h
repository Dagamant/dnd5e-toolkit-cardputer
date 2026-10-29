#pragma once
#include "dice_expr.h"
#include <vector>

// Dice that tumble and bounce around a rectangle, slow down and land in a
// tidy grid showing what was rolled (the Dice app's tray and the roll popup).
// Big screen only: the Cardputer keeps its own animations.
//
//   tray.start({&attack, &damage}, x, y, w, h);   // then each frame:
//   tray.step(dt);  tray.draw();  tray.liveTotal(1, damage);
namespace dicetray {

// Edge colour of the dice of the i-th result (attack gold, damage ember, ...).
uint16_t tint(int i);

class Tray {
public:
    // Throws the dice of these results inside (x, y, w, h); maxR caps the size.
    void start(const std::vector<const dice::Result*>& results, int x, int y, int w, int h, int maxR = 110);
    void clear() { dice_.clear(); }
    void step(uint32_t dt);
    void draw() const;
    bool empty() const { return dice_.empty(); }
    bool rolling() const { return !dice_.empty() && elapsed_ < duration_; }
    // What result i adds up to so far: the faces showing plus its fixed numbers.
    int liveTotal(size_t i, const dice::Result& r) const;

private:
    struct Die {
        int sides, value, shown, sign, result;
        bool kept, settled;
        float rx, ry, r;      // where it lands
        float x, y, vx, vy;   // where it is while tumbling (px, px/ms)
        float rot, spin, phase;
        uint32_t settleMs, nextFlip;
    };
    std::vector<Die> dice_;
    int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    uint32_t elapsed_ = 0, duration_ = 0;
    void drawDie(const Die& d) const;
};

}  // namespace dicetray

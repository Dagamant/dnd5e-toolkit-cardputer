#include "dice_tray.h"
#include "die_shapes.h"
#include "rng.h"
#include "ui.h"
#include <math.h>
#include <algorithm>

using namespace ui;

namespace dicetray {

uint16_t tint(int i) {
    static const uint16_t T[] = {C_GOLD, C_BLOOD, C_ARCANE, C_EMERALD, C_ICE};
    return T[i % 5];
}

void Tray::start(const std::vector<const dice::Result*>& results, int x, int y, int w, int h, int maxR) {
    dice_.clear();
    x_ = x; y_ = y; w_ = w; h_ = h;
    elapsed_ = duration_ = 0;
    for (size_t k = 0; k < results.size(); k++) {
        if (!results[k] || !results[k]->ok) continue;
        for (auto& g : results[k]->groups) {
            if (!g.sides) continue;
            for (size_t i = 0; i < g.vals.size() && dice_.size() < 24; i++) {
                Die d = {};
                d.sides = g.sides;
                d.value = g.vals[i];
                d.kept = g.kept[i];
                d.sign = g.sign;
                d.result = k;
                dice_.push_back(d);
            }
        }
    }
    const int n = dice_.size();
    if (!n) return;
    const int cols = n <= 1 ? 1 : n <= 4 ? 2 : n <= 9 ? 3 : n <= 16 ? 4 : 5, rows = (n + cols - 1) / cols;
    // wide and short trays: more columns
    const int c2 = w > 2 * h ? std::min(n, std::max(cols, (int)ceilf(sqrtf(n * (float)w / h)))) : cols;
    const int r2 = (n + c2 - 1) / c2;
    const float cw = (float)w / c2, ch = (float)h / r2;
    const float r = std::min(std::min(cw, ch) * 0.40f, (float)maxR);
    (void)rows;
    for (int i = 0; i < n; i++) {
        Die& d = dice_[i];
        const int row = i / c2, col = i % c2, inRow = row == r2 - 1 ? n - row * c2 : c2;
        d.rx = x + (w - inRow * cw) / 2 + (col + 0.5f) * cw;   // a short last row is centred
        d.ry = y + (row + 0.5f) * ch;
        d.r = r;
        d.x = x + r + rng::unit() * std::max(1.0f, w - 2 * r);
        d.y = y + r + rng::unit() * std::max(1.0f, h - 2 * r);
        const float a = rng::unit() * 6.2832f, sp = 0.7f + rng::unit() * 0.7f;
        d.vx = cosf(a) * sp;
        d.vy = sinf(a) * sp;
        d.rot = rng::unit() * 6.2832f;
        d.spin = (rng::unit() < 0.5f ? -1 : 1) * (0.012f + rng::unit() * 0.012f);
        d.phase = rng::unit() * 6.2832f;
        d.settleMs = 900 + i * 45 + rng::below(500);
        d.shown = 1 + rng::below(d.sides);
        duration_ = std::max(duration_, d.settleMs);
    }
    duration_ += 30;
}

// Slide and bounce off the walls, slowing down; faces flick over more and
// more slowly until each die lands.
void Tray::step(uint32_t dt) {
    if (dice_.empty()) return;
    dt = std::min<uint32_t>(dt, 60);
    elapsed_ += dt;
    for (auto& d : dice_) {
        if (d.settled) continue;
        if (elapsed_ >= d.settleMs) { d.settled = true; d.shown = d.value; continue; }
        const float p = (float)elapsed_ / d.settleMs;
        d.x += d.vx * dt;
        d.y += d.vy * dt;
        const float lx = x_ + d.r, hx = x_ + w_ - d.r, ly = y_ + d.r, hy = y_ + h_ - d.r;
        if (d.x < lx) { d.x = lx; d.vx = fabsf(d.vx) * 0.8f; }
        if (d.x > hx) { d.x = hx; d.vx = -fabsf(d.vx) * 0.8f; }
        if (d.y < ly) { d.y = ly; d.vy = fabsf(d.vy) * 0.8f; }
        if (d.y > hy) { d.y = hy; d.vy = -fabsf(d.vy) * 0.8f; }
        const float fr = powf(0.9975f, (float)dt);
        d.vx *= fr;
        d.vy *= fr;
        d.rot += d.spin * dt * (1 - p);
        if (elapsed_ >= d.nextFlip) {
            d.shown = 1 + rng::below(d.sides);
            d.nextFlip = elapsed_ + 35 + (uint32_t)(220 * p * p);
        }
    }
}

int Tray::liveTotal(size_t i, const dice::Result& r) const {
    int sum = 0;
    for (auto& g : r.groups) if (!g.sides) sum += g.sign * g.constant;
    for (auto& d : dice_) if (d.result == (int)i && d.kept) sum += d.sign * d.shown;
    return sum;
}

void Tray::draw() const {
    for (auto& d : dice_) drawDie(d);
}

void Tray::drawDie(const Die& d) const {
    const float p = d.settled ? 1.0f : std::min(1.0f, (float)elapsed_ / d.settleMs);
    float s = std::max(0.0f, std::min(1.0f, (p - 0.6f) / 0.4f));
    s = s * s * (3 - 2 * s);   // glide into place over the last part of the tumble
    const float k = 1 - p;
    const float hop = fabsf(sinf(elapsed_ * 0.014f + d.phase)) * k * k * d.r * 0.7f;
    const float x = d.x + (d.rx - d.x) * s, by = d.y + (d.ry - d.y) * s, y = by - hop;
    const float squash = d.settled ? 1.0f : 0.35f + 0.65f * fabsf(cosf(elapsed_ * 0.018f + d.phase));
    const float rot = d.settled ? 0 : d.rot * (1 - s);
    const bool dropped = d.settled && !d.kept;
    const bool hi = d.settled && d.kept && d.sides == 20 && (d.value == 20 || d.value == 1);
    const uint16_t hiC = d.value == 20 ? C_GOLD : C_BLOOD, edge = tint(d.result);
    gfx.fillEllipse(x, by + d.r * 0.95f, std::max(4.0f, d.r * 0.75f - hop * 0.3f), std::max(2.0f, d.r * 0.14f), rgb(3, 6, 14));
    if (hi) for (int g = 0; g < 3; g++) gfx.drawCircle(x, y, d.r * 1.15f + g * 4, g ? rgb(70, 50, 20) : hiC);
    const float edgeW = d.r > 60 ? 5 : d.r > 36 ? 4 : 3;
    dieshape::draw(d.sides, x, y, d.r, rot, squash, dropped ? C_BG2 : hi ? (d.value == 20 ? C_GOLD_DK : C_BLOOD_DK) : C_PANEL,
                   dropped ? C_EDGE : hi ? hiC : edge, dropped ? C_EDGE : C_GOLD_DK, edgeW);
    if (squash > 0.55f) {
        gfx.setFont(d.r >= 60 ? font(&fonts::FreeSerifBold18pt7b) : d.r >= 36 ? font(&fonts::FreeSerifBold12pt7b) : metrics().bold);
        gfx.setTextDatum(middle_center);
        // numbers sit a touch low on the triangles so they fit inside
        const float dy = (d.sides == 3 || d.sides == 4) ? d.r * 0.22f : 0;
        gfx.setTextColor(dropped ? C_EDGE : hi ? rgb(255, 235, 170) : d.settled ? C_TEXT : C_DIM);
        gfx.drawString(String(d.shown), x, y + dy + 2);
    }
    if (dropped) gfx.drawWideLine(x - d.r * 0.7f, y + d.r * 0.7f, x + d.r * 0.7f, y - d.r * 0.7f, 2, C_BLOOD);
}

}  // namespace dicetray

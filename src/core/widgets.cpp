#include "widgets.h"
#include "sfx.h"
#include "touch.h"

using namespace ui;

namespace widgets {

void caption(int x, int y, const char* text) {
    gfx.setFont(metrics().small);
    gfx.setTextDatum(top_left);
    gfx.setTextColor(C_DIM);
    gfx.drawString(text, x + 4, y);
}

void button(int x, int y, int w, int h, const String& label, bool on, uint16_t accent, std::function<void()> fn,
            bool enabled) {
    const Metrics& m = metrics();
    gfx.fillRoundRect(x, y, w, h, 10, on ? C_PANEL : C_BG2);
    gfx.drawRoundRect(x, y, w, h, 10, on ? accent : C_EDGE);
    if (on) gfx.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, accent);
    gfx.setFont(m.body);
    if (gfx.textWidth(label) > w - 12) gfx.setFont(m.small);   // a smaller font before cutting it short
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(!enabled ? C_EDGE : on ? accent : C_TEXT);
    gfx.drawString(fitText(label, w - 12), x + w / 2, y + h / 2 + 1);
    if (enabled && fn) touch::zone(x, y, w, h, std::move(fn));
}

void stat(int x, int y, int w, int h, const char* label, const String& value, uint16_t color, const String& sub) {
    const Metrics& m = metrics();
    drawPanel(x, y, w, h, C_BG2, C_EDGE);
    gfx.setFont(m.small);
    gfx.setTextDatum(top_center);
    gfx.setTextColor(C_DIM);
    gfx.drawString(label, x + w / 2, y + 10);
    // the biggest font the value fits in
    gfx.setFont(m.big);
    if (gfx.textWidth(value) > w - 16) gfx.setFont(m.title);
    if (gfx.textWidth(value) > w - 16) gfx.setFont(m.bold);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(color);
    const int vy = sub.length() ? y + h / 2 + 4 : y + h / 2 + 12;
    gfx.drawString(value, x + w / 2, vy);
    if (sub.length()) {
        gfx.setFont(m.small);
        gfx.setTextDatum(bottom_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(fitText(sub, w - 12), x + w / 2, y + h - 8);
    }
}

void stepper(int x, int y, int w, int h, const char* label, const String& value, std::function<void(int)> step, int big,
             std::function<void()> onValue, int labelW) {
    const Metrics& m = metrics();
    gfx.setFont(m.body);
    gfx.setTextDatum(middle_left);
    gfx.setTextColor(C_TEXT);
    gfx.drawString(label, x + 4, y + h / 2 + 1);
    const int bw = h + 8, bigW = big ? h + 12 : 0, gap = 8;
    int bx = x + labelW;
    const int vw = x + w - bx - 2 * (bw + gap) - (big ? 2 * (bigW + gap) : 0);
    auto key = [&](int bx, int bwid, const String& t, int d) {
        gfx.fillRoundRect(bx, y, bwid, h, 10, C_BG2);
        gfx.drawRoundRect(bx, y, bwid, h, 10, C_EDGE);
        gfx.setFont(d == 1 || d == -1 ? m.title : m.body);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(d > 0 ? C_GOLD : C_DIM);
        gfx.drawString(t, bx + bwid / 2, y + h / 2 + 1);
        touch::repeat(bx, y, bwid, h, [step, d] { step(d); sfx::click(); });
    };
    if (big) { key(bx, bigW, String("-") + big, -big); bx += bigW + gap; }
    key(bx, bw, "-", -1);
    bx += bw + gap;
    gfx.fillRoundRect(bx, y, vw, h, 10, C_BG);
    gfx.drawRoundRect(bx, y, vw, h, 10, onValue ? C_GOLD_DK : C_EDGE);
    gfx.setFont(m.bold);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(C_GOLD);
    gfx.drawString(fitText(value, vw - 12), bx + vw / 2, y + h / 2 + 1);
    if (onValue) touch::zone(bx, y, vw, h, onValue);
    bx += vw + gap;
    key(bx, bw, "+", 1);
    bx += bw + gap;
    if (big) key(bx, bigW, String("+") + big, big);
}

}  // namespace widgets

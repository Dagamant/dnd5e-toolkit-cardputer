#include "hold_menu.h"
#include "sfx.h"
#include "touch.h"
#include "ui.h"
#include <algorithm>

using namespace ui;

namespace holdmenu {

static bool s_open = false;
static String s_title;
static std::vector<Item> s_items;
static int s_sel = 0;
static int s_armed = -1;   // a confirm item that has had its first tap
static int s_x = 0, s_y = 0;

static constexpr int MIN_W = 340, MAX_W = 600, TITLE_H = 52, ROW_H = 68, PAD = 10;

void open(const String& title, std::vector<Item> items) {
    if (items.empty()) return;
    s_title = title;
    s_items = std::move(items);
    s_sel = 0;
    s_armed = -1;
    s_x = touch::tapX();
    s_y = touch::tapY();
    s_open = true;
    sfx::select();
}

bool active() { return s_open; }

void close() {
    s_open = false;
    s_items.clear();
}

static void choose(int i) {
    if (i < 0 || i >= (int)s_items.size()) return;
    if (s_items[i].confirm && s_armed != i) {
        s_armed = i;
        s_sel = i;
        sfx::click();
        return;
    }
    auto fn = s_items[i].fn;   // it may open another menu
    close();
    if (fn) fn();
}

bool handle(const input::Event& e) {
    if (!s_open) return false;
    const int n = s_items.size();
    switch (e.key) {
        case input::K_BACK: close(); sfx::back(); break;
        case input::K_UP: s_sel = (s_sel + n - 1) % n; s_armed = -1; sfx::click(); break;
        case input::K_DOWN: s_sel = (s_sel + 1) % n; s_armed = -1; sfx::click(); break;
        case input::K_ENTER: choose(s_sel); break;
        default: break;
    }
    return true;   // nothing gets past an open menu
}

void draw() {
    if (!s_open) return;
    const Metrics& m = metrics();
    const int n = s_items.size(), h = TITLE_H + n * ROW_H + PAD;
    // as wide as the longest action (or "Tap again to ...")
    gfx.setFont(m.body);
    int W = MIN_W;
    for (auto& it : s_items) W = std::max(W, (int)(gfx.textWidth(it.label) + 2 * PAD + 40 + (it.confirm ? gfx.textWidth("Tap again to ") : 0)));
    W = std::min(W, MAX_W);
    // beside the finger, kept on screen
    int x = s_x + 24, y = s_y - TITLE_H / 2;
    if (x + W > m.w - 8) x = s_x - 24 - W;
    x = std::max(8, std::min(m.w - W - 8, x));
    y = std::max(8, std::min(m.h - h - 8, y));
    touch::modal(x, y, W, h);
    gfx.fillRoundRect(x + 6, y + 8, W, h, 14, C_BG);   // shadow
    drawPanel(x, y, W, h, C_BG2, C_GOLD);
    gfx.setFont(m.bold);
    gfx.setTextDatum(middle_left);
    gfx.setTextColor(C_GOLD);
    gfx.drawString(fitText(s_title, W - 2 * PAD - 8), x + PAD + 6, y + TITLE_H / 2 + 2);
    gfx.drawFastHLine(x + PAD, y + TITLE_H - 2, W - 2 * PAD, C_EDGE);
    gfx.setFont(m.body);
    for (int i = 0; i < n; i++) {
        const Item& it = s_items[i];
        const int ry = y + TITLE_H + i * ROW_H + 4, rh = ROW_H - 8;
        const bool armed = i == s_armed;
        if (i == s_sel || armed)
            gfx.fillRoundRect(x + PAD, ry, W - 2 * PAD, rh, 10, armed ? C_BLOOD_DK : C_PANEL);
        gfx.drawRoundRect(x + PAD, ry, W - 2 * PAD, rh, 10, armed ? C_BLOOD : i == s_sel ? C_GOLD_DK : C_EDGE);
        gfx.setTextColor(armed ? C_TEXT : it.color ? it.color : C_TEXT);
        String label = it.label;
        if (armed) {
            label.toLowerCase();
            label = "Tap again to " + label;
        }
        gfx.drawString(fitText(label, W - 2 * PAD - 24), x + PAD + 16, ry + rh / 2 + 1);
        touch::zone(x + PAD, ry, W - 2 * PAD, rh, [i] { choose(i); });
    }
}

}  // namespace holdmenu

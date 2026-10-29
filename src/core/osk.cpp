#include "osk.h"
#include "input.h"
#include "roll_overlay.h"
#include "sfx.h"
#include "touch.h"
#include "ui.h"
#include "../board/board.h"

using namespace ui;

namespace osk {

static bool s_visible = false;
static int s_shift = 0;          // 0 off, 1 next key, 2 locked
static int s_lastKey = -1;       // flashes for a moment after a tap
static uint32_t s_lastAt = 0;

static constexpr int KB_H = 336, ROW_H = 58, GAP = 6, PAD = 12;

// Special keys; anything else is the character itself.
enum : char { K_BS = 1, K_RET, K_SHIFT, K_ESC, K_LEFT, K_RIGHT, K_HIDE, K_SPACE };

struct Key { char ch; float units; };

// 13 units a row
static const Key ROWS[5][14] = {
    {{'1', 1}, {'2', 1}, {'3', 1}, {'4', 1}, {'5', 1}, {'6', 1}, {'7', 1}, {'8', 1}, {'9', 1}, {'0', 1}, {'-', 1}, {'+', 1}, {K_BS, 1}},
    {{'q', 1}, {'w', 1}, {'e', 1}, {'r', 1}, {'t', 1}, {'y', 1}, {'u', 1}, {'i', 1}, {'o', 1}, {'p', 1}, {'(', 1}, {')', 1}, {'*', 1}},
    {{'a', 1.5f}, {'s', 1}, {'d', 1}, {'f', 1}, {'g', 1}, {'h', 1}, {'j', 1}, {'k', 1}, {'l', 1}, {'\'', 1}, {K_RET, 2.5f}},
    {{K_SHIFT, 2}, {'z', 1}, {'x', 1}, {'c', 1}, {'v', 1}, {'b', 1}, {'n', 1}, {'m', 1}, {',', 1}, {'.', 1}, {'/', 1}, {':', 1}},
    {{K_ESC, 1.5f}, {'#', 1}, {K_SPACE, 6}, {K_LEFT, 1.25f}, {K_RIGHT, 1.25f}, {K_HIDE, 2}},
};

static char shifted(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    static const char* const FROM = "1234567890-+'/,.:=#*()";
    static const char* const TO = "!@#$%^&*()_=\"?<>;~&%[]";
    for (int i = 0; FROM[i]; i++) if (FROM[i] == c) return TO[i];
    return c;
}

static bool s_offered = false;   // this frame's screen takes typing
void offer() { s_offered = true; }

static bool wanted() {
#ifdef BOARD_TAB5
    return wide() && !board::hasKeyboard() && (input::textMode() || s_offered);
#else
    return false;
#endif
}

void show(bool on) {
    s_visible = on && wanted();
    s_shift = 0;
}

bool visible() { return s_visible && wanted() && !rollui::active(); }

int height() { return visible() ? KB_H : 0; }

static void send(input::Key k, char ch = 0) {
    input::Event e;
    e.key = k;
    e.ch = ch;
    e.base = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
    input::inject(e);
}

static void press(char k) {
    switch (k) {
        case K_BS: send(input::K_DEL); break;
        case K_RET: send(input::K_ENTER); break;
        case K_ESC: send(input::K_BACK); break;
        case K_LEFT: send(input::K_LEFT); break;
        case K_RIGHT: send(input::K_RIGHT); break;
        case K_SPACE: send(input::K_CHAR, ' '); break;
        case K_HIDE: s_visible = false; sfx::back(); return;
        case K_SHIFT: s_shift = (s_shift + 1) % 3; sfx::click(); return;
        default:
            send(input::K_CHAR, s_shift ? shifted(k) : k);
            if (s_shift == 1) s_shift = 0;
    }
    sfx::click();
}

static String label(char k) {
    switch (k) {
        case K_BS: return "Del";
        case K_RET: return "Enter";
        case K_SHIFT: return s_shift == 2 ? "CAPS" : "Shift";
        case K_ESC: return "Esc";
        case K_LEFT: return "<";
        case K_RIGHT: return ">";
        case K_HIDE: return "Hide";
        case K_SPACE: return "space";
        default: return String(s_shift ? shifted(k) : k);
    }
}

// The closed keyboard: a button in the bottom-right corner.
static void drawButton() {
    const Metrics& m = metrics();
    const int w = 96, h = 50, x = m.w - w - 10, y = m.h - h - 5;
    gfx.fillRoundRect(x, y, w, h, 10, C_PANEL);
    gfx.drawRoundRect(x, y, w, h, 10, C_GOLD);
    // a little keyboard
    const int kx = x + 22, ky = y + 13, kw = 52, kh = 24;
    gfx.drawRoundRect(kx, ky, kw, kh, 4, C_GOLD);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 5; c++) gfx.fillRect(kx + 5 + c * 9, ky + 4 + r * 7, 6, 4, C_GOLD);
    gfx.fillRect(kx + 12, ky + 18, 28, 3, C_GOLD);
    touch::zone(x, y, w, h, [] { s_visible = true; s_shift = 0; sfx::select(); });
}

void draw() {
    struct Reset { ~Reset() { s_offered = false; } } reset;   // offers last one frame
    if (!wanted() || rollui::active()) { if (!wanted()) s_visible = false; return; }
    if (!s_visible) { drawButton(); return; }
    const Metrics& m = metrics();
    const int top = m.h - KB_H;
    gfx.fillRect(0, top, m.w, KB_H, C_BG);
    gfx.drawFastHLine(0, top, m.w, C_GOLD_DK);
    gfx.drawFastHLine(0, top + 1, m.w, C_GOLD_DK);
    touch::zone(0, top, m.w, KB_H, [] {});   // nothing beneath gets the taps
    const float unit = (m.w - 2 * PAD - 12 * GAP) / 13.0f;
    gfx.setFont(m.bold);
    gfx.setTextDatum(middle_center);
    int idx = 0;
    for (int r = 0; r < 5; r++) {
        float x = PAD;
        const int y = top + PAD + r * (ROW_H + GAP);
        for (int i = 0; i < 14 && ROWS[r][i].ch; i++, idx++) {
            const Key& k = ROWS[r][i];
            const int w = (int)(k.units * unit + (k.units - 1) * GAP);
            const bool special = k.ch < ' ', flash = idx == s_lastKey && millis() - s_lastAt < 150;
            const bool on = k.ch == K_SHIFT && s_shift;
            gfx.fillRoundRect((int)x, y, w, ROW_H, 8, flash ? C_GOLD_DK : on ? C_PANEL : special ? C_BG2 : rgb(20, 32, 56));
            gfx.drawRoundRect((int)x, y, w, ROW_H, 8, on ? C_GOLD : C_EDGE);
            gfx.setTextColor(special ? (k.ch == K_RET ? C_GOLD : C_DIM) : C_TEXT);
            gfx.drawString(label(k.ch), (int)x + w / 2, y + ROW_H / 2);
            const char ch = k.ch;
            const int id = idx;
            auto fn = [ch, id] { s_lastKey = id; s_lastAt = millis(); press(ch); };
            // Del and the arrows repeat while held
            if (ch == K_BS || ch == K_LEFT || ch == K_RIGHT) touch::repeat((int)x, y, w, ROW_H, fn);
            else touch::zone((int)x, y, w, ROW_H, fn);
            x += w + GAP;
        }
    }
}

}  // namespace osk

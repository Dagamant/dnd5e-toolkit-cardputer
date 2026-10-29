#include "ui.h"
#include "web.h"
#include "input.h"
#include "touch.h"
#include "../board/board.h"
#include "fonts_wide.h"
#include <algorithm>
#include <math.h>

namespace ui {

// &M5.Display (a plain member) is safe during static initialisation;
// M5Cardputer.Display is a reference that isn't set up yet at that point.
M5Canvas gfx(&M5.Display);

static char     s_toast[96];
static uint16_t s_toastColor = C_GOLD;
static uint32_t s_toastUntil = 0;

// header, footer and rows are sized for a fingertip (the touch screen)
const Metrics WIDE{1280, 720, 68, 60, 32, 16, 20, &WideMono, &fonts::FreeSerifBold18pt7b,
                   &fonts::FreeSerifBold24pt7b, &WideSmall, &WideMonoBold, 60, 4};

bool g_wide = false;
static bool& s_wide = g_wide;
const Metrics& metrics() { return s_wide ? WIDE : CLASSIC; }
bool bigScreen() { return board::bigScreen(); }

const lgfx::IFont* font(const lgfx::IFont* f) {
    if (!s_wide) return f;
    using namespace fonts;
    if (f == &Font0) return &WideMono;
    if (f == &Font2) return &WideMonoBold;
    if (f == &FreeSerif9pt7b) return &FreeSerif18pt7b;
    if (f == &FreeSerifBold9pt7b) return &FreeSerifBold18pt7b;
    if (f == &FreeSerifBold12pt7b) return &FreeSerifBold24pt7b;
    if (f == &FreeSerifBold18pt7b) return &DejaVu56;
    if (f == &FreeSerifBold24pt7b) return &DejaVu72;
    if (f == &FreeMonoBold12pt7b) return &FreeMonoBold24pt7b;
    return f;
}

void setWide(bool on) {
    on = on && board::bigScreen();
    if (on == s_wide && gfx.getBuffer()) return;
    s_wide = on;
    const Metrics& m = metrics();
    W = m.w; H = m.h; HEADER_H = m.headerH; FOOTER_H = m.footerH;
    board::makeCanvas(gfx, on);
    gfx.setTextWrap(false);
}

void begin() {
    M5.Display.setRotation(board::displayRotation());
    setWide(board::bigScreen());
}

struct Rect { int16_t x, y, w, h; };
static Rect s_only[64];
static int s_onlyN = 0;   // -1: too many, show it all

void presentOnly(int x, int y, int w, int h) {
    if (s_onlyN < 0) return;
    if (s_onlyN == 64) { s_onlyN = -1; return; }
    s_only[s_onlyN++] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

bool toastShowing() { return millis() <= s_toastUntil; }

void present() {
    drawToast();
#ifdef BOARD_TAB5
    const int n = s_onlyN;
    s_onlyN = 0;
    if (n > 0) {
        for (int i = 0; i < n; i++) board::presentRect(gfx, s_only[i].x, s_only[i].y, s_only[i].w, s_only[i].h);
        return;
    }
#endif
    board::present(gfx);
}

void drawBackground() {
    const Metrics& m = metrics();
    if (!board::fill(gfx, 0, 0, m.w, m.h, C_BG)) gfx.fillScreen(C_BG);
    // subtle vignette lines for texture
    for (int y = m.headerH; y < m.h; y += 4) gfx.drawFastHLine(0, y, m.w, rgb(11, 19, 36));
}

void shade(int x, int y, int w, int h, int r, uint8_t alpha, uint16_t color) {
    // a row at a time, each inset to follow the rounded corners
    for (int i = 0; i < h; i++) {
        int d = i < r ? r - i : i >= h - r ? i - (h - r - 1) : 0, in = 0;
        if (d) in = r - (int)sqrtf((float)(r * r - d * d));
        gfx.fillRectAlpha(x + in, y + i, w - 2 * in, 1, alpha, color);
    }
}

String keyHints(const char* text) {
    String s = text;
    const char* fn = board::fnName();
    if (strcmp(fn, "Fn")) s.replace("Fn", fn);
    return s;
}

String fitText(const String& s, int maxW) {
    if (gfx.textWidth(s) <= maxW) return s;
    int lo = 0, hi = s.length();   // longest prefix that fits with the "."
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (gfx.textWidth(s.substring(0, mid) + ".") <= maxW) lo = mid;
        else hi = mid - 1;
    }
    String t = s.substring(0, lo);
    t.trim();
    return t + ".";
}

void drawHeader(const char* title, uint16_t accent, int reserve) {
    const Metrics& m = metrics();
    gfx.fillRect(0, 0, m.w, m.headerH, C_BG2);
    gfx.fillRect(0, m.headerH - u(1), m.w, u(1), accent);
    gfx.fillRect(0, m.headerH - u(2), m.w, u(1), C_GOLD_DK);
    gfx.setFont(m.title);
    gfx.setTextColor(accent);
    gfx.setTextDatum(middle_left);
    const int tx = headerTitleX();
    String t = fitText(title, m.w - u(34) - reserve - tx + m.pad);
    gfx.drawString(t, tx, m.headerH / 2);
    if (wide()) {
        // touch: a back button, and the title is part of it
        const int b = m.headerH - 16, by = 7;
        gfx.fillRoundRect(10, by, b, b, 8, C_PANEL);
        gfx.drawRoundRect(10, by, b, b, 8, C_EDGE);
        const int cx = 10 + b / 2, cy = by + b / 2;
        gfx.drawWideLine(cx + 5, cy - 11, cx - 6, cy, 4, accent);
        gfx.drawWideLine(cx - 6, cy, cx + 5, cy + 11, 4, accent);
        touch::key(0, 0, tx + gfx.textWidth(t) + 20, m.headerH, input::K_BACK);
    }
    // battery
    // reading the battery takes ADC samples; once every few seconds is plenty
    static int lvl = -1;
    static uint32_t nextRead = 0;
    if (millis() >= nextRead) { lvl = M5.Power.getBatteryLevel(); nextRead = millis() + 5000; }
    if (lvl >= 0) {
        int bx = m.w - u(24), by = (m.headerH - u(8)) / 2;
        gfx.drawRect(bx, by, u(18), u(8), C_DIM);
        gfx.fillRect(bx + u(18), by + u(2), u(2), u(4), C_DIM);
        uint16_t c = lvl > 50 ? C_EMERALD : (lvl > 20 ? C_GOLD : C_BLOOD);
        gfx.fillRect(bx + u(2), by + u(2), (u(14) * lvl) / 100, u(4), c);
    }
    // web access running: a small WiFi mark left of the battery
    if (web::state() != web::OFF) {
        int wx = m.w - u(31), wy = m.headerH / 2 + u(3);
        uint16_t c = web::state() == web::RUNNING ? C_ICE : ((millis() / 300) % 2 ? C_DIM : C_EDGE);
        gfx.fillCircle(wx, wy, u(1), c);
        gfx.drawArc(wx, wy, u(4), u(4) - u(1) + 1, 225, 315, c);
        gfx.drawArc(wx, wy, u(7), u(7) - u(1) + 1, 225, 315, c);
    }
}

int headerTitleX() { return wide() ? metrics().headerH + 8 : metrics().pad; }

// The key a footer hint names ("Ent", "Esc", "R", "CtrlX", "<"), as the key a
// tap on it sends. False for words that aren't one key ("A-Z", "type").
static bool hintKey(const String& w, input::Key& k, char& ch, bool& fn) {
    k = input::K_CHAR; ch = 0; fn = false;
    const String pre = board::fnName();
    if (w == "Ent" || w == "Enter") k = input::K_ENTER;
    else if (w == "Esc") k = input::K_BACK;
    else if (w == "Tab") k = input::K_TAB;
    else if (w == "Del") k = input::K_DEL;
    else if (w == "<") k = input::K_LEFT;
    else if (w == ">") k = input::K_RIGHT;
    else if (w == "Up") k = input::K_UP;
    else if (w == "Dn") k = input::K_DOWN;
    else if (w == "Spc" || w == "Space") ch = ' ';
    else if (w == "XX") ch = 'x';
    else if (w.length() == pre.length() + 1 && w.startsWith(pre)) { ch = tolower(w[w.length() - 1]); fn = true; }
    else if (w.length() == 1 && w[0] > ' ') ch = tolower(w[0]);
    else return false;
    return true;
}

// A hint word as one or two keys ("Ent", "</>", "CtrlA/D"). False when it
// isn't keys, and then it is part of the previous key's label.
static bool hintKeys(const String& word, String keys[2], input::Key k[2], char ch[2], bool fn[2]) {
    const String pre = board::fnName();
    keys[0] = word;
    keys[1] = "";
    int slash = word.indexOf('/');
    if (slash > 0 && slash < (int)word.length() - 1) {
        keys[0] = word.substring(0, slash);
        keys[1] = word.substring(slash + 1);
        if (keys[0].startsWith(pre) && keys[1].length() == 1) keys[1] = pre + keys[1];
    }
    return hintKey(keys[0], k[0], ch[0], fn[0]) && (!keys[1].length() || hintKey(keys[1], k[1], ch[1], fn[1]));
}

// On the big screen each hint is a button: its key in a chip, then what it
// does. "</>", "Up/Dn", "-/=" and "CtrlA/D" become a chip per key. Words
// after a key, up to the next key, are its label.
static void drawFooterButtons(const String& hints) {
    const Metrics& m = metrics();
    const int y = m.h - m.footerH + 5, h = m.footerH - 9, cy = m.h - m.footerH / 2;
    std::vector<String> words;
    for (int at = 0; at < (int)hints.length();) {
        int sp = hints.indexOf(' ', at);
        if (sp < 0) sp = hints.length();
        if (sp > at) words.push_back(hints.substring(at, sp));
        at = sp + 1;
    }
    int x = m.pad / 2;
    for (size_t i = 0; i < words.size() && x < m.w;) {
        String keys[2];
        input::Key k[2];
        char ch[2];
        bool fn[2];
        const bool isKey = hintKeys(words[i], keys, k, ch, fn);
        String label;
        size_t j = i + 1;
        String dk[2];
        input::Key dkk[2];
        char dch[2];
        bool dfn[2];
        // "hold for more" (long-press menus) stands on its own, not as a key's label
        for (; j < words.size() && words[j] != "hold" && !hintKeys(words[j], dk, dkk, dch, dfn); j++)
            label += (label.length() ? " " : "") + words[j];
        gfx.setTextDatum(middle_left);
        if (!isKey) {
            String t = words[i] + (label.length() ? " " + label : String(""));
            gfx.setTextColor(C_DIM);
            gfx.drawString(t, x, cy);
            x += gfx.textWidth(t) + 22;
            i = j;
            continue;
        }
        const int start = x;
        const bool pair = keys[1].length();
        const int lw = label.length() ? gfx.textWidth(label) + 12 : 0;
        for (int n = 0; n < 2 && keys[n].length(); n++) {
            String shown = keys[n] == "Dn" ? String("Down") : keys[n];
            const int w = std::max<int>(64, gfx.textWidth(shown) + 24);   // fingertip wide
            gfx.fillRoundRect(x, y, w, h, 7, C_PANEL);
            gfx.drawRoundRect(x, y, w, h, 7, C_EDGE);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(shown, x + w / 2, cy);
            // a lone key's button also covers its label; a pair's keys are separate
            touch::key(n == 0 ? start : x, y - 5, pair ? w : w + lw, m.footerH, k[n], ch[n], fn[n]);
            x += w + 14;   // apart enough that a tap can't hit the neighbouring key
        }
        if (label.length()) {
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(C_DIM);
            gfx.drawString(label, x + 2, cy);
            x += lw - 10;
        }
        x += 22;
        i = j;
    }
}

void drawFooter(const char* hints) {
    const Metrics& m = metrics();
    gfx.fillRect(0, m.h - m.footerH, m.w, m.footerH, C_BG2);
    gfx.drawFastHLine(0, m.h - m.footerH, m.w, C_EDGE);
    gfx.setFont(m.small);
    if (wide()) { drawFooterButtons(keyHints(hints)); return; }
    gfx.setTextColor(C_DIM);
    gfx.setTextDatum(middle_left);
    gfx.drawString(keyHints(hints), m.pad / 2, m.h - m.footerH / 2);
}

void fillRound(int x, int y, int w, int h, int r, uint16_t color) {
    // Big ones: the middle by DMA (as two rectangles clear of the corners),
    // the corners by hand. Small ones aren't worth setting the DMA up for.
    if (wide() && w * h > 40000 && w > 2 * r && h > 2 * r && board::fill(gfx, x, y + r, w, h - 2 * r, color) &&
        board::fill(gfx, x + r, y, w - 2 * r, r, color) && board::fill(gfx, x + r, y + h - r, w - 2 * r, r, color)) {
        gfx.fillCircle(x + r, y + r, r, color);
        gfx.fillCircle(x + w - r - 1, y + r, r, color);
        gfx.fillCircle(x + r, y + h - r - 1, r, color);
        gfx.fillCircle(x + w - r - 1, y + h - r - 1, r, color);
        return;
    }
    gfx.fillRoundRect(x, y, w, h, r, color);
}

void drawPanel(int x, int y, int w, int h, uint16_t fill, uint16_t edge) {
    fillRound(x, y, w, h, u(4), fill);
    gfx.drawRoundRect(x, y, w, h, u(4), edge);
    if (wide()) gfx.drawRoundRect(x + 1, y + 1, w - 2, h - 2, u(4) - 1, edge);
}

void drawBar(int x, int y, int w, int h, float frac, uint16_t fg, uint16_t bg) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx.fillRect(x, y, w, h, bg);
    gfx.fillRect(x, y, (int)(w * frac + 0.5f), h, fg);
}

void toast(const char* msg, uint16_t color) {
    strncpy(s_toast, msg, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = 0;
    s_toastColor = color;
    s_toastUntil = millis() + 1400 + 40 * std::max(0, (int)strlen(s_toast) - 30);   // longer to read a longer one
}

void drawToast() {
    if (millis() > s_toastUntil) return;
    const Metrics& m = metrics();
    gfx.setFont(m.body);
    String msg = fitText(keyHints(s_toast), m.w - u(20));
    int tw = gfx.textWidth(msg) + 2 * m.pad;
    int th = m.line + u(4);
    int x = (m.w - tw) / 2, y = m.h - m.footerH - th - u(4);
    gfx.fillRoundRect(x, y, tw, th, u(3), C_BG);
    gfx.drawRoundRect(x, y, tw, th, u(3), s_toastColor);
    if (wide()) gfx.drawRoundRect(x + 1, y + 1, tw - 2, th - 2, u(3) - 1, s_toastColor);
    gfx.setTextColor(s_toastColor);
    gfx.setTextDatum(middle_center);
    gfx.drawString(msg, m.w / 2, y + th / 2);
}

static void polygon(int n, int cx, int cy, int r, float rot, uint16_t fill, uint16_t edge) {
    float px[12], py[12];
    for (int i = 0; i < n; i++) {
        float a = rot + (float)i * 2.0f * (float)M_PI / n - (float)M_PI / 2;
        px[i] = cx + r * cosf(a);
        py[i] = cy + r * sinf(a);
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        gfx.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], fill);
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        gfx.drawLine(px[i], py[i], px[j], py[j], edge);
    }
}

void drawDieShape(int sides, int cx, int cy, int r, uint16_t fill, uint16_t edge, float rot) {
    switch (sides) {
        case 4:  polygon(3, cx, cy + r / 6, r, rot, fill, edge); break;
        case 6: {
            polygon(4, cx, cy, r, rot + (float)M_PI / 4, fill, edge);
            break;
        }
        case 8:  polygon(4, cx, cy, r, rot, fill, edge); break;
        case 10:
        case 100: polygon(5, cx, cy, r, rot, fill, edge); break;
        case 12: polygon(10, cx, cy, r, rot, fill, edge); break;
        default: {
            polygon(6, cx, cy, r, rot, fill, edge);
            // inner facet lines give the d20 its gem look
            float a0 = rot - (float)M_PI / 2;
            for (int i = 0; i < 3; i++) {
                float a = a0 + i * 2.0f * (float)M_PI / 3;
                float b = a0 + (i + 1) * 2.0f * (float)M_PI / 3;
                gfx.drawLine(cx + r * 0.5f * cosf(a), cy + r * 0.5f * sinf(a),
                             cx + r * 0.5f * cosf(b), cy + r * 0.5f * sinf(b), edge);
                gfx.drawLine(cx + r * 0.5f * cosf(a), cy + r * 0.5f * sinf(a),
                             cx + r * cosf(a), cy + r * sinf(a), edge);
            }
            break;
        }
    }
}

void textCentered(const char* s, int cx, int y) {
    gfx.setTextDatum(top_center);
    gfx.drawString(s, cx, y);
}

int textWrapped(const char* s, int x, int y, int w, int lineH, int maxLines) {
    gfx.setTextDatum(top_left);
    char line[128];
    int lines = 0;
    const char* p = s;
    while (*p && lines < maxLines) {
        int len = 0, lastSpace = -1;
        while (p[len] && p[len] != '\n' && len < (int)sizeof(line) - 1) {
            line[len] = p[len];
            line[len + 1] = 0;
            if (gfx.textWidth(line) > w) break;
            if (p[len] == ' ') lastSpace = len;
            len++;
        }
        int take = len;
        if (p[len] && p[len] != '\n' && lastSpace > 0) take = lastSpace;
        memcpy(line, p, take);
        line[take] = 0;
        gfx.drawString(line, x, y);
        y += lineH;
        lines++;
        p += take;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return y;
}

void wrapLines(const String& s, int w, std::vector<String>& out) {
    int start = 0, n = s.length();
    while (start <= n) {
        int nl = s.indexOf('\n', start);
        String para = nl < 0 ? s.substring(start) : s.substring(start, nl);
        if (para.length() == 0) out.push_back("");
        while (para.length()) {
            // find longest prefix that fits (binary search: width grows with length),
            // breaking at a space when possible
            int fit = para.length();
            if (gfx.textWidth(para) > w) {
                int lo = 1, hi = para.length() - 1;   // lo always fits (or is the minimum)
                while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;
                    if (gfx.textWidth(para.substring(0, mid)) <= w) lo = mid;
                    else hi = mid - 1;
                }
                fit = lo;
            }
            if (fit < (int)para.length()) {
                int sp = para.lastIndexOf(' ', fit);
                if (sp > 0) fit = sp;
            }
            out.push_back(para.substring(0, fit));
            para = para.substring(fit);
            para.trim();
        }
        if (nl < 0) break;
        start = nl + 1;
    }
}

}  // namespace ui

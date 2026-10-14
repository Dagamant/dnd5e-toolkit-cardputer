#include "ui.h"
#include "web.h"
#include "../board/board.h"
#include <math.h>

namespace ui {

// &M5.Display (a plain member) is safe during static initialisation;
// M5Cardputer.Display is a reference that isn't set up yet at that point.
M5Canvas gfx(&M5.Display);

static char     s_toast[48];
static uint16_t s_toastColor = C_GOLD;
static uint32_t s_toastUntil = 0;

void begin() {
    M5.Display.setRotation(board::displayRotation());
    gfx.setColorDepth(16);
    gfx.createSprite(metrics().w, metrics().h);
    gfx.setTextWrap(false);
}

void present() {
    drawToast();
    gfx.pushSprite(0, 0);
}

void drawBackground() {
    const Metrics& m = metrics();
    gfx.fillScreen(C_BG);
    // subtle vignette lines for texture
    for (int y = m.headerH; y < m.h; y += 4) gfx.drawFastHLine(0, y, m.w, rgb(18, 16, 26));
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
    gfx.drawFastHLine(0, m.headerH - 1, m.w, accent);
    gfx.drawFastHLine(0, m.headerH - 2, m.w, C_GOLD_DK);
    gfx.setFont(m.title);
    gfx.setTextColor(accent);
    gfx.setTextDatum(middle_left);
    gfx.drawString(fitText(title, m.w - 34 - reserve), m.pad, m.headerH / 2);
    // battery
    // reading the battery takes ADC samples; once every few seconds is plenty
    static int lvl = -1;
    static uint32_t nextRead = 0;
    if (millis() >= nextRead) { lvl = M5.Power.getBatteryLevel(); nextRead = millis() + 5000; }
    if (lvl >= 0) {
        int bx = m.w - 24, by = (m.headerH - 8) / 2;
        gfx.drawRect(bx, by, 18, 8, C_DIM);
        gfx.fillRect(bx + 18, by + 2, 2, 4, C_DIM);
        uint16_t c = lvl > 50 ? C_EMERALD : (lvl > 20 ? C_GOLD : C_BLOOD);
        gfx.fillRect(bx + 2, by + 2, (14 * lvl) / 100, 4, c);
    }
    // web access running: a small WiFi mark left of the battery
    if (web::state() != web::OFF) {
        int wx = m.w - 31, wy = m.headerH / 2 + 3;
        uint16_t c = web::state() == web::RUNNING ? C_ICE : ((millis() / 300) % 2 ? C_DIM : C_EDGE);
        gfx.fillCircle(wx, wy, 1, c);
        gfx.drawArc(wx, wy, 4, 4, 225, 315, c);
        gfx.drawArc(wx, wy, 7, 7, 225, 315, c);
    }
}

void drawFooter(const char* hints) {
    const Metrics& m = metrics();
    gfx.fillRect(0, m.h - m.footerH, m.w, m.footerH, C_BG2);
    gfx.drawFastHLine(0, m.h - m.footerH, m.w, C_EDGE);
    gfx.setFont(m.body);
    gfx.setTextColor(C_DIM);
    gfx.setTextDatum(middle_left);
    gfx.drawString(keyHints(hints), m.pad / 2, m.h - m.footerH / 2);
}

void drawPanel(int x, int y, int w, int h, uint16_t fill, uint16_t edge) {
    gfx.fillRoundRect(x, y, w, h, 4, fill);
    gfx.drawRoundRect(x, y, w, h, 4, edge);
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
    s_toastUntil = millis() + 1400;
}

void drawToast() {
    if (millis() > s_toastUntil) return;
    const Metrics& m = metrics();
    gfx.setFont(m.body);
    String msg = fitText(keyHints(s_toast), m.w - 20);
    int tw = gfx.textWidth(msg) + 2 * m.pad;
    int th = m.line + 4;
    int x = (m.w - tw) / 2, y = m.h - m.footerH - th - 4;
    gfx.fillRoundRect(x, y, tw, th, 3, C_BG);
    gfx.drawRoundRect(x, y, tw, th, 3, s_toastColor);
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

#pragma once
#include <M5Unified.h>
#include <vector>

// Screen and frame buffer. Everything draws into `gfx`, then ui::present()
// pushes the whole frame to the panel at once (no flicker).
namespace ui {

// Sizes and fonts the shared widgets (header, footer, toast, TextView, Prompt,
// Wizard, roll overlay) lay themselves out with. CLASSIC is the 240x135 layout
// every app is drawn for; the Tab5 runs it scaled up first, and gets its own
// preset when the native-resolution UI arrives (docs/TAB5_PORT.md, phase 2).
struct Metrics {
    int w, h;                      // canvas size
    int headerH, footerH;          // title bar and key-hint bar
    int line;                      // body text line height
    int charW;                     // advance of the (monospace) body font
    int pad;                       // standard left/right margin
    const lgfx::IFont* body;       // small text, lists, hints
    const lgfx::IFont* title;      // headers, wizard questions
    const lgfx::IFont* big;        // roll totals, launcher title
};
constexpr Metrics CLASSIC{240, 135, 16, 11, 10, 6, 6, &fonts::Font0, &fonts::FreeSerifBold9pt7b,
                          &fonts::FreeSerifBold12pt7b};
inline const Metrics& metrics() { return CLASSIC; }

// Shorthands for the classic layout used by the apps' own drawing code.
constexpr int W = CLASSIC.w;
constexpr int H = CLASSIC.h;
constexpr int HEADER_H = CLASSIC.headerH;
constexpr int FOOTER_H = CLASSIC.footerH;

// Dark-fantasy palette (RGB565)
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t C_BG       = rgb(14, 12, 20);
constexpr uint16_t C_BG2      = rgb(28, 24, 38);
constexpr uint16_t C_PANEL    = rgb(40, 34, 52);
constexpr uint16_t C_EDGE     = rgb(84, 70, 100);
constexpr uint16_t C_TEXT     = rgb(232, 224, 210);
constexpr uint16_t C_DIM      = rgb(140, 130, 150);
constexpr uint16_t C_GOLD     = rgb(232, 176, 74);
constexpr uint16_t C_GOLD_DK  = rgb(140, 98, 32);
constexpr uint16_t C_BLOOD    = rgb(200, 48, 52);
constexpr uint16_t C_BLOOD_DK = rgb(90, 18, 24);
constexpr uint16_t C_EMERALD  = rgb(60, 200, 120);
constexpr uint16_t C_ARCANE   = rgb(120, 110, 240);
constexpr uint16_t C_ICE      = rgb(110, 200, 230);

extern M5Canvas gfx;

void begin();
void present();

// Common chrome
void drawBackground();
// Titles are trimmed to fit; `reserve` keeps px free on the right for a label.
void drawHeader(const char* title, uint16_t accent = C_GOLD, int reserve = 0);
// Key hints are written for the Cardputer ("Fn+X"); this swaps in the
// device's own modifier name (board::fnName()).
String keyHints(const char* text);
// Trims s (with a trailing ".") to fit maxW px in the current font.
String fitText(const String& s, int maxW);
void drawFooter(const char* hints);
void drawPanel(int x, int y, int w, int h, uint16_t fill = C_PANEL, uint16_t edge = C_EDGE);
void drawBar(int x, int y, int w, int h, float frac, uint16_t fg, uint16_t bg = C_BG2);
void toast(const char* msg, uint16_t color = C_GOLD);  // brief overlay message
void drawToast();

// Draws a die silhouette (d4..d20/d100) centred at cx,cy
void drawDieShape(int sides, int cx, int cy, int r, uint16_t fill, uint16_t edge, float rot = 0);

// Text helpers
void textCentered(const char* s, int cx, int y);
// Word-wrap `s` into width w starting at x,y; returns y after last line.
int  textWrapped(const char* s, int x, int y, int w, int lineH, int maxLines = 99);
// Splits text into lines that fit width w in the current font (honours '\n').
void wrapLines(const String& s, int w, std::vector<String>& out);

// Easing
inline float easeOut(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return 1 - (1 - t) * (1 - t) * (1 - t); }
inline float approach(float cur, float target, float rate) { return cur + (target - cur) * rate; }

}  // namespace ui

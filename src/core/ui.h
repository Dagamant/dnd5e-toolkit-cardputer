#pragma once
#include <M5Unified.h>
#include <vector>

// Screen and frame buffer. Everything draws into `gfx`, then ui::present()
// pushes the whole frame to the panel at once (no flicker).
namespace ui {

// Sizes and fonts the shared widgets (header, footer, toast, TextView, Prompt,
// Wizard, roll overlay) lay themselves out with. CLASSIC is the Cardputer's
// 240x135 layout. WIDE is the Tab5's native 1280x720 layout, used by apps that
// draw for it (App::wide()); other apps run in CLASSIC, scaled up 5x.
struct Metrics {
    int w, h;                      // canvas size
    int headerH, footerH;          // title bar and key-hint bar
    int line;                      // body text line height
    int charW;                     // advance of the (monospace) body font
    int pad;                       // standard left/right margin
    const lgfx::IFont* body;       // small text, lists, hints
    const lgfx::IFont* title;      // headers, wizard questions
    const lgfx::IFont* big;        // roll totals, launcher title
    const lgfx::IFont* small;      // key hints, secondary labels
    const lgfx::IFont* bold;       // body-sized emphasis
    int rowH;                      // list row height
    int s;                         // scale for hand-drawn shapes (icons, dice): 1 or 4
};
constexpr Metrics CLASSIC{240, 135, 16, 11, 10, 6, 6, &fonts::Font0, &fonts::FreeSerifBold9pt7b,
                          &fonts::FreeSerifBold12pt7b, &fonts::Font0, &fonts::Font0, 12, 1};
extern const Metrics WIDE;
const Metrics& metrics();
// True while the canvas is the big-screen 1280x720 one.
extern bool g_wide;
inline bool wide() { return g_wide; }
// A length from the classic 240x135 layout, in the current layout's pixels:
// unchanged on the Cardputer, x3.2 on the big screen (text there is 3.2x as
// tall). Lets one drawing routine serve both screens.
inline int u(int px) { return g_wide ? (px * 16 + (px >= 0 ? 2 : -2)) / 5 : px; }
// Touch-sized rows: on the big screen a list row is at least TOUCH_ROW px
// tall, so a fingertip can't land on its neighbour. touchRow(11) is the
// height for a row laid out as 11 classic px; touchPad(11) is how far to push
// its contents down to centre them. The Cardputer keeps its heights.
constexpr int TOUCH_ROW = 60;
inline int touchRow(int classicPx) { return g_wide ? std::max(u(classicPx), TOUCH_ROW) : classicPx; }
inline int touchPad(int classicPx) { return (touchRow(classicPx) - u(classicPx)) / 2; }
// The device has a big screen (the Tab5).
bool bigScreen();
// Switches the canvas between CLASSIC and WIDE (no-op on small screens).
void setWide(bool on);

// Shorthands for the current layout (set by setWide()).
inline int W = CLASSIC.w;
inline int H = CLASSIC.h;
inline int HEADER_H = CLASSIC.headerH;
inline int FOOTER_H = CLASSIC.footerH;
// The font to use for one picked for the classic layout (Font0, the
// FreeSerif sizes, ...): itself on the classic layout, a bigger one on WIDE.
const lgfx::IFont* font(const lgfx::IFont* classic);

// Tab5: a campfire-at-night palette (RGB565), taken from the home screen picture:
// night-sky navy, moonlit parchment text, firelight amber, embers, the
// forest and its fireflies, moonlight and the lake.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
#ifdef BOARD_TAB5
constexpr uint16_t C_BG       = rgb(8, 14, 28);
constexpr uint16_t C_BG2      = rgb(14, 24, 44);
constexpr uint16_t C_PANEL    = rgb(24, 38, 66);
constexpr uint16_t C_EDGE     = rgb(54, 74, 112);
constexpr uint16_t C_TEXT     = rgb(240, 228, 204);
constexpr uint16_t C_DIM      = rgb(140, 152, 176);
constexpr uint16_t C_GOLD     = rgb(244, 162, 56);
constexpr uint16_t C_GOLD_DK  = rgb(150, 80, 26);
constexpr uint16_t C_BLOOD    = rgb(216, 74, 44);
constexpr uint16_t C_BLOOD_DK = rgb(92, 26, 14);
constexpr uint16_t C_EMERALD  = rgb(140, 200, 80);
constexpr uint16_t C_ARCANE   = rgb(140, 160, 244);
constexpr uint16_t C_ICE      = rgb(100, 166, 232);
constexpr uint16_t C_FIREFLY  = rgb(230, 216, 96);
// the Cardputer keeps its original dark-fantasy palette
#else
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
constexpr uint16_t C_FIREFLY  = rgb(230, 216, 96);
#endif

extern M5Canvas gfx;

void begin();
void present();
// The next present() only needs to show (x, y, w, h): nothing else changed.
// Called more than once, it shows each of them.
void presentOnly(int x, int y, int w, int h);
bool toastShowing();

// Common chrome
void drawBackground();
// fillRoundRect, but big ones are filled by DMA where the board can.
void fillRound(int x, int y, int w, int h, int r, uint16_t color);
// Darkens a rounded rectangle of the canvas by alpha (0-255) toward color,
// leaving what is under it showing through.
void shade(int x, int y, int w, int h, int r, uint8_t alpha, uint16_t color = C_BG);
// Titles are trimmed to fit; `reserve` keeps px free on the right for a label.
void drawHeader(const char* title, uint16_t accent = C_GOLD, int reserve = 0);
// Where drawHeader puts the title; on the big screen a back button comes first.
int headerTitleX();
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

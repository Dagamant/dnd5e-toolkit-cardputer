// D&D 5E Toolkit — game-master and player tools for the M5Stack Cardputer ADV.
#include <M5Unified.h>
#include "board/board.h"
#include "core/app.h"
#include "core/debug_console.h"
#include "core/input.h"
#include "core/osk.h"
#include "core/hold_menu.h"
#include "core/clock.h"
#include "core/roll_overlay.h"
#include "core/settings.h"
#include "core/sfx.h"
#include "core/storage.h"
#include "core/touch.h"
#include "core/ui.h"
#include "core/web.h"
#include <math.h>
#include <vector>
#ifdef BOARD_TAB5
#include <SD.h>
#include "board/wallpaper_jpg.h"
#include "core/icons_px.h"
#endif

using namespace ui;

// The wizards and SD parsing nest fairly deep; give loop() a 16 KB stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

uint16_t App::accent() const { return C_GOLD; }

// Breadcrumbs survive a watchdog reset so we can report where a hang happened.
RTC_NOINIT_ATTR char g_crumb[48];
RTC_NOINIT_ATTR uint32_t g_crumbMagic;
static volatile uint32_t g_heartbeat = 0;
static volatile bool g_wdPaused = false;
void watchdogPause(bool paused) { g_wdPaused = paused; g_heartbeat = millis(); }
static const char* g_request = nullptr;
static uint32_t g_perfFrames = 0, g_perfSum = 0, g_perfMax = 0, g_perfHandleMax = 0, g_perfPush = 0;
void perfReport(char* out, size_t n) {
    snprintf(out, n, "frames %u  avg %u ms  max %u ms  max-key %u ms  avg-push %u ms", (unsigned)g_perfFrames,
             (unsigned)(g_perfFrames ? g_perfSum / g_perfFrames : 0), (unsigned)g_perfMax, (unsigned)g_perfHandleMax,
             (unsigned)(g_perfFrames ? g_perfPush / g_perfFrames : 0));
    g_perfFrames = g_perfSum = g_perfMax = g_perfHandleMax = g_perfPush = 0;
}
void requestApp(const char* name) { g_request = name; }
static void crumb(const char* a, const char* b) {
    snprintf(g_crumb, sizeof(g_crumb), "%s:%s", a, b);
    g_crumbMagic = 0xC0FFEE42;
    g_heartbeat = millis();
}

// Runs on core 0. If the main loop stops making progress, report where and reboot.
static void watchdogTask(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!g_wdPaused && g_heartbeat && millis() - g_heartbeat > 4000) {
            for (int i = 0; i < 3; i++) {
                Serial.printf("HANG at %s\n", g_crumb);
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            esp_restart();  // reset reason SW; crumb kept in RTC memory
        }
    }
}

namespace {

std::vector<App*> g_apps;
App*     g_active = nullptr;
int      g_sel = 0;
float    g_scroll = 0;      // animated carousel position
uint32_t g_lastFrame = 0;
uint32_t g_t0 = 0;
uint32_t g_transition = 0;  // ms remaining of the open/close wipe
bool     g_dimmed = false;
uint8_t  g_screenLevel = 0;   // Tab5 screen timeout: 0 on, 1 dimmed, 2 off

constexpr uint32_t DIM_AFTER_MS = 90 * 1000;
constexpr uint32_t IDLE_SLOW_MS = 4000;

// Home screen groups, in carousel order.
struct Group { const char* name; uint16_t color; };
const Group GROUPS[] = {{"At the table", C_BLOOD}, {"Prep", rgb(120, 200, 160)}, {"Reference", C_ARCANE},
                        {"System", C_DIM}};
std::vector<uint8_t> g_group;   // group of each app
int g_listTop = 0;

int groupStart(int g) {
    for (int i = 0; i < (int)g_apps.size(); i++) if (g_group[i] == g) return i;
    return 0;
}

void drawBackdrop(float t) {
    gfx.fillScreen(C_BG);
    // starfield-ish backdrop that drifts slowly
    for (int i = 0; i < 40; i++) {
        uint32_t h = i * 2654435761u;
        int x = (int)((h % W) + t * (2 + (h >> 8) % 4)) % W;
        int y = (h >> 12) % H;
        gfx.drawPixel(x, y, (h & 1) ? C_EDGE : C_DIM);
    }
}

void drawCarousel() {
    float t = (millis() - g_t0) / 1000.0f;
    drawBackdrop(t);
    gfx.setFont(ui::font(&fonts::FreeSerifBold12pt7b));
    gfx.setTextDatum(top_center);
    gfx.setTextColor(C_GOLD_DK);
    gfx.drawString("D&D 5E Toolkit", W / 2 + 1, 1);
    gfx.setTextColor(C_GOLD);
    gfx.drawString("D&D 5E Toolkit", W / 2, 0);
    gfx.setFont(ui::font(&fonts::Font0));
    gfx.setTextColor(C_DIM);
    if (!storage::ok()) { gfx.setTextDatum(top_left); gfx.drawString("no SD", 4, 4); }

    // group label with Tab hints
    const Group& grp = GROUPS[g_group[g_sel]];
    gfx.setTextDatum(top_center);
    gfx.setTextColor(grp.color);
    String gl = String("- ") + grp.name + " -";
    gfx.drawString(gl, W / 2, 23);
    gfx.setTextColor(C_EDGE);
    gfx.drawString("< Tab >", W / 2 + gfx.textWidth(gl) / 2 + 26, 23);

    // carousel
    const int cy = 66, spacing = 74;
    int n = g_apps.size();
    for (int k = -2; k <= 2; k++) {
        int idx = ((int)roundf(g_scroll) + k + n * 4) % n;
        float off = (roundf(g_scroll) + k) - g_scroll;
        int cx = W / 2 + (int)(off * spacing);
        float d = fabsf(off);
        if (cx < -40 || cx > W + 40) continue;
        bool focus = d < 0.5f;
        int r = focus ? 30 : 22;
        uint16_t accent = g_apps[idx]->accent();
        gfx.fillRoundRect(cx - r, cy - r, r * 2, r * 2, 8, focus ? C_PANEL : C_BG2);
        gfx.drawRoundRect(cx - r, cy - r, r * 2, r * 2, 8, focus ? accent : C_EDGE);
        if (focus) gfx.drawRoundRect(cx - r - 2, cy - r - 2, r * 2 + 4, r * 2 + 4, 10, C_GOLD_DK);
        g_apps[idx]->drawIcon(cx, cy, focus ? t : 0);
    }

    App* a = g_apps[g_sel];
    gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
    gfx.setTextColor(a->accent());
    gfx.setTextDatum(top_center);
    gfx.drawString(a->name(), W / 2, 100);
    gfx.setFont(ui::font(&fonts::Font0));
    gfx.setTextColor(C_DIM);
    gfx.drawString(a->blurb(), W / 2, 116);

    // page dots, with a gap between groups; the current group is tinted
    int gaps = 0;
    for (int i = 1; i < n; i++) gaps += g_group[i] != g_group[i - 1];
    int dx = W / 2 - (n * 6 + gaps * 6) / 2;
    for (int i = 0; i < n; i++) {
        if (i && g_group[i] != g_group[i - 1]) dx += 6;
        bool cur = i == g_sel;
        uint16_t c = cur ? C_GOLD : g_group[i] == g_group[g_sel] ? GROUPS[g_group[i]].color : C_EDGE;
        gfx.fillCircle(dx + i * 6 + 3, 129, cur ? 2 : 1, c);
    }
}

// All apps as a grouped list: one row per app with its blurb.
void drawList() {
    drawBackdrop((millis() - g_t0) / 1000.0f);
    drawHeader("All tools", C_GOLD);
    struct Row { int app; int group; };   // app -1 = group heading
    std::vector<Row> rows;
    int selRow = 0;
    for (int i = 0; i < (int)g_apps.size(); i++) {
        if (!i || g_group[i] != g_group[i - 1]) rows.push_back({-1, g_group[i]});
        if (i == g_sel) selRow = rows.size();
        rows.push_back({i, g_group[i]});
    }
    const int rowH = 12, y0 = HEADER_H + 2, vis = (H - FOOTER_H - y0) / rowH;
    if (selRow - 1 < g_listTop) g_listTop = std::max(0, selRow - 1);   // keep the heading in view
    if (selRow >= g_listTop + vis) g_listTop = selRow - vis + 1;
    gfx.setFont(ui::font(&fonts::Font0));
    for (int r = g_listTop; r < (int)rows.size() && r < g_listTop + vis; r++) {
        int y = y0 + (r - g_listTop) * rowH;
        const Row& row = rows[r];
        gfx.setTextDatum(top_left);
        if (row.app < 0) {
            gfx.setTextColor(GROUPS[row.group].color);
            gfx.drawString(GROUPS[row.group].name, 4, y + 2);
            int lw = gfx.textWidth(GROUPS[row.group].name);
            gfx.drawFastHLine(8 + lw, y + 5, W - 14 - lw, C_EDGE);
            continue;
        }
        App* a = g_apps[row.app];
        bool s = row.app == g_sel;
        if (s) gfx.fillRoundRect(2, y, W - 4, rowH - 1, 3, C_PANEL);
        gfx.fillCircle(11, y + 5, 2, a->accent());
        gfx.setTextColor(s ? C_TEXT : rgb(205, 198, 186));
        gfx.drawString(a->name(), 18, y + 2);
        gfx.setTextColor(s ? C_GOLD : C_DIM);
        gfx.drawString(fitText(a->blurb(), W - 112), 104, y + 2);
    }
    drawFooter("Ent open  A-Z jump  Tab group  Spc icons");
}

// ---- big screen (ui::WIDE): every tool at once, one column per group

constexpr int ICON = 56;                    // captured icon size (classic px)
std::vector<lgfx::swap565_t*> g_icons;      // one per app, drawn at start-up
constexpr uint16_t ICON_KEY = 0xF81F;        // magenta: the see-through part of an icon
lgfx::swap565_t g_iconKey;                   // ICON_KEY as the captured icons store it
constexpr int PX_ZOOM = 4;                   // pixel-art icons: each pixel 4x4
std::vector<lgfx::swap565_t*> g_pxIcons;     // big screen: one per app (null: use g_icons)

// Icons are drawn for the 240x135 canvas; capture each once and scale it up.
void captureIcons() {
    ui::setWide(false);
    gfx.drawPixel(0, 0, ICON_KEY);
    gfx.readRect(0, 0, 1, 1, &g_iconKey);
    for (App* a : g_apps) {
        gfx.fillRect(0, 0, ICON, ICON, ICON_KEY);
        a->drawIcon(ICON / 2, ICON / 2, 0);
        auto* buf = (lgfx::swap565_t*)heap_caps_malloc(ICON * ICON * 2, MALLOC_CAP_SPIRAM);
        if (buf) gfx.readRect(0, 0, ICON, ICON, buf);
        g_icons.push_back(buf);
    }
    ui::setWide(true);
#ifdef BOARD_TAB5
    // the pixel-art icons, scaled up once
    const int big = iconpx::SIZE * PX_ZOOM;
    for (App* a : g_apps) {
        lgfx::swap565_t* buf = nullptr;
        for (auto& ic : iconpx::ICONS) {
            if (strcmp(ic.app, a->name())) continue;
            gfx.fillRect(0, 0, big, big, ICON_KEY);
            for (int i = 0; i < iconpx::SIZE * iconpx::SIZE; i++)
                if (ic.px[i] != iconpx::CLEAR)
                    gfx.fillRect(i % iconpx::SIZE * PX_ZOOM, i / iconpx::SIZE * PX_ZOOM, PX_ZOOM, PX_ZOOM, ic.px[i]);
            buf = (lgfx::swap565_t*)heap_caps_malloc(big * big * 2, MALLOC_CAP_SPIRAM);
            if (buf) gfx.readRect(0, 0, big, big, buf);
        }
        g_pxIcons.push_back(buf);
    }
#endif
}

void select(int i);
void openApp(App* a);

constexpr int GRID_COLS = 7;

// The home screen picture with the icon cells darkened over it, built once
// and copied in each frame. A 1280x720 /wallpaper.jpg on the SD card replaces
// the built-in one. False when there is none (or no memory for it).
bool drawWallpaper(int cellW, int cellH, int top, int rows) {
#ifdef BOARD_TAB5
    static uint16_t* s_wall = nullptr;
    static bool s_tried = false;
    const Metrics& m = metrics();
    const size_t bytes = (size_t)m.w * m.h * 2;
    if (!s_tried) {
        s_tried = true;
        gfx.fillScreen(C_BG);
        bool ok = false;
        if (storage::ok() && SD.exists("/wallpaper.jpg")) {
            File f = SD.open("/wallpaper.jpg");
            size_t len = f ? f.size() : 0;
            auto* jpg = len ? (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : nullptr;
            if (jpg && f.read(jpg, len) == len) ok = gfx.drawJpg(jpg, len, 0, 0, m.w, m.h);
            free(jpg);
            f.close();
        }
        if (!ok) ok = gfx.drawJpg(WALLPAPER_JPG, sizeof(WALLPAPER_JPG), 0, 0, m.w, m.h);
        if (!ok) return false;
        // somewhere to read the names: each cell, and the line under the grid
        const int n = g_apps.size();
        for (int i = 0; i < n; i++) {
            int cx = m.pad + (i % GRID_COLS) * cellW + cellW / 2, y = top + (i / GRID_COLS) * cellH;
            ui::shade(cx - (cellW - 16) / 2, y, cellW - 16, cellH - 12, 14, 130);
        }
        gfx.fillRectAlpha(0, top + rows * cellH - 2, m.w, 48, 170, C_BG);
        s_wall = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (s_wall) memcpy(s_wall, gfx.getBuffer(), bytes);
    }
    if (!s_wall) return false;
    memcpy(gfx.getBuffer(), s_wall, bytes);
    return true;
#else
    return false;
#endif
}

// The display still shows the last home screen frame, nothing over it.
static bool g_homeShown = false;

struct Box { int16_t x, y, w, h; };
constexpr int MAX_BOXES = 32;

// Fireflies over the home picture: the ones painted into it pulse, and a
// few more wander about, blinking on and off. Where each one drew goes in
// boxes, for the next frame to put back.
void drawFireflies(Box* boxes, int& nb) {
    nb = 0;
#ifdef BOARD_TAB5
    struct Spot { int16_t x, y; };
    static const Spot STILL[] = {{111, 12}, {67, 68}, {528, 106}, {31, 164}, {367, 176}, {633, 225}, {47, 233},
                                 {1173, 301}, {150, 306}, {827, 334}, {761, 376}, {912, 406}, {1158, 420}, {854, 472}};
    const float t = millis() / 1000.0f;
    const uint16_t core = rgb(255, 250, 170);
    // a soft square glow, pixel-art style; b is 0..1
    auto glow = [&](int x, int y, float b) {
        if (b <= 0.02f) return;
        if (nb < MAX_BOXES) boxes[nb++] = {(int16_t)(x - 16), (int16_t)(y - 16), 32, 32};
        static const uint8_t R[] = {16, 11, 7};
        static const uint8_t A[] = {40, 60, 90};
        for (int i = 0; i < 3; i++) gfx.fillRectAlpha(x - R[i], y - R[i], 2 * R[i], 2 * R[i], (uint8_t)(A[i] * b), C_FIREFLY);
        gfx.fillRectAlpha(x - 4, y - 4, 8, 8, (uint8_t)(255 * std::min(1.0f, b * 1.4f)), core);
    };
    for (int i = 0; i < (int)(sizeof(STILL) / sizeof(STILL[0])); i++) {
        const float ph = i * 2.39f, b = 0.5f + 0.5f * sinf(t * (1.1f + (i % 5) * 0.23f) + ph);
        if (b < 0.35f) {   // fading: dim the painted one
            if (nb < MAX_BOXES) boxes[nb++] = {(int16_t)(STILL[i].x - 7), (int16_t)(STILL[i].y - 7), 14, 14};
            gfx.fillRectAlpha(STILL[i].x - 7, STILL[i].y - 7, 14, 14, (uint8_t)((0.35f - b) * 420), C_BG);
        } else
            glow(STILL[i].x, STILL[i].y, (b - 0.35f) / 0.65f);
    }
    const Metrics& m = metrics();
    for (int i = 0; i < 14; i++) {
        const uint32_t h = (i + 1) * 2654435761u;
        const float bx = 60 + h % (m.w - 120), byy = 60 + (h >> 11) % (m.h - 140);
        const float px = 9 + (h >> 3) % 9, py = 7 + (h >> 7) % 8, ph = (h >> 15) % 628 / 100.0f;
        const float x = bx + sinf(t * 6.2832f / px + ph) * 90 + sinf(t * 1.7f + ph) * 8;
        const float y = byy + sinf(t * 6.2832f / py + ph * 1.3f) * 50 + cosf(t * 1.3f + ph) * 6;
        // a blink every few seconds: up quickly, hold, fade
        const float period = 3.0f + (h >> 19) % 30 / 10.0f, c = fmodf(t + ph, period) / period;
        const float b = c < 0.1f ? c / 0.1f : c < 0.35f ? 1 : c < 0.6f ? 1 - (c - 0.35f) / 0.25f : 0;
        glow((int)x, (int)y, b);
    }
#endif
}


void drawLauncherWide() {
    const Metrics& m = metrics();
    const int n = g_apps.size(), rows = (n + GRID_COLS - 1) / GRID_COLS;
    const int cellW = (m.w - 2 * m.pad) / GRID_COLS, cellH = 176, top = 84;
    const int big = 24 * PX_ZOOM;
    auto cellAt = [&](int i, int& cx, int& y) { cx = m.pad + (i % GRID_COLS) * cellW + cellW / 2; y = top + (i / GRID_COLS) * cellH; };
    const bool pxSel = g_sel < (int)g_pxIcons.size() && g_pxIcons[g_sel];

    // Everything but the fireflies and the bobbing icon is kept until the
    // selection changes: composing it is ~50 ms and even copying it in is
    // ~45 ms. While the screen still shows the home screen, only the bits the
    // fireflies and the icon touched last frame are put back, and only those
    // and this frame's are sent to the display.
    static Box s_prev[MAX_BOXES + 2];
    static int s_prevN = 0;
    Box boxes[MAX_BOXES + 2];
    int nb = 0;
#ifdef BOARD_TAB5
    static uint16_t* s_home = nullptr;
    static int s_homeSel = -1;
    static bool s_homeSd = false;
    const size_t bytes = (size_t)m.w * m.h * 2;
    const bool cached = s_home && s_homeSel == g_sel && s_homeSd == storage::ok();
    const bool partial = cached && g_homeShown;
    if (partial) {
        uint16_t* buf = (uint16_t*)gfx.getBuffer();
        for (int i = 0; i < s_prevN; i++) {
            const Box& b = s_prev[i];
            const int x0 = std::max(0, (int)b.x), x1 = std::min(m.w, b.x + b.w);
            for (int y = std::max(0, (int)b.y); y < std::min(m.h, b.y + b.h) && x1 > x0; y++)
                memcpy(buf + y * m.w + x0, s_home + y * m.w + x0, (x1 - x0) * 2);
            ui::presentOnly(b.x, b.y, b.w, b.h);
        }
    } else if (cached) {
        memcpy(gfx.getBuffer(), s_home, bytes);
    }
#else
    const bool cached = false, partial = false;
#endif
    if (!cached) {
        const bool wall = drawWallpaper(cellW, cellH, top, rows);
        if (!wall) {
            if (!board::fill(gfx, 0, 0, m.w, m.h, C_BG)) gfx.fillScreen(C_BG);
            for (int i = 0; i < 160; i++) {
                uint32_t h = i * 2654435761u;
                gfx.fillRect(h % m.w, (h >> 12) % m.h, 2, 2, (h & 1) ? C_EDGE : C_DIM);
            }
        }
        gfx.setFont(m.big);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_BG);
        gfx.drawString("D&D 5E Toolkit", m.w / 2 + 4, 16);
        gfx.setTextColor(C_GOLD_DK);
        gfx.drawString("D&D 5E Toolkit", m.w / 2 + 2, 14);
        gfx.setTextColor(C_GOLD);
        gfx.drawString("D&D 5E Toolkit", m.w / 2, 12);
        if (!storage::ok()) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_BLOOD);
            gfx.drawString("no SD card", m.w - m.pad, 20);
        }

        // icon grid
        for (int i = 0; i < n; i++) {
            App* a = g_apps[i];
            bool sel = i == g_sel;
            int cx, y;
            cellAt(i, cx, y);
            int tw = cellW - 16, th = cellH - 12, tx = cx - tw / 2;
            if (sel) {
                ui::shade(tx, y, tw, th, 14, 110, C_PANEL);
                gfx.drawRoundRect(tx, y, tw, th, 14, a->accent());
                gfx.drawRoundRect(tx - 3, y - 3, tw + 6, th + 6, 16, C_GOLD_DK);
            }
            if (i < (int)g_pxIcons.size() && g_pxIcons[i]) {
                if (!sel) gfx.pushImage(cx - big / 2, y + 18, big, big, g_pxIcons[i], g_iconKey);
            } else if (g_icons[i]) {
                float z = sel ? 2.0f : 1.8f;
                gfx.pushImageRotateZoom(cx, y + 64, ICON / 2, ICON / 2, 0, z, z, ICON, ICON, g_icons[i], g_iconKey);
            }
            gfx.setFont(m.small);
            gfx.setTextDatum(top_center);
            String label = fitText(a->name(), tw - 8);
            gfx.setTextColor(C_BG);
            gfx.drawString(label, cx + 2, y + 130);
            gfx.setTextColor(sel ? a->accent() : C_TEXT);
            gfx.drawString(label, cx, y + 128);
        }

        // the selected tool's description
        App* a = g_apps[g_sel];
        int by = top + rows * cellH + 4;
        gfx.setFont(m.body);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(fitText(a->blurb(), m.w - 2 * m.pad), m.w / 2, by);
        drawFooter("Arrows move  Ent open  A-Z jump  1-9 open");
#ifdef BOARD_TAB5
        if (wall) {
            if (!s_home) s_home = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
            if (s_home) memcpy(s_home, gfx.getBuffer(), bytes);
            s_homeSel = g_sel;
            s_homeSd = storage::ok();
        }
#endif
    }

    drawFireflies(boxes, nb);
    // the selected one bobs, in front of the fireflies
    if (pxSel) {
        int cx, y;
        cellAt(g_sel, cx, y);
        const int bob = (int)(fabsf(sinf(millis() / 260.0f)) * 6);
        gfx.pushImage(cx - big / 2, y + 18 - bob, big, big, g_pxIcons[g_sel], g_iconKey);
        boxes[nb++] = {(int16_t)(cx - big / 2), (int16_t)(y + 12), (int16_t)big, (int16_t)(big + 6)};
    }
#ifdef BOARD_TAB5
    // the time and date, top left (put back and drawn again every frame, as
    // fireflies fly through it)
    if (wallclock::valid()) {
        const String tm = wallclock::timeText(), dt = wallclock::dateText();
        gfx.setTextDatum(top_left);
        gfx.setFont(m.title);
        gfx.setTextColor(C_BG);
        gfx.drawString(tm, m.pad + 2, 12);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(tm, m.pad, 10);
        gfx.setFont(m.small);
        gfx.setTextColor(C_BG);
        gfx.drawString(dt, m.pad + 2, 52);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(dt, m.pad, 50);
        boxes[nb++] = {0, 6, 300, 72};
    }
#endif
    if (partial) for (int i = 0; i < nb; i++) ui::presentOnly(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h);
    memcpy(s_prev, boxes, sizeof(Box) * nb);
    s_prevN = nb;
    for (int i = 0; i < n; i++) {
        int cx, y;
        cellAt(i, cx, y);
        const int tw = cellW - 16, th = cellH - 12;
        touch::zone(cx - tw / 2, y, tw, th, [i] { select(i); openApp(g_apps[i]); });
    }
}

// Left/Right step through the grid, Up/Down move a row (staying in the column).
void moveWide(int d) {
    int n = g_apps.size(), i = g_sel + d;
    if (d == GRID_COLS || d == -GRID_COLS) {
        int col = g_sel % GRID_COLS, rows = (n + GRID_COLS - 1) / GRID_COLS;
        int r = (g_sel / GRID_COLS + (d > 0 ? 1 : rows - 1)) % rows;
        i = r * GRID_COLS + col;
        if (i >= n) i = d > 0 ? col : (rows - 2) * GRID_COLS + col;   // short last row
    }
    select(i);
}

void drawLauncher() {
    if (ui::wide()) drawLauncherWide();
    else if (settings::cur.launcherList) drawList();
    else drawCarousel();
}

void openApp(App* a) {
    touch::clear();
    ui::setWide(a->bigLayout());
    g_active = a;
    a->onEnter();
    g_transition = 220;
    sfx::select();
    if (strcmp(settings::cur.lastApp, a->name())) {
        strncpy(settings::cur.lastApp, a->name(), sizeof(settings::cur.lastApp) - 1);
        settings::saveLauncher();
    }
}

void closeApp() {
    if (!g_active) return;
    g_active->onExit();
    g_active = nullptr;
    touch::clear();
    ui::setWide(true);
    input::setTextMode(false);
    g_transition = 220;
    sfx::back();
}

// Move the selection; keeps the carousel animation wrapping smoothly.
void select(int i) {
    int n = g_apps.size();
    i = (i % n + n) % n;
    if (i == g_sel) return;
    int d = i - g_sel;
    if (abs(d) > n / 2) g_scroll += d > 0 ? n : -n;   // shortest way round
    g_sel = i;
    sfx::click();
}

void handleLauncher(const input::Event& e) {
    int n = g_apps.size();
    if (ui::wide()) {
        if (e.key == input::K_UP) { moveWide(-GRID_COLS); return; }
        if (e.key == input::K_DOWN) { moveWide(GRID_COLS); return; }
        if (e.key == input::K_LEFT) { moveWide(-1); return; }
        if (e.key == input::K_RIGHT) { moveWide(1); return; }
        if (e.key == input::K_CHAR && e.ch == ' ') return;   // the carousel/list switch is for the small screen
    }
    switch (e.key) {
        case input::K_LEFT:
        case input::K_UP: select(g_sel - 1); break;
        case input::K_RIGHT:
        case input::K_DOWN: select(g_sel + 1); break;
        case input::K_TAB: {
            // next / previous group (Shift+Tab)
            int ng = sizeof(GROUPS) / sizeof(GROUPS[0]);
            int g = g_group[g_sel];
            if (e.shift) g = g_sel != groupStart(g) ? g : (g + ng - 1) % ng;
            else g = (g + 1) % ng;
            select(groupStart(g));
            break;
        }
        case input::K_ENTER: openApp(g_apps[g_sel]); break;
        case input::K_CHAR:
            if (e.ch == ' ') { settings::cur.launcherList = !settings::cur.launcherList; settings::saveLauncher(); sfx::select(); }
            // number keys open an app directly
            else if (e.ch >= '1' && e.ch <= '9' && e.ch - '1' < n) { select(e.ch - '1'); openApp(g_apps[g_sel]); }
            else if (e.ch == '0' && n >= 10) { select(9); openApp(g_apps[g_sel]); }
            else if (isalpha((unsigned char)e.ch)) {
                // letters jump to the next app starting with that letter
                for (int k = 1; k <= n; k++) {
                    int i = (g_sel + k) % n;
                    if (tolower(g_apps[i]->name()[0]) == tolower(e.ch)) { select(i); break; }
                }
            }
            break;
        default: break;
    }
}

}  // namespace

void setup() {
    esp_reset_reason_t why = esp_reset_reason();
    board::begin();
    wallclock::begin();
    Serial.setRxBufferSize(4096);  // room for SD uploads from the debug console
    Serial.begin(115200);
    settings::load();
    ui::begin();
    sfx::begin();
    settings::apply();
    input::begin();
    storage::begin();

    // grouped as on the home screen: 0 at the table, 1 prep, 2 reference, 3 system
    const std::pair<App*, uint8_t> apps[] = {
        {makeDiceApp(), 0}, {makeInitiativeApp(), 0}, {makeCharactersApp(), 0}, {makeNotesApp(), 0},
        {makeCampaignApp(), 1}, {makeEncountersApp(), 1}, {makeMonstersApp(), 1}, {makeDungeonApp(), 1},
#ifdef BOARD_TAB5
        {makeDungeon23App(), 1},
        {makeHexMapApp(), 1},
#endif
        {makeTablesApp(), 1}, {makeTravelApp(), 1}, {makeShopApp(), 1},
        {makeSpellsApp(), 2}, {makeItemsApp(), 2}, {makeRulesApp(), 2}, {makeHazardsApp(), 2},
        {makeArchiveApp(), 3}, {makeWebApp(), 3}, {makeFoundryApp(), 3}, {makeSettingsApp(), 3}};
    for (auto& a : apps) { g_apps.push_back(a.first); g_group.push_back(a.second); }
    if (ui::bigScreen()) captureIcons();
    // start on the app used last
    for (int i = 0; i < (int)g_apps.size(); i++)
        if (!strcmp(g_apps[i]->name(), settings::cur.lastApp)) g_sel = g_scroll = i;
    g_t0 = g_lastFrame = millis();
    if (g_crumbMagic == 0xC0FFEE42 && (why == ESP_RST_SW || why == ESP_RST_TASK_WDT || why == ESP_RST_PANIC || why == ESP_RST_INT_WDT)) {
        g_crumb[sizeof(g_crumb) - 1] = 0;
        Serial.printf("LAST CRASH reason=%d at %s\n", (int)why, g_crumb);
        storage::appendLine("/gmtools/crash.txt", String("reason ") + (int)why + " at " + g_crumb);
    }
    g_crumbMagic = 0;
    xTaskCreatePinnedToCore(watchdogTask, "hangwd", 3072, nullptr, 1, nullptr, 0);
}

void loop() {
    crumb("main", "update");
    board::update();
    crumb("main", "input");
    input::update();
    crumb("main", "console");
    debug_console::update();
    crumb("main", "sfx");
    sfx::update();
    crumb("main", "web");
    web::update();
    wallclock::update();

    uint32_t now = millis();
    uint32_t dt = now - g_lastFrame;
    g_lastFrame = now;

#ifdef BOARD_TAB5
    // Screen timeout (Settings): dim 10 s before it as a warning, then turn
    // the backlight off. Any key or touch wakes it and is swallowed.
    {
        const uint32_t off = settings::cur.screenTimeout * 1000UL, idleMs = now - input::lastActivity();
        const uint8_t level = !off ? 0 : idleMs > off ? 2 : idleMs > off - 10000 ? 1 : 0;   // 0 on, 1 dim, 2 off
        if (level != g_screenLevel) {
            if (!level) { input::Event e; while (input::poll(e)) {} }
            g_screenLevel = level;
            M5.Display.setBrightness(level == 2 ? 0 : level == 1 ? 20 : settings::cur.brightness);
            input::setAsleep(level != 0);
        }
    }
#else
    // Dim the backlight when idle; any key wakes it (and is swallowed).
    bool idle = now - input::lastActivity() > DIM_AFTER_MS;
    if (idle != g_dimmed) {
        g_dimmed = idle;
        M5.Display.setBrightness(idle ? 20 : settings::cur.brightness);
        if (!idle) { input::Event e; while (input::poll(e)) {} }
    }
#endif

    input::Event e;
    uint32_t handleStart = millis();
    bool hadEvent = false;
    while (input::poll(e)) {
        hadEvent = true;
        if (e.key == input::K_TAP) { touch::tap(e.x, e.y); continue; }
        if (holdmenu::handle(e)) continue;
        if (rollui::handle(e)) continue;
        if (g_active) {
            crumb(g_active->name(), "handle");
            if (!g_active->handle(e)) closeApp();
        } else {
            handleLauncher(e);
        }
        if (g_request) {
            for (size_t i = 0; i < g_apps.size(); i++)
                if (!strcmp(g_apps[i]->name(), g_request)) {
                    if (g_active) g_active->onExit();
                    g_sel = g_scroll = i;
                    openApp(g_apps[i]);
                    break;
                }
            g_request = nullptr;
        }
    }

    g_perfHandleMax = std::max<uint32_t>(g_perfHandleMax, millis() - handleStart);
    if (g_active) {
        crumb(g_active->name(), "tick");
        g_active->tick(dt);
    }

    touch::update(dt);   // a flicked list coasting

#ifdef BOARD_TAB5
    if (g_screenLevel == 2) { rollui::tick(dt); delay(50); return; }   // dark: nothing to draw
#endif

    // Big screen: a frame costs ~70 ms, so draw only when something may have
    // changed: on input and for a moment after (animations), while dice roll,
    // and otherwise 4 times a second (timers, web and Foundry updates). The
    // home screen keeps drawing for its fireflies.
    static uint32_t s_lastDraw = 0;
    if (ui::wide() && !hadEvent && now - input::lastActivity() > 1500 && !rollui::animating() && !touch::scrolling() &&
        !(g_active && g_active->animating()) && g_active && now - s_lastDraw < 250) {
        rollui::tick(dt);
        delay(16);
        return;
    }
    s_lastDraw = now;
    touch::clear();   // the screens below register what can be tapped as they draw

    if (rollui::drawsAlone()) {
        // the roll popup covers the app, which hasn't changed underneath
    } else if (g_active) {
        crumb(g_active->name(), "draw");
        g_active->draw();
    } else {
        crumb("launcher", "draw");
        g_scroll = approach(g_scroll, (float)g_sel, std::min(1.0f, dt * 0.018f));
        if (fabsf(g_scroll - g_sel) < 0.002f) g_scroll = g_sel;
        drawLauncher();
    }

    // iris wipe on app open/close
    if (g_transition && ui::wide()) g_transition = 0;
    if (g_transition) {
        float f = g_transition / 220.0f;
        int r = (int)(f * 150);
        for (int i = 0; i < 6; i++) gfx.drawCircle(W / 2, H / 2, 140 - r + i * 3, C_BG);
        if (r > 0) gfx.fillRect(0, 0, W, (int)(f * H / 2), C_BG), gfx.fillRect(0, H - (int)(f * H / 2), W, H, C_BG);
        g_transition = dt >= g_transition ? 0 : g_transition - dt;
    }

    rollui::tick(dt);
    rollui::draw();
    holdmenu::draw();
    osk::draw();
    crumb("main", "present");
    uint32_t pushStart = millis();
    ui::present();
    g_homeShown = !g_active && !g_transition && !rollui::active() && !holdmenu::active() && !osk::visible() &&
                  !ui::toastShowing();
    g_perfPush += millis() - pushStart;

    // ~60 fps while in use; ~25 fps after a few idle seconds (saves power,
    // animations still look smooth)
    uint32_t frameMs = now - input::lastActivity() > IDLE_SLOW_MS && !rollui::animating() ? 40 : 16;
    uint32_t spent = millis() - now;
    g_perfFrames++;
    g_perfSum += spent;
    g_perfMax = std::max(g_perfMax, spent);
    if (spent < frameMs) delay(frameMs - spent);
}

App* findApp(const char* name) {
    for (App* a : g_apps) if (!strcmp(a->name(), name)) return a;
    return nullptr;
}
bool appOpen(const App* a) { return a && a == g_active; }
void forEachApp(const std::function<void(App*)>& fn) { for (App* a : g_apps) fn(a); }

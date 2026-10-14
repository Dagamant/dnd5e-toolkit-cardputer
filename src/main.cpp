// D&D 5E Toolkit — game-master and player tools for the M5Stack Cardputer ADV.
#include <M5Unified.h>
#include "board/board.h"
#include "core/app.h"
#include "core/debug_console.h"
#include "core/input.h"
#include "core/roll_overlay.h"
#include "core/settings.h"
#include "core/sfx.h"
#include "core/storage.h"
#include "core/ui.h"
#include "core/web.h"
#include <math.h>
#include <vector>

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
    gfx.setFont(&fonts::FreeSerifBold12pt7b);
    gfx.setTextDatum(top_center);
    gfx.setTextColor(C_GOLD_DK);
    gfx.drawString("D&D 5E Toolkit", W / 2 + 1, 1);
    gfx.setTextColor(C_GOLD);
    gfx.drawString("D&D 5E Toolkit", W / 2, 0);
    gfx.setFont(&fonts::Font0);
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
    gfx.setFont(&fonts::FreeSerifBold9pt7b);
    gfx.setTextColor(a->accent());
    gfx.setTextDatum(top_center);
    gfx.drawString(a->name(), W / 2, 100);
    gfx.setFont(&fonts::Font0);
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
    gfx.setFont(&fonts::Font0);
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

void drawLauncher() {
    if (settings::cur.launcherList) drawList();
    else drawCarousel();
}

void openApp(App* a) {
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
        {makeTablesApp(), 1}, {makeTravelApp(), 1}, {makeShopApp(), 1},
        {makeSpellsApp(), 2}, {makeItemsApp(), 2}, {makeRulesApp(), 2}, {makeHazardsApp(), 2},
        {makeArchiveApp(), 3}, {makeWebApp(), 3}, {makeSettingsApp(), 3}};
    for (auto& a : apps) { g_apps.push_back(a.first); g_group.push_back(a.second); }
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

    uint32_t now = millis();
    uint32_t dt = now - g_lastFrame;
    g_lastFrame = now;

    // Dim the backlight when idle; any key wakes it (and is swallowed).
    bool idle = now - input::lastActivity() > DIM_AFTER_MS;
    if (idle != g_dimmed) {
        g_dimmed = idle;
        M5.Display.setBrightness(idle ? 20 : settings::cur.brightness);
        if (!idle) { input::Event e; while (input::poll(e)) {} }
    }

    input::Event e;
    uint32_t handleStart = millis();
    while (input::poll(e)) {
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
        crumb(g_active->name(), "draw");
        g_active->draw();
    } else {
        crumb("launcher", "draw");
        g_scroll = approach(g_scroll, (float)g_sel, std::min(1.0f, dt * 0.018f));
        if (fabsf(g_scroll - g_sel) < 0.002f) g_scroll = g_sel;
        drawLauncher();
    }

    // iris wipe on app open/close
    if (g_transition) {
        float f = g_transition / 220.0f;
        int r = (int)(f * 150);
        for (int i = 0; i < 6; i++) gfx.drawCircle(W / 2, H / 2, 140 - r + i * 3, C_BG);
        if (r > 0) gfx.fillRect(0, 0, W, (int)(f * H / 2), C_BG), gfx.fillRect(0, H - (int)(f * H / 2), W, H, C_BG);
        g_transition = dt >= g_transition ? 0 : g_transition - dt;
    }

    rollui::tick(dt);
    rollui::draw();
    crumb("main", "present");
    uint32_t pushStart = millis();
    ui::present();
    g_perfPush += millis() - pushStart;

    // ~60 fps while in use; ~25 fps after a few idle seconds (saves power,
    // animations still look smooth)
    uint32_t frameMs = now - input::lastActivity() > IDLE_SLOW_MS && !rollui::active() ? 40 : 16;
    uint32_t spent = millis() - now;
    g_perfFrames++;
    g_perfSum += spent;
    g_perfMax = std::max(g_perfMax, spent);
    if (spent < frameMs) delay(frameMs - spent);
}

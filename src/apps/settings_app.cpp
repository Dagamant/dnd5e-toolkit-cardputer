#include "../board/board.h"
#include "../core/touch.h"
#include "../core/app.h"
#include "../core/settings.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/textview.h"
#include "../core/hold_menu.h"
#include "../core/widgets.h"
#include "../core/clock.h"
#include "../core/web.h"

using namespace ui;

namespace {

class SettingsApp : public App {
public:
    const char* name() const override { return "Settings"; }
    const char* blurb() const override { return "Sound, display, SD card."; }
    uint16_t accent() const override { return C_DIM; }

    void drawIcon(int cx, int cy, float t) override {
        // gear
        for (int i = 0; i < 8; i++) {
            float a = t + i * 0.7854f;
            gfx.fillCircle(cx + (int)(15 * cosf(a)), cy + (int)(15 * sinf(a)), u(4), C_DIM);
        }
        gfx.fillCircle(cx, cy, u(13), C_DIM);
        gfx.fillCircle(cx, cy, u(6), C_BG2);
    }

    bool bigLayout() const override { return true; }

    bool handle(const input::Event& e) override {
        Settings& s = settings::cur;
        if (overlay_) {   // big screen: setting the time or picking a zone
            if (e.key == input::K_BACK) { overlay_ = 0; sfx::back(); }
            else if (e.key == input::K_ENTER && overlay_ == 1) saveTime();
            return true;
        }
        if (about_) {
            if (e.key == input::K_UP) aboutTv_.scroll(-1);
            else if (e.key == input::K_DOWN) aboutTv_.scroll(1);
            else { about_ = false; sfx::back(); }
            return true;
        }
        if (confirmFormat_) {
            if (e.key == input::K_CHAR && (e.base == 'y')) {
                confirmFormat_ = false;
                doFormat();
            } else if (e.key == input::K_BACK || e.key == input::K_CHAR || e.key == input::K_ENTER) {
                confirmFormat_ = false;
                sfx::back();
            }
            return true;
        }
        if (e.key == input::K_BACK) { settings::save(); return false; }
        if (e.key == input::K_ENTER && sel_ == 5) { remount(); return true; }
        if (e.key == input::K_ENTER && sel_ == 6) { confirmFormat_ = true; sfx::error(); return true; }
        if (e.key == input::K_ENTER && sel_ == 7) { openAbout(); return true; }
#ifdef BOARD_TAB5
        // Up/Down in screen order: the settings, the screen timeout (8), keyboard lights (9), About, the SD card
        if (e.key == input::K_UP || e.key == input::K_DOWN) {
            static constexpr int ORDER[] = {0, 1, 2, 3, 4, 8, 9, 7, 5, 6};
            int k = 0;
            while (ORDER[k] != sel_) k++;
            sel_ = ORDER[(k + (e.key == input::K_DOWN ? 1 : 9)) % 10];
            sfx::click();
            return true;
        }
        if (sel_ == 8) {
            const int dir = e.key == input::K_LEFT ? -1 : (e.key == input::K_RIGHT || e.key == input::K_ENTER) ? 1 : 0;
            if (dir) stepTimeout(dir);
            return true;
        }
        if (sel_ == 9) {
            const int dir = e.key == input::K_LEFT ? -1 : (e.key == input::K_RIGHT || e.key == input::K_ENTER) ? 1 : 0;
            if (dir) stepLights(dir);
            return true;
        }
#endif
        if (e.key == input::K_UP) { sel_ = (sel_ + ROWS - 1) % ROWS; sfx::click(); return true; }
        if (e.key == input::K_DOWN) { sel_ = (sel_ + 1) % ROWS; sfx::click(); return true; }
        if (sel_ >= 5) return true;   // action rows
        int dir = e.key == input::K_LEFT ? -1 : (e.key == input::K_RIGHT || e.key == input::K_ENTER) ? 1 : 0;
        if (!dir) return true;
        switch (sel_) {
            case 0: s.sound = !s.sound; break;
            case 1: s.volume = (uint8_t)std::max(0, std::min(255, s.volume + dir * 25)); break;
            case 2: s.brightness = (uint8_t)std::max(20, std::min(255, s.brightness + dir * 25)); break;
            case 3: s.shakeToRoll = !s.shakeToRoll; break;
            case 4: s.logRolls = !s.logRolls; break;
            default: return true;  // action rows only respond to Enter
        }
        settings::apply();
        sfx::click();
        return true;
    }

    void draw() override {
        if (wide()) { drawWide(); return; }
        drawBackground();
        drawHeader("Settings", C_DIM);
        const Settings& s = settings::cur;
        const char* labels[ROWS] = {"Sound", "Volume", "Brightness", "Shake to roll", "Log rolls to SD", "Remount SD card",
                                   "Format SD card (FAT32)", "About & license"};
        gfx.setFont(ui::font(&fonts::Font0));
        const int vis = wide() ? ROWS : VISIBLE;
        int first = std::max(0, std::min(sel_ - 3, ROWS - vis));
        for (int i = first; i < ROWS && i < first + vis; i++) {
            int y = wide() ? HEADER_H + 40 + (i - first) * 64 : HEADER_H + 6 + (i - first) * 14;
            bool sel = i == sel_;
            const int rh = wide() ? 60 : u(14), ry = wide() ? y - 16 : y - u(3);   // the row's box; text sits at y
            if (sel) drawPanel(u(4), ry, W - u(8), rh, C_PANEL, C_GOLD_DK);
            if (i == 1 || i == 2) {
                // touch: tap the row to select it, tap the bar to set the level
                touch::zone(0, ry, W - u(96), rh, [this, i] { sel_ = i; });
                touch::zone(W - u(96), ry, u(96), rh, [this, i] {
                    sel_ = i;
                    const Settings& s = settings::cur;
                    int cur = i == 1 ? s.volume : s.brightness;
                    int want = std::max(0, std::min(255, (touch::tapX() - (W - u(90))) * 255 / u(80)));
                    int steps = (want - cur) / 25;   // the keys step by 25
                    input::Event e;
                    e.key = steps < 0 ? input::K_LEFT : input::K_RIGHT;
                    for (int k = 0; k < abs(steps); k++) input::inject(e);
                });
            } else touch::row(0, ry, W, rh, sel_, i);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(sel ? C_GOLD : C_TEXT);
            gfx.drawString(labels[i], u(10), y);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(sel ? C_TEXT : C_DIM);
            switch (i) {
                case 0: gfx.drawString(s.sound ? "On" : "Off", W - u(10), y); break;
                case 1: drawBar(W - u(90), y + u(1), u(80), u(5), s.volume / 255.0f, C_GOLD); break;
                case 2: drawBar(W - u(90), y + u(1), u(80), u(5), s.brightness / 255.0f, C_ICE); break;
                case 3: gfx.drawString(s.shakeToRoll ? "On" : "Off", W - u(10), y); break;
                case 4: gfx.drawString(s.logRolls ? "On" : "Off", W - u(10), y); break;
                case 5: gfx.drawString(storage::ok() ? "Enter" : "No card - Enter", W - u(10), y); break;
                case 6: gfx.setTextColor(sel ? C_BLOOD : C_DIM); gfx.drawString("Enter", W - u(10), y); break;
                case 7: gfx.drawString("Enter", W - u(10), y); break;
            }
        }
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        String sd = storage::ok() ? String("SD ") + (uint32_t)storage::cardSizeMB() + " MB  log: " + storage::sessionLogPath()
                                  : String("No SD card (needs FAT32)");
        gfx.drawString(sd, u(8), H - FOOTER_H - u(11));
        drawFooter(sel_ >= 5 ? "Ent open  Up/Dn select  Esc save" : "Up/Dn select  </> change  Esc save");
        if (confirmFormat_) drawConfirm();
        if (about_) {
            drawBackground();
            drawHeader("About & license", C_DIM);
            aboutTv_.draw(u(6), HEADER_H + u(3), H - FOOTER_H - HEADER_H - u(5), C_GOLD);
            drawFooter("Up/Dn scroll  any key close");
        }
    }

    void openAbout() {
        fillAbout();
        about_ = true;
        sfx::select();
    }

    void fillAbout() {
        aboutTv_.clear();
        aboutTv_.add("D&D 5E Toolkit", TextView::HEAD);
        aboutTv_.add(wide() ? String("Game-master and player tools for the M5Stack ") + board::name() + "."
                            : String("Game-master and player tools for the M5Stack Cardputer ADV."));
        aboutTv_.gap();
        aboutTv_.add("SRD attribution", TextView::HEAD);
        aboutTv_.add("This work includes material from the System Reference Document 5.2.1 (\"SRD 5.2.1\") by Wizards of "
                     "the Coast LLC, available at https://www.dndbeyond.com/srd. The SRD 5.2.1 is licensed under the Creative "
                     "Commons Attribution 4.0 International License, available at "
                     "https://creativecommons.org/licenses/by/4.0/legalcode.");
        aboutTv_.gap();
        aboutTv_.add("Compatible with fifth edition (5E compatible).", TextView::DIM);
    }

    // ---------------------------------------------------------------- big screen
    // Sound and display on the left as switches and sliders (tap where you
    // want the level); the SD card and the licence on the right. Formatting
    // asks for a second tap instead of the Y key.
    bool aboutFilled_ = false;

    void focus(int x, int y, int w, int h, int row) {
        if (row == sel_) gfx.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 12, C_GOLD);
    }

    void toggle(int x, int y, bool on) {
        gfx.fillRoundRect(x, y, 88, 44, 22, on ? C_EMERALD : C_BG);
        gfx.drawRoundRect(x, y, 88, 44, 22, on ? C_EMERALD : C_EDGE);
        gfx.fillCircle(on ? x + 66 : x + 22, y + 22, 17, on ? C_TEXT : C_DIM);
    }

    // One setting: a card with its name, a line under it and the control on the right.
    void row(int x, int y, int w, int h, int i, const char* label, const String& sub) {
        const Metrics& m = metrics();
        drawPanel(x, y, w, h, C_BG2, C_EDGE);
        focus(x, y, w, h, i);
        gfx.setTextDatum(top_left);
        gfx.setFont(m.body);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(label, x + 18, y + h / 2 - 26);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString(fitText(sub, w - 160), x + 18, y + h / 2 + 6);
    }

    void switchRow(int x, int y, int w, int h, int i, const char* label, const String& sub, bool& value) {
        row(x, y, w, h, i, label, sub);
        toggle(x + w - 108, y + (h - 44) / 2, value);
        touch::zone(x, y, w, h, [this, i, &value] {
            sel_ = i;
            value = !value;
            settings::apply();
            settings::save();
            sfx::click();
        });
    }

    void sliderRow(int x, int y, int w, int h, int i, const char* label, uint8_t& value, uint8_t lo, uint16_t color) {
        row(x, y, w, h, i, label, String((value * 100 + 127) / 255) + "%");
        const int sx = x + 230, sw = w - 230 - 30, cy = y + h / 2;
        gfx.fillRoundRect(sx, cy - 6, sw, 12, 6, C_BG);
        const int kx = sx + sw * value / 255;
        gfx.fillRoundRect(sx, cy - 6, std::max(12, kx - sx), 12, 6, color);
        gfx.fillCircle(kx, cy, 16, C_TEXT);
        gfx.drawCircle(kx, cy, 16, color);
        touch::zone(x, y, w, h, [this, i, sx, sw, lo, &value] {
            sel_ = i;
            value = (uint8_t)std::max((int)lo, std::min(255, (touch::tapX() - sx) * 255 / sw));
            settings::apply();
            settings::save();
            sfx::click();
        });
    }

    // Screen timeout: 30 seconds to 15 minutes, with - and + buttons.
    void stepTimeout(int dir) {
        const int i = std::max(0, std::min(settings::NTIMEOUTS - 1, settings::timeoutIndex() + dir));
        if (settings::TIMEOUTS[i] == settings::cur.screenTimeout) { sfx::error(); return; }
        settings::cur.screenTimeout = settings::TIMEOUTS[i];
        settings::save();
        sfx::click();
    }

    void timeoutRow(int x, int y, int w, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        row(x, y, w, h, 8, "Screen timeout", "Dims, then goes dark");
        const int bw = 56, bx = x + w - 18 - bw, vx = bx - 120;
        const int i = settings::timeoutIndex();
        button(vx - bw, y + (h - 52) / 2, bw, 52, "-", false, C_ICE, [this] { sel_ = 8; stepTimeout(-1); }, i > 0);
        button(bx, y + (h - 52) / 2, bw, 52, "+", false, C_ICE, [this] { sel_ = 8; stepTimeout(1); }, i < settings::NTIMEOUTS - 1);
        gfx.setFont(m.title);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(settings::timeoutLabel(settings::cur.screenTimeout), vx + 60, y + h / 2);
        gfx.setTextDatum(top_left);
    }

    // Keyboard lights: Off, Dim, Normal.
    void stepLights(int dir) {
        const int v = std::max(0, std::min(2, settings::cur.kbLights + dir));
        if (v == settings::cur.kbLights) { sfx::error(); return; }
        settings::cur.kbLights = v;
        settings::apply();
        settings::save();
        sfx::click();
    }

    void lightsRow(int x, int y, int w, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        row(x, y, w, h, 9, "Keyboard lights", "Green and blue LEDs");
        const int bw = 56, bx = x + w - 18 - bw, vx = bx - 120;
        const int v = settings::cur.kbLights;
        button(vx - bw, y + (h - 52) / 2, bw, 52, "-", false, C_ICE, [this] { sel_ = 9; stepLights(-1); }, v > 0);
        button(bx, y + (h - 52) / 2, bw, 52, "+", false, C_ICE, [this] { sel_ = 9; stepLights(1); }, v < 2);
        gfx.setFont(m.bold);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(settings::KB_LIGHTS[v], vx + 60, y + h / 2);
        gfx.setTextDatum(top_left);
    }

    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        Settings& s = settings::cur;
        drawBackground();
        drawHeader("Settings", C_DIM);
        const int LX = 16, LW = 600, RX = LX + LW + 24, RW = W - RX - 16, top = HEADER_H + 8, bottom = H - FOOTER_H - 10;
        const int rh = 64, gap = 6;
        int y = top;
        caption(LX, y, "SOUND & DISPLAY");
        y += 30;
        switchRow(LX, y, LW, rh, 0, "Sound", "Clicks, dice and alerts", s.sound);
        y += rh + gap;
        sliderRow(LX, y, LW, rh, 1, "Volume", s.volume, 0, C_GOLD);
        y += rh + gap;
        sliderRow(LX, y, LW, rh, 2, "Brightness", s.brightness, 20, C_ICE);
        y += rh + gap;
        switchRow(LX, y, LW, rh, 3, "Shake to roll", "Shake the device to roll the dice", s.shakeToRoll);
        y += rh + gap;
        switchRow(LX, y, LW, rh, 4, "Log rolls to SD", storage::ok() ? storage::sessionLogPath() : String("Needs an SD card"),
                  s.logRolls);
        y += rh + gap;
        timeoutRow(LX, y, LW, rh);
        y += rh + gap;
        lightsRow(LX, y, LW, rh);

        // the SD card
        y = top;
        caption(RX, y, "SD CARD");
        y += 30;
        const int ch = 160;
        drawPanel(RX, y, RW, ch, C_BG2, C_EDGE);
        gfx.setTextDatum(top_left);
        gfx.setFont(m.title);
        gfx.setTextColor(storage::ok() ? C_EMERALD : C_BLOOD);
        const uint32_t mb = storage::ok() ? (uint32_t)storage::cardSizeMB() : 0;
        gfx.drawString(storage::ok() ? (mb >= 1024 ? String(mb / 1024.0f, 1) + " GB card" : String(mb) + " MB card")
                                     : String("No SD card"), RX + 18, y + 12);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString(storage::ok() ? "Mounted. Saves, logs and the SRD live here." : "Insert a FAT32 card, then Remount.",
                       RX + 18, y + 54);
        const int bw = (RW - 36 - 12) / 2;
        button(RX + 18, y + ch - 70, bw, 56, "Remount", sel_ == 5, C_EMERALD, [this] { sel_ = 5; remount(); });
        button(RX + 18 + bw + 12, y + ch - 70, bw, 56, "Format...", sel_ == 6, C_BLOOD, [this] {
            sel_ = 6;
            holdmenu::open("Format the SD card?", {{"Erase everything (FAT32)", [this] { doFormat(); }, C_BLOOD, true}});
        });

        // date and time
        y += ch + 20;
        caption(RX, y, "DATE & TIME");
        y += 30;
        const int th = bottom - y;
        drawPanel(RX, y, RW, th, C_BG2, C_EDGE);
        gfx.setTextDatum(top_left);
        gfx.setFont(m.big);
        gfx.setTextColor(wallclock::valid() ? C_GOLD : C_DIM);
        gfx.drawString(wallclock::valid() ? wallclock::timeText() : String("--:--"), RX + 18, y + 10);
        gfx.setFont(m.title);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(wallclock::valid() ? wallclock::dateText() : String("Not set"), RX + 200, y + 18);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString(fitText(wallclock::syncing() ? String("Asking the internet for the time...")
                       : wallclock::lastSyncMs() ? String("Set from the internet ") + (millis() - wallclock::lastSyncMs()) / 60000 + " min ago"
                                                 : String("Sets itself when web access joins your WiFi"), RW - 36),
                       RX + 18, y + 82);
        const int tbw = (RW - 36 - 12) / 2;
        button(RX + 18, y + th - 70, tbw, 56, String("Zone: ") + wallclock::ZONES[wallclock::zone()].label, false, C_ICE,
               [this] { overlay_ = 2; sfx::click(); });
        button(RX + 18 + tbw + 12, y + th - 70, tbw, 56, "Set time...", false, C_GOLD, [this] { openSetTime(); });

        // the licence, behind a button under the settings
        button(LX, bottom - 56, LW, 56, "About & license", sel_ == 7, C_DIM, [this] { sel_ = 7; openAbout(); });

        drawFooter(sel_ >= 5 && sel_ < 8 ? "Ent open  Up/Dn select  Esc save" : "Up/Dn select  </> change  Esc save");
        if (confirmFormat_) drawConfirm();
        if (overlay_ == 1) drawSetTime();
        else if (overlay_ == 2) drawZones();
        if (about_) {
            drawBackground();
            drawHeader("About & license", C_DIM);
            aboutTv_.setWidth(W - 60);
            aboutTv_.draw(u(6), HEADER_H + u(3), H - FOOTER_H - HEADER_H - u(5), C_GOLD);
            drawFooter("Up/Dn scroll  any key close");
            touch::key(0, HEADER_H, W, H - HEADER_H - FOOTER_H, input::K_BACK);
        }
    }

    // ---- setting the time by hand, and the zone
    int overlay_ = 0;   // 1 set time, 2 zone
    tm edit_{};

    void openSetTime() {
        if (!wallclock::now(edit_)) {   // never set: start from a sensible date
            edit_ = tm{};
            edit_.tm_year = 2026 - 1900;
            edit_.tm_mday = 1;
            edit_.tm_hour = 12;
        }
        edit_.tm_sec = 0;
        overlay_ = 1;
        sfx::click();
    }

    static int daysIn(int year, int mon) {
        static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const bool leap = (year % 4 == 0 && year % 100) || year % 400 == 0;
        return D[mon] + (mon == 1 && leap);
    }

    // The web page's settings: the clock, and sound and screen.
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        Settings& st = settings::cur;
        if (c.op == "zone") {
            const int z = c.num("i", -1);
            if (z < 0 || z >= wallclock::NZONES) { out = "No such zone"; return 400; }
            wallclock::setZone(z);
        } else if (c.op == "time") {
            tm t = {};
            t.tm_year = c.num("y") - 1900;
            t.tm_mon = c.num("mo") - 1;
            t.tm_mday = c.num("d");
            t.tm_hour = c.num("h");
            t.tm_min = c.num("mi");
            t.tm_sec = c.num("s");
            if (t.tm_year < 124 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 || t.tm_hour < 0 ||
                t.tm_hour > 23 || t.tm_min < 0 || t.tm_min > 59 || t.tm_sec < 0 || t.tm_sec > 59) {
                out = "That's not a time";
                return 400;
            }
            wallclock::setLocal(t);
        } else if (c.op == "sync") wallclock::syncFromInternet();
        else if (c.op == "set") {
            if (c.arg("volume").length()) st.volume = std::max(0, std::min(255, c.num("volume")));
            if (c.arg("brightness").length()) st.brightness = std::max(10, std::min(255, c.num("brightness")));
            if (c.arg("sound").length()) st.sound = c.arg("sound") == "1";
            if (c.arg("logRolls").length()) st.logRolls = c.arg("logRolls") == "1";
            if (c.arg("kbLights").length()) st.kbLights = std::max(0, std::min(2, c.num("kbLights")));
            if (c.arg("timeout").length()) { st.screenTimeout = c.num("timeout"); st.screenTimeout = settings::TIMEOUTS[settings::timeoutIndex()]; }
            settings::save();
            settings::apply();
        } else if (c.op != "state") return 404;
        out = String("{\"valid\":") + (wallclock::valid() ? "true" : "false") + ",\"time\":" + web::js(wallclock::timeText()) +
              ",\"date\":" + web::js(wallclock::dateText()) + ",\"zone\":" + wallclock::zone() + ",\"syncing\":" +
              (wallclock::syncing() ? "true" : "false") + ",\"syncedAgo\":" +
              (wallclock::lastSyncMs() ? (long)((millis() - wallclock::lastSyncMs()) / 60000) : -1L) + ",\"zones\":[";
        for (int i = 0; i < wallclock::NZONES; i++) out += String(i ? "," : "") + web::js(wallclock::ZONES[i].label);
        out += String("],\"volume\":") + st.volume + ",\"brightness\":" + st.brightness + ",\"sound\":" + (st.sound ? "true" : "false") +
               ",\"logRolls\":" + (st.logRolls ? "true" : "false") + ",\"timeout\":" + st.screenTimeout + ",\"kbLights\":" + st.kbLights + ",\"timeouts\":[";
        for (int i = 0; i < settings::NTIMEOUTS; i++) out += String(i ? "," : "") + settings::TIMEOUTS[i];
        out += "]}";
        return 200;
    }

    void saveTime() {
        wallclock::setLocal(edit_);
        overlay_ = 0;
        toast("Time set", C_EMERALD);
        sfx::select();
    }

    void drawSetTime() {
        using namespace widgets;
        const Metrics& m = metrics();
        const int w = 720, h = 560, x = (W - w) / 2, y = (H - h) / 2 + 10;
        gfx.fillRectAlpha(0, 0, W, H, 160, C_BG);
        touch::modal(x, y, w, h);
        drawPanel(x, y, w, h, C_BG2, C_GOLD_DK);
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString("Set the date and time", x + 24, y + 18);
        static const char* const MON[] = {"January", "February", "March", "April", "May", "June", "July",
                                          "August", "September", "October", "November", "December"};
        tm& t = edit_;
        auto clampDay = [&t] { t.tm_mday = std::min(t.tm_mday, daysIn(t.tm_year + 1900, t.tm_mon)); };
        const int sx = x + 24, sw = w - 48, sh = 56;
        int yy = y + 80;
        stepper(sx, yy, sw, sh, "Year", String(t.tm_year + 1900),
                [&t, clampDay](int d) { t.tm_year = std::max(124, std::min(199, t.tm_year + d)); clampDay(); }, 0, nullptr, 160);
        yy += 68;
        stepper(sx, yy, sw, sh, "Month", MON[t.tm_mon],
                [&t, clampDay](int d) { t.tm_mon = (t.tm_mon + d + 12) % 12; clampDay(); }, 0, nullptr, 160);
        yy += 68;
        stepper(sx, yy, sw, sh, "Day", String(t.tm_mday), [&t](int d) {
            const int n = daysIn(t.tm_year + 1900, t.tm_mon);
            t.tm_mday = (t.tm_mday - 1 + d % n + n) % n + 1;
        }, 0, nullptr, 160);
        yy += 68;
        stepper(sx, yy, sw, sh, "Hour", (t.tm_hour < 10 ? "0" : "") + String(t.tm_hour),
                [&t](int d) { t.tm_hour = (t.tm_hour + d % 24 + 24) % 24; }, 0, nullptr, 160);
        yy += 68;
        stepper(sx, yy, sw, sh, "Minute", (t.tm_min < 10 ? "0" : "") + String(t.tm_min),
                [&t](int d) { t.tm_min = (t.tm_min + d % 60 + 60) % 60; }, 10, nullptr, 160);
        const int bw = (sw - 12) / 2;
        button(sx, y + h - 80, bw, 60, "Cancel", false, C_DIM, [this] { overlay_ = 0; sfx::back(); });
        button(sx + bw + 12, y + h - 80, bw, 60, "Set", true, C_GOLD, [this] { saveTime(); });
    }

    void drawZones() {
        using namespace widgets;
        const Metrics& m = metrics();
        const int cols = 3, rows = (wallclock::NZONES + cols - 1) / cols, bh = 56, gap = 10;
        const int w = 1000, h = 90 + rows * (bh + gap) + 10, x = (W - w) / 2, y = std::max(HEADER_H + 4, (H - h) / 2);
        gfx.fillRectAlpha(0, 0, W, H, 160, C_BG);
        touch::modal(x, y, w, h);
        drawPanel(x, y, w, h, C_BG2, C_ICE);
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_ICE);
        gfx.drawString("Time zone", x + 24, y + 18);
        const int bw = (w - 48 - (cols - 1) * gap) / cols;
        for (int i = 0; i < wallclock::NZONES; i++) {
            const int bx = x + 24 + (i % cols) * (bw + gap), by = y + 80 + (i / cols) * (bh + gap);
            button(bx, by, bw, bh, wallclock::ZONES[i].label, i == wallclock::zone(), C_ICE, [this, i] {
                wallclock::setZone(i);
                overlay_ = 0;
                sfx::select();
            });
        }
    }

    void drawConfirm() {
        const int w = wide() ? W / 2 : W - 32, h = u(76), x = (W - w) / 2, y = wide() ? (H - h) / 2 : 26;
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BLOOD_DK, C_BLOOD);
        touch::key(0, 0, W, H, input::K_BACK);   // only the Y key formats; a tap cancels
        gfx.setTextDatum(top_center);
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextColor(C_TEXT);
        gfx.drawString("Format SD card?", W / 2, y + u(6));
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.drawString("This ERASES EVERYTHING on the card", W / 2, y + u(28));
        gfx.drawString("and makes it FAT32.", W / 2, y + u(39));
        gfx.setTextColor(C_GOLD);
        gfx.drawString("Y = format     any other key = cancel", W / 2, y + u(58));
    }

    void remount() {
        storage::end();
        bool ok = storage::begin();
        if (ok) { sfx::select(); toast("SD card mounted", C_EMERALD); }
        else { sfx::error(); toast("No FAT32 card found", C_BLOOD); }
    }

    void doFormat() {
        // Draw a status screen first; formatting blocks the loop.
        drawBackground();
        drawHeader("Settings", C_DIM);
        gfx.setTextDatum(middle_center);
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextColor(C_GOLD);
        const int cy = wide() ? H / 2 - u(8) : 60;
        gfx.drawString("Formatting SD card...", W / 2, cy);
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextColor(C_DIM);
        gfx.drawString("Large cards can take a minute.", W / 2, cy + u(20));
        gfx.drawString("Don't remove the card.", W / 2, cy + u(32));
        board::present(gfx);
        String err;
        watchdogPause(true);
        bool ok = storage::format(err);
        watchdogPause(false);
        if (ok) { sfx::crit(); toast("Card formatted (FAT32)", C_EMERALD); }
        else { sfx::error(); toast(err.c_str(), C_BLOOD); }
    }

private:
    static constexpr int ROWS = 8, VISIBLE = 6;
    bool about_ = false;
    TextView aboutTv_;
    int sel_ = 0;
    bool confirmFormat_ = false;
};

}  // namespace

App* makeSettingsApp() { return new SettingsApp(); }

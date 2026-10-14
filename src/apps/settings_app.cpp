#include "../core/app.h"
#include "../core/settings.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/textview.h"

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
            gfx.fillCircle(cx + (int)(15 * cosf(a)), cy + (int)(15 * sinf(a)), 4, C_DIM);
        }
        gfx.fillCircle(cx, cy, 13, C_DIM);
        gfx.fillCircle(cx, cy, 6, C_BG2);
    }

    bool handle(const input::Event& e) override {
        Settings& s = settings::cur;
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
        drawBackground();
        drawHeader("Settings", C_DIM);
        const Settings& s = settings::cur;
        const char* labels[ROWS] = {"Sound", "Volume", "Brightness", "Shake to roll", "Log rolls to SD", "Remount SD card",
                                   "Format SD card (FAT32)", "About & license"};
        gfx.setFont(&fonts::Font0);
        int first = std::max(0, std::min(sel_ - 3, ROWS - VISIBLE));
        for (int i = first; i < ROWS && i < first + VISIBLE; i++) {
            int y = HEADER_H + 6 + (i - first) * 14;
            bool sel = i == sel_;
            if (sel) drawPanel(4, y - 3, W - 8, 14, C_PANEL, C_GOLD_DK);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(sel ? C_GOLD : C_TEXT);
            gfx.drawString(labels[i], 10, y);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(sel ? C_TEXT : C_DIM);
            switch (i) {
                case 0: gfx.drawString(s.sound ? "On" : "Off", W - 10, y); break;
                case 1: drawBar(W - 90, y + 1, 80, 5, s.volume / 255.0f, C_GOLD); break;
                case 2: drawBar(W - 90, y + 1, 80, 5, s.brightness / 255.0f, C_ICE); break;
                case 3: gfx.drawString(s.shakeToRoll ? "On" : "Off", W - 10, y); break;
                case 4: gfx.drawString(s.logRolls ? "On" : "Off", W - 10, y); break;
                case 5: gfx.drawString(storage::ok() ? "Enter" : "No card - Enter", W - 10, y); break;
                case 6: gfx.setTextColor(sel ? C_BLOOD : C_DIM); gfx.drawString("Enter", W - 10, y); break;
                case 7: gfx.drawString("Enter", W - 10, y); break;
            }
        }
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        String sd = storage::ok() ? String("SD ") + (uint32_t)storage::cardSizeMB() + " MB  log: " + storage::sessionLogPath()
                                  : String("No SD card (needs FAT32)");
        gfx.drawString(sd, 8, H - FOOTER_H - 11);
        drawFooter(sel_ >= 5 ? "Ent open  Up/Dn select  Esc save" : "Up/Dn select  </> change  Esc save");
        if (confirmFormat_) drawConfirm();
        if (about_) {
            drawBackground();
            drawHeader("About & license", C_DIM);
            aboutTv_.draw(6, HEADER_H + 3, H - FOOTER_H - HEADER_H - 5, C_GOLD);
            drawFooter("Up/Dn scroll  any key close");
        }
    }

    void openAbout() {
        aboutTv_.clear();
        aboutTv_.add("D&D 5E Toolkit", TextView::HEAD);
        aboutTv_.add("Game-master and player tools for the M5Stack Cardputer ADV.");
        aboutTv_.gap();
        aboutTv_.add("SRD attribution", TextView::HEAD);
        aboutTv_.add("This work includes material from the System Reference Document 5.2.1 (\"SRD 5.2.1\") by Wizards of "
                     "the Coast LLC, available at https://www.dndbeyond.com/srd. The SRD 5.2.1 is licensed under the Creative "
                     "Commons Attribution 4.0 International License, available at "
                     "https://creativecommons.org/licenses/by/4.0/legalcode.");
        aboutTv_.gap();
        aboutTv_.add("Compatible with fifth edition (5E compatible).", TextView::DIM);
        about_ = true;
        sfx::select();
    }

    void drawConfirm() {
        const int x = 16, y = 26, w = W - 32, h = 76;
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BLOOD_DK, C_BLOOD);
        gfx.setTextDatum(top_center);
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        gfx.setTextColor(C_TEXT);
        gfx.drawString("Format SD card?", W / 2, y + 6);
        gfx.setFont(&fonts::Font0);
        gfx.drawString("This ERASES EVERYTHING on the card", W / 2, y + 28);
        gfx.drawString("and makes it FAT32.", W / 2, y + 39);
        gfx.setTextColor(C_GOLD);
        gfx.drawString("Y = format     any other key = cancel", W / 2, y + 58);
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
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        gfx.setTextColor(C_GOLD);
        gfx.drawString("Formatting SD card...", W / 2, 60);
        gfx.setFont(&fonts::Font0);
        gfx.setTextColor(C_DIM);
        gfx.drawString("Large cards can take a minute.", W / 2, 80);
        gfx.drawString("Don't remove the card.", W / 2, 92);
        gfx.pushSprite(0, 0);
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

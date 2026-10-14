#include "../core/app.h"
#include "../core/prompt.h"
#include "../core/roll_overlay.h"
#include "../core/sfx.h"
#include "../core/ui.h"

using namespace ui;

// Travel planner built on the SRD Travel Pace and Travel Terrain tables.

namespace {

struct Terrain { const char* name; int maxPace; const char* encounter; int forage, navigate, search; };
// maxPace: 0 Slow, 1 Normal, 2 Fast, 3 special (vehicle)
const Terrain TERRAIN[] = {
    {"Arctic", 2, "6d6", 20, 10, 10}, {"Coastal", 1, "2d10", 10, 5, 15}, {"Desert", 1, "6d6", 20, 10, 10},
    {"Forest", 1, "2d8", 10, 15, 15}, {"Grassland", 2, "6d6", 15, 5, 15}, {"Hill", 1, "2d10", 15, 10, 15},
    {"Mountain", 0, "4d10", 20, 15, 20}, {"Swamp", 0, "2d8", 10, 15, 20}, {"Underdark", 1, "2d6", 20, 10, 20},
    {"Urban", 1, "2d6", 20, 15, 15}, {"Waterborne", 3, "6d6", 15, 10, 15}};
constexpr int NTERRAIN = 11;
const char* const PACE[] = {"Slow", "Normal", "Fast"};
const int MPH[] = {2, 3, 4};
const char* const PACE_EFFECT[] = {"Advantage on Perception & Survival",
                                   "Disadvantage on Stealth",
                                   "Disadv. on Perception, Survival, Stealth"};

class TravelApp : public App {
public:
    const char* name() const override { return "Travel"; }
    const char* blurb() const override { return "Pace, terrain, days & supplies."; }
    uint16_t accent() const override { return rgb(120, 180, 230); }
    void drawIcon(int cx, int cy, float t) override {
        // compass
        gfx.fillCircle(cx, cy, 18, rgb(30, 50, 70));
        gfx.drawCircle(cx, cy, 18, C_GOLD);
        float a = sinf(t * 1.5f) * 0.5f;
        int nx = cx + (int)(14 * sinf(a)), ny = cy - (int)(14 * cosf(a));
        gfx.fillTriangle(nx, ny, cx - 4, cy, cx + 4, cy, C_BLOOD);
        gfx.fillTriangle(2 * cx - nx, 2 * cy - ny, cx - 4, cy, cx + 4, cy, C_TEXT);
        gfx.fillCircle(cx, cy, 2, C_GOLD);
    }

    void onEnter() override { input::setTextMode(false); }

    bool handle(const input::Event& e) override {
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done()) miles_ = std::max(0, (int)prompt_.value().toInt());
            return true;
        }
        int d = 0;
        if (e.key == input::K_LEFT || (e.key == input::K_CHAR && e.base == '-')) d = -1;
        if (e.key == input::K_RIGHT || (e.key == input::K_CHAR && e.base == '=')) d = 1;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: sel_ = (sel_ + ROWS - 1) % ROWS; sfx::click(); return true;
            case input::K_DOWN: sel_ = (sel_ + 1) % ROWS; sfx::click(); return true;
            case input::K_ENTER:
                if (sel_ == 4) prompt_.open("Distance in miles", String(miles_), 1, 5);
                else if (sel_ == 6) {
                    const Terrain& t = TERRAIN[terrain_];
                    rollui::show(String("Encounter distance (") + t.name + ")", {{"Distance", t.encounter, "x 10 ft"}});
                } else if (sel_ == 7) rollui::show("Navigation (Survival)", {{String("vs DC ") + TERRAIN[terrain_].navigate, "d20", ""}});
                else if (sel_ == 8) rollui::show("Foraging (Survival)", {{String("vs DC ") + TERRAIN[terrain_].forage, "d20", ""}});
                return true;
            default: break;
        }
        if (!d) return true;
        switch (sel_) {
            case 0: terrain_ = (terrain_ + d + NTERRAIN) % NTERRAIN; break;
            case 1: road_ = !road_; break;
            case 2: pace_ = (pace_ + d + 3) % 3; break;
            case 3: hours_ = std::max(1, std::min(16, hours_ + d)); break;
            case 4: miles_ = std::max(0, miles_ + d * (e.key == input::K_CHAR ? 10 : 1)); break;
            case 5: party_ = std::max(1, std::min(12, party_ + d)); break;
            default: return true;
        }
        sfx::click();
        return true;
    }

    void draw() override {
        drawBackground();
        drawHeader("Travel", accent());
        const Terrain& t = TERRAIN[terrain_];
        int maxPace = t.maxPace == 3 ? 2 : std::min(2, t.maxPace + (road_ ? 1 : 0));
        int pace = std::min(pace_, maxPace);
        int perDay = MPH[pace] * hours_;
        int days = perDay ? (miles_ + perDay - 1) / perDay : 0;
        String rows[ROWS][2] = {
            {"Terrain", String("< ") + t.name + " >"},
            {"Good road", road_ ? "Yes" : "No"},
            {"Pace wanted", String("< ") + PACE[pace_] + " >"},
            {"Hours per day", String("< ") + hours_ + " >"},
            {"Distance (Ent)", String(miles_) + " mi"},
            {"Party size", String("< ") + party_ + " >"},
            {"Encounter dist.", String(t.encounter) + "x10"},
            {"Navigation", String("DC ") + t.navigate},
            {"Foraging", String("DC ") + t.forage},
        };
        gfx.setFont(&fonts::Font0);
        const int rowH = 10, y0 = HEADER_H + 2;
        for (int i = 0; i < ROWS; i++) {
            int y = y0 + i * rowH;
            bool s = i == sel_;
            if (s) gfx.fillRoundRect(2, y - 1, 140, rowH, 2, C_PANEL);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(i >= 6 ? C_EMERALD : s ? C_TEXT : rgb(200, 205, 215));
            gfx.drawString(rows[i][0], 5, y);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(rows[i][1], 140, y);
        }
        // results panel
        int px = 146, py = HEADER_H + 2, pw = W - px - 3, ph = H - FOOTER_H - py - 2;
        drawPanel(px, py, pw, ph, C_BG2, accent());
        int y = py + 3;
        auto line = [&](const String& s, uint16_t c) {
            gfx.setTextDatum(top_left);
            gfx.setTextColor(c);
            y = textWrapped(s.c_str(), px + 4, y, pw - 8, 9, 3) + 1;
        };
        if (t.maxPace == 3) line("Waterborne: speed of the vessel", C_GOLD);
        else {
            line(String("Pace: ") + PACE[pace] + (pace < pace_ ? " (terrain max)" : ""), pace < pace_ ? C_GOLD : C_TEXT);
            line(String(MPH[pace]) + " mi/h, " + perDay + " mi/day", C_EMERALD);
            if (miles_) line(String("Trip: ") + days + (days == 1 ? " day" : " days"), C_EMERALD);
        }
        line(PACE_EFFECT[pace], C_DIM);
        if (hours_ > 8) line(String("Each hour past 8: CON save DC ") + (10 + hours_ - 8) + "+ or 1 Exhaustion", C_BLOOD);
        if (days) line(String("Supplies: ") + days * party_ + " lb food, " + days * party_ + " gal water", C_ICE);
        if (terrain_ == 0 && pace == 2) line("Fast needs skis etc.", C_DIM);
        drawFooter("</> change  Ent roll/type  Esc back");
        prompt_.draw("Whole miles");
    }

private:
    static constexpr int ROWS = 9;
    int sel_ = 0, terrain_ = 3, pace_ = 1, hours_ = 8, miles_ = 24, party_ = 4;
    bool road_ = false;
    Prompt prompt_;
};

}  // namespace

App* makeTravelApp() { return new TravelApp(); }

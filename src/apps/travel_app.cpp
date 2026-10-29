#include "../core/app.h"
#include "../core/character.h"
#include "../core/touch.h"
#include "../core/prompt.h"
#include "../core/roll_overlay.h"
#include "../core/sfx.h"
#include "../core/ui.h"
#include "../core/web.h"
#include "../core/widgets.h"

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
        gfx.fillCircle(cx, cy, u(18), rgb(30, 50, 70));
        gfx.drawCircle(cx, cy, u(18), C_GOLD);
        float a = sinf(t * 1.5f) * 0.5f;
        int nx = cx + (int)(14 * sinf(a)), ny = cy - (int)(14 * cosf(a));
        gfx.fillTriangle(nx, ny, cx - u(4), cy, cx + u(4), cy, C_BLOOD);
        gfx.fillTriangle(2 * cx - nx, 2 * cy - ny, cx - u(4), cy, cx + u(4), cy, C_TEXT);
        gfx.fillCircle(cx, cy, u(2), C_GOLD);
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

    bool bigLayout() const override { return true; }

    void draw() override {
        drawBackground();
        drawHeader("Travel", accent());
        if (wide()) { drawWide(); return; }
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
        gfx.setFont(ui::font(&fonts::Font0));
        const int rowH = touchRow(10), pad = touchPad(10), y0 = HEADER_H + u(2);
        for (int i = 0; i < ROWS; i++) {
            int y = y0 + i * rowH + pad;
            bool s = i == sel_;
            if (s) gfx.fillRoundRect(u(2), y - pad - u(1), u(140), rowH - 2, u(2), C_PANEL);
            // the last rows roll (Enter); the others step their value
            if (i >= 6) touch::row(0, y - pad - u(1), u(144), rowH, sel_, i);
            else touch::field(0, y - pad - u(1), u(144), rowH, sel_, i);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(i >= 6 ? C_EMERALD : s ? C_TEXT : rgb(200, 205, 215));
            gfx.drawString(rows[i][0], u(5), y);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(rows[i][1], u(140), y);
        }
        // results panel
        int px = u(146), py = HEADER_H + u(2), pw = W - px - u(3), ph = H - FOOTER_H - py - u(2);
        drawPanel(px, py, pw, ph, C_BG2, accent());
        int y = py + u(3);
        auto line = [&](const String& s, uint16_t c) {
            gfx.setTextDatum(top_left);
            gfx.setTextColor(c);
            y = textWrapped(s.c_str(), px + u(4), y, pw - u(8), u(9), 3) + u(1) + (wide() ? u(3) : 0);
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
    int survival_ = 2;           // the guide's Survival bonus, for the big screen's rolls
    int day_ = 0, done_ = 0;     // the journey so far: days travelled, miles covered
    bool road_ = false;
    Prompt prompt_;

    int paceNow() const {
        const Terrain& t = TERRAIN[terrain_];
        const int maxPace = t.maxPace == 3 ? 2 : std::min(2, t.maxPace + (road_ ? 1 : 0));
        return std::min(pace_, maxPace);
    }
    int perDay() const { return MPH[paceNow()] * hours_; }

    // The web page's view of the same planner, so a change on either side shows on both.
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (c.op == "set") {
            terrain_ = std::max(0, std::min(NTERRAIN - 1, c.num("terrain", terrain_)));
            pace_ = std::max(0, std::min(2, c.num("pace", pace_)));
            road_ = c.num("road", road_) != 0;
            miles_ = std::max(0, std::min(9999, c.num("miles", miles_)));
            hours_ = std::max(1, std::min(16, c.num("hours", hours_)));
            party_ = std::max(1, std::min(12, c.num("party", party_)));
            survival_ = std::max(-5, std::min(17, c.num("survival", survival_)));
        } else if (c.op == "day") {
            if (perDay() && TERRAIN[terrain_].maxPace != 3 && !(miles_ && done_ >= miles_)) {
                done_ = std::min(miles_, done_ + perDay());
                day_++;
            }
        } else if (c.op == "restart") {
            day_ = done_ = 0;
        } else if (c.op != "state") return 404;
        out = String("{\"terrain\":") + terrain_ + ",\"pace\":" + pace_ + ",\"paceNow\":" + paceNow() + ",\"road\":" +
              (road_ ? "true" : "false") + ",\"miles\":" + miles_ + ",\"hours\":" + hours_ + ",\"party\":" + party_ +
              ",\"survival\":" + survival_ + ",\"day\":" + day_ + ",\"done\":" + done_ + ",\"perDay\":" + perDay() +
              ",\"mph\":[2,3,4],\"paces\":[\"Slow\",\"Normal\",\"Fast\"],\"paceEffects\":[";
        for (int i = 0; i < 3; i++) out += String(i ? "," : "") + web::js(PACE_EFFECT[i]);
        out += "],\"terrains\":[";
        for (int i = 0; i < NTERRAIN; i++) {
            const Terrain& t = TERRAIN[i];
            out += String(i ? "," : "") + "{\"name\":" + web::js(t.name) + ",\"maxPace\":" + t.maxPace + ",\"encounter\":" +
                   web::js(t.encounter) + ",\"forage\":" + t.forage + ",\"navigate\":" + t.navigate + ",\"search\":" + t.search + "}";
        }
        out += "]}";
        return 200;
    }

    void check(const char* what, int dc) {
        rollui::show(String(what) + " (Survival)", {{"Survival", "d20" + dnd::fmtMod(survival_) + " vs " + dc, ""}});
    }

    // ---- big screen: choices on the left, the trip on the right
    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        const Terrain& t = TERRAIN[terrain_];
        const int pace = paceNow(), per = perDay(), days = per ? (miles_ + per - 1) / per : 0;
        const bool boat = t.maxPace == 3;
        const int LX = 16, LW = 600, RX = LX + LW + 16, RW = W - RX - 16;

        // terrain
        int y = HEADER_H + 10;
        caption(LX, y, "TERRAIN");
        y += 26;
        const int tw = (LW - 3 * 8) / 4, th = 50;
        for (int i = 0; i < NTERRAIN; i++) {
            const int x = LX + (i % 4) * (tw + 8), ty = y + (i / 4) * (th + 8);
            button(x, ty, tw, th, TERRAIN[i].name, i == terrain_, accent(), [this, i] { terrain_ = i; sfx::click(); });
        }
        y += 3 * (th + 8) + 8;

        // pace and road
        caption(LX, y, "PACE");
        y += 26;
        const int maxPace = boat ? 2 : std::min(2, t.maxPace + (road_ ? 1 : 0));
        const int pw = (LW - 180 - 3 * 8) / 3;
        for (int i = 0; i < 3; i++)
            button(LX + i * (pw + 8), y, pw, 56, PACE[i], i == pace, accent(), [this, i] { pace_ = i; sfx::click(); },
                   boat || i <= maxPace);
        button(LX + 3 * (pw + 8), y, LW - 3 * (pw + 8), 56, road_ ? "Road: yes" : "Road: no", road_, C_GOLD,
               [this] { road_ = !road_; sfx::click(); });
        y += 56 + 16;

        // numbers
        const int sh = 54, sg = 10;
        stepper(LX, y, LW, sh, "Distance", String(miles_) + " mi", [this](int d) { miles_ = std::max(0, miles_ + d); }, 10,
                [this] { prompt_.open("Distance in miles", String(miles_), 1, 5); }, 190);
        y += sh + sg;
        stepper(LX, y, LW, sh, "Hours a day", String(hours_), [this](int d) { hours_ = std::max(1, std::min(16, hours_ + d)); }, 0, nullptr, 190);
        y += sh + sg;
        stepper(LX, y, LW, sh, "Party", String(party_), [this](int d) { party_ = std::max(1, std::min(12, party_ + d)); }, 0, nullptr, 190);
        y += sh + sg;
        stepper(LX, y, LW, sh, "Survival", dnd::fmtMod(survival_), [this](int d) { survival_ = std::max(-5, std::min(17, survival_ + d)); },
                0, nullptr, 190);

        // the trip
        y = HEADER_H + 10;
        const int cw = (RW - 3 * 10) / 4, ch = 118;
        if (boat) stat(RX, y, cw, ch, "PER DAY", "vessel", C_DIM, "ship's speed");
        else stat(RX, y, cw, ch, "PER DAY", String(per) + " mi", C_EMERALD, String(MPH[pace]) + " mi/hour");
        stat(RX + cw + 10, y, cw, ch, "TRIP", boat || !miles_ ? String("-") : String(days) + (days == 1 ? " day" : " days"),
             C_EMERALD, String(miles_) + " mi");
        stat(RX + 2 * (cw + 10), y, cw, ch, "FOOD", String(days * party_) + " lb", C_ICE, "whole trip");
        const bool hot = terrain_ == 2;
        stat(RX + 3 * (cw + 10), y, cw, ch, "WATER", String(days * party_ * (hot ? 2 : 1)) + " gal", C_ICE,
             hot ? "x2: hot" : "whole trip");
        y += ch + 12;

        // the journey, a day at a time
        const int jh = 150;
        drawPanel(RX, y, RW, jh, C_BG2, C_EDGE);
        gfx.setFont(m.bold);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(accent());
        const bool there = miles_ && done_ >= miles_;
        gfx.drawString(there ? String("Arrived after ") + day_ + (day_ == 1 ? " day" : " days")
                       : day_ ? String("Day ") + day_ + " done" : String("Not set out yet"),
                       RX + 16, y + 12);
        gfx.setFont(m.small);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(std::min(done_, miles_)) + " of " + miles_ + " mi" +
                           (miles_ > done_ ? String(", ") + (miles_ - done_) + " to go" : String("")),
                       RX + RW - 16, y + 16);
        drawBar(RX + 16, y + 52, RW - 32, 18, miles_ ? std::min(1.0f, (float)done_ / miles_) : 0, accent(), C_BG);
        // a tick for the end of each planned day
        for (int d = 1; per && d < days; d++) {
            const int tx = RX + 16 + (RW - 32) * d * per / std::max(1, miles_);
            gfx.drawFastVLine(tx, y + 48, 26, C_BG);
        }
        const int bw = (RW - 32 - 10) / 2;
        button(RX + 16, y + 84, bw, 54, there ? "Arrived" : "Travel a day", false, C_EMERALD, [this] {
            if (!perDay() || (miles_ && done_ >= miles_)) return;
            done_ = std::min(miles_, done_ + perDay());
            day_++;
            sfx::select();
        }, !there && !boat);
        button(RX + 26 + bw, y + 84, bw, 54, "Start over", false, C_DIM, [this] { day_ = done_ = 0; sfx::back(); }, day_ > 0);
        y += jh + 12;

        // what the pace and hours mean
        const int eh = 96;
        drawPanel(RX, y, RW, eh, C_BG2, C_EDGE);
        gfx.setFont(m.body);
        gfx.setTextDatum(top_left);
        int ty = y + 12;
        auto say = [&](const String& txt, uint16_t c) {
            if (ty > y + eh - 30) return;
            gfx.setTextColor(c);
            gfx.drawString(fitText(txt, RW - 32), RX + 16, ty);
            ty += 34;
        };
        if (pace < pace_) say(String(t.name) + ": " + PACE[pace] + " at most" + (road_ ? "" : " (a road helps)"), C_GOLD);
        say(PACE_EFFECT[pace], C_DIM);
        if (hours_ > 8) say(String("Past 8 hours: CON save DC ") + (10 + hours_ - 8) + " or 1 Exhaustion", C_BLOOD);
        else if (terrain_ == 0 && pace == 2) say("Fast in the arctic needs skis or the like", C_DIM);

        // checks: a tap rolls
        y += eh + 12;
        const int rh = H - FOOTER_H - 10 - y, rw = (RW - 3 * 10) / 4;
        struct Check { const char* label; String value; std::function<void()> fn; };
        Check checks[4] = {
            {"NAVIGATE", String("DC ") + t.navigate, [this] { check("Navigation", TERRAIN[terrain_].navigate); }},
            {"FORAGE", String("DC ") + t.forage, [this] { check("Foraging", TERRAIN[terrain_].forage); }},
            {"SEARCH", String("DC ") + t.search, [this] { check("Search", TERRAIN[terrain_].search); }},
            {"ENCOUNTER", String(t.encounter), [this] {
                 const Terrain& t = TERRAIN[terrain_];
                 rollui::show(String("Encounter distance (") + t.name + ")", {{"Distance", t.encounter, "x 10 ft"}});
             }},
        };
        for (int i = 0; i < 4; i++) {
            const int x = RX + i * (rw + 10);
            stat(x, y, rw, rh, checks[i].label, checks[i].value, i == 3 ? C_GOLD : C_EMERALD, i == 3 ? "x 10 feet" : "tap: roll");
            touch::zone(x, y, rw, rh, checks[i].fn);
        }

        drawFooter("Esc back");
        prompt_.draw("Whole miles");
    }
};

}  // namespace

App* makeTravelApp() { return new TravelApp(); }

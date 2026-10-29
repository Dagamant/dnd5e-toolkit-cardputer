#include "db_browser.h"
#include "../core/touch.h"
#include "../core/hold_menu.h"
#include "../core/widgets.h"
#include "../core/campaign.h"
#include "../core/encounter.h"
#include "../core/encounter_gen.h"
#include "../core/rng.h"
#include "../core/web.h"

using namespace ui;
using namespace encounter;

// Encounter generator: picks SRD monsters to fit the party's XP budget for a
// habitat, creature type and group style, then sends the fight to Initiative
// (or saves it as the Monsters app's encounter plan).

namespace {

const char* const ACTIVITY[] = {
    "Lying in ambush", "On patrol", "Feeding on a kill", "Resting in a rough camp", "Hunting - tracking the party",
    "Guarding something valuable", "Fighting another group", "Lost, hungry and desperate", "Arguing over loot",
    "Fleeing something worse", "Searching for something", "Building or digging", "Performing a ritual",
    "Sleeping - mostly", "Escorting a prisoner", "Returning to their lair"};
const uint16_t ACCENT = rgb(230, 110, 50);

class EncountersApp : public App {
public:
    const char* name() const override { return "Encounters"; }
    const char* blurb() const override { return "Random fights by habitat & level."; }
    uint16_t accent() const override { return ACCENT; }

    void drawIcon(int cx, int cy, float t) override {
        // something lurks: glowing eyes between dark pines under a moon
        gfx.fillRoundRect(cx - 19, cy - 18, 38, 36, 6, rgb(20, 28, 40));
        gfx.fillCircle(cx + 10, cy - 10, 4, rgb(230, 225, 190));
        const int8_t trees[][3] = {{-13, 12, 7}, {0, 16, 9}, {13, 10, 6}};
        for (auto& tr : trees) {
            int x = cx + tr[0], top = cy - tr[1], w = tr[2];
            gfx.fillTriangle(x, top, x - w, cy + 14, x + w, cy + 14, rgb(30, 70, 50));
        }
        gfx.drawFastHLine(cx - 19, cy + 14, 38, rgb(30, 70, 50));
        // two pairs of eyes, blinking at different times
        float ph[2] = {fmodf(t, 3.1f), fmodf(t + 1.4f, 2.6f)};
        const int8_t eyes[2][2] = {{-7, 4}, {7, 8}};
        for (int i = 0; i < 2; i++) {
            if (t > 0 && ph[i] < 0.15f) continue;
            uint16_t c = i ? C_GOLD : rgb(240, 70, 50);
            gfx.fillRect(cx + eyes[i][0] - 3, cy + eyes[i][1], 2, 2, c);
            gfx.fillRect(cx + eyes[i][0] + 2, cy + eyes[i][1], 2, 2, c);
        }
        gfx.drawRoundRect(cx - 19, cy - 18, 38, 36, 6, ACCENT);
    }

    void onEnter() override {
        input::setTextMode(false);
        plan_.load();
        gen_.open();
        view_ = OPTIONS;
    }
    void onExit() override {
        closeViewer();
        plan_.save();
        gen_.close();
        result_.clear();
    }

    bool handle(const input::Event& e) override {
        if (viewer_) {
            if (!viewer_->handle(e)) closeViewer();
            return true;
        }
        return view_ == OPTIONS ? handleOptions(e) : handleResult(e);
    }

    bool bigLayout() const override { return true; }

    void draw() override {
        if (viewer_) { viewer_->draw(); return; }
        if (wide()) drawWide();
        else if (view_ == OPTIONS) drawOptions();
        else drawResult();
    }

private:
    enum View { OPTIONS, RESULT };
    View view_ = OPTIONS;
    encounter::Generator gen_;
    encounter::Plan plan_;
    std::vector<encounter::Entry> result_;
    int optSel_ = 0, resSel_ = 0;
    int budget_ = 0, groupUsed_ = 0;
    String activity_, note_;
    std::unique_ptr<DbBrowser> viewer_;

    // ---------------------------------------------------------------- options
    static constexpr int OPT_ROWS = 7;   // size, level, habitat, difficulty, type, group, generate

    bool handleOptions(const input::Event& e) {
        int d = 0;
        if (e.key == input::K_LEFT || (e.key == input::K_CHAR && e.base == '-')) d = -1;
        if (e.key == input::K_RIGHT || (e.key == input::K_CHAR && e.base == '=')) d = 1;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: optSel_ = (optSel_ + OPT_ROWS - 1) % OPT_ROWS; sfx::click(); return true;
            case input::K_DOWN: optSel_ = (optSel_ + 1) % OPT_ROWS; sfx::click(); return true;
            case input::K_TAB: optSel_ = (optSel_ + 1) % OPT_ROWS; sfx::click(); return true;
            case input::K_ENTER: generate(); return true;
            default: break;
        }
        if (e.key == input::K_CHAR && e.base == 'r') { generate(); return true; }
        if (!d) return true;
        auto wrap = [](int v, int n) { return (v + n) % n; };
        switch (optSel_) {
            case 0: plan_.partySize = std::max(1, std::min(10, plan_.partySize + d)); break;
            case 1: plan_.partyLevel = std::max(1, std::min(20, plan_.partyLevel + d)); break;
            case 2: plan_.genHabitat = wrap(plan_.genHabitat + d, NHAB); break;
            case 3: plan_.genDiff = wrap(plan_.genDiff + d, 3); break;
            case 4: plan_.genType = wrap(plan_.genType + d, NTYPES); break;
            case 5: plan_.genGroup = wrap(plan_.genGroup + d, NGROUPS); break;
            default: return true;
        }
        sfx::click();
        return true;
    }

    void drawOptions() {
        drawBackground();
        drawHeader("Encounter Generator", ACCENT);
        gfx.setFont(ui::font(&fonts::Font0));
        const int rowH = touchRow(13), pad = touchPad(13), y0 = HEADER_H + u(4);
        for (int i = 0; i < OPT_ROWS; i++) {
            int ry = y0 + i * rowH + pad;
            bool s = i == optSel_;
            if (s) gfx.fillRoundRect(u(2), ry - pad - u(2), W - u(4), rowH - u(1), u(3), C_PANEL);
            if (i == OPT_ROWS - 1) touch::row(0, ry - pad - u(2), W, rowH, optSel_, i);   // Generate
            else touch::field(0, ry - pad - u(2), W, rowH, optSel_, i);
            String l, r;
            switch (i) {
                case 0: l = "Party size"; r = String(plan_.partySize); break;
                case 1: l = "Party level"; r = String(plan_.partyLevel); break;
                case 2: l = "Habitat"; r = HABITATS[plan_.genHabitat]; break;
                case 3: l = "Difficulty"; r = DIFF[plan_.genDiff]; break;
                case 4: l = "Creature type"; r = TYPES[plan_.genType]; break;
                case 5: l = "Group"; r = GROUPS[plan_.genGroup]; break;
                default: break;
            }
            gfx.setTextDatum(top_left);
            if (i == OPT_ROWS - 1) {
                gfx.setTextDatum(top_center);
                gfx.setTextColor(s ? C_GOLD : C_EMERALD);
                gfx.drawString(">> Generate encounter <<", W / 2, ry + u(1));
                continue;
            }
            gfx.setTextColor(s ? C_TEXT : C_DIM);
            gfx.drawString(l, u(8), ry + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(s ? C_GOLD : C_ICE);
            gfx.drawString(s ? "< " + r + " >" : r, W - u(8), ry + u(1));
        }
        int b = encounter::budget(plan_.partyLevel, plan_.genDiff) * plan_.partySize;
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String("XP budget ") + b, W / 2, y0 + OPT_ROWS * rowH);
        drawFooter("</> change  Ent generate  Esc exit");
    }

    // ---------------------------------------------------------------- generation
    int totalXp() const { int t = 0; for (auto& m : result_) t += m.xp * m.count; return t; }

    // The web page's encounter builder: the same options, generator and result.
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (!appOpen(this)) plan_.load();
        const bool opened = !gen_.isOpen();
        if (opened) gen_.open();
        const int code = webOp(c, out);
        if (opened && !appOpen(this)) gen_.close();   // the monster index is big; only keep it while the app is up
        return code;
    }

    int webOp(const WebCall& c, String& out) {
        auto clamp = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };
        if (c.op == "set") {
            plan_.partySize = clamp(c.num("party", plan_.partySize), 1, 10);
            plan_.partyLevel = clamp(c.num("level", plan_.partyLevel), 1, 20);
            plan_.genHabitat = clamp(c.num("habitat", plan_.genHabitat), 0, NHAB - 1);
            plan_.genType = clamp(c.num("type", plan_.genType), 0, NTYPES - 1);
            plan_.genDiff = clamp(c.num("diff", plan_.genDiff), 0, 2);
            plan_.genGroup = clamp(c.num("group", plan_.genGroup), 0, NGROUPS - 1);
            plan_.save();
        } else if (c.op == "generate") {
            encounter::GenOptions o;
            o.partySize = plan_.partySize;
            o.partyLevel = plan_.partyLevel;
            o.habitat = plan_.genHabitat;
            o.type = plan_.genType;
            o.diff = plan_.genDiff;
            o.group = plan_.genGroup;
            encounter::GenResult r;
            if (!gen_.generate(o, r)) { out = "No monsters fit that budget"; return 400; }
            result_ = r.monsters;
            budget_ = r.budget;
            groupUsed_ = r.group;
            note_ = r.note;
            activity_ = rng::pick(ACTIVITY);
        } else if (c.op == "session") {
            if (result_.empty()) { out = "Generate one first"; return 400; }
            String text = encounter::describe(result_) + " (" + HABITATS[plan_.genHabitat] + ", " + DIFF[plan_.genDiff] + ", " +
                          totalXp() + " XP)", title;
            if (!campaign::addToCurrentSession("Encounters", text, title)) { out = "Open a session in Campaign first"; return 400; }
            out = String("Added to ") + title;
            return 200;
        } else if (c.op == "save") {
            if (result_.empty()) { out = "Generate one first"; return 400; }
            plan_.monsters = result_;
            plan_.save();
            out = "Saved as the Monsters app's fight";
            return 200;
        } else if (c.op != "state") return 404;
        auto list = [](const char* const* v, int n) {
            String o = "[";
            for (int i = 0; i < n; i++) o += String(i ? "," : "") + web::js(v[i]);
            return o + "]";
        };
        out = String("{\"habitats\":") + list(HABITATS, NHAB) + ",\"types\":" + list(TYPES, NTYPES) + ",\"diffs\":" + list(DIFF, 3) +
              ",\"groups\":" + list(GROUPS, NGROUPS) + ",\"party\":" + plan_.partySize + ",\"level\":" + plan_.partyLevel +
              ",\"habitat\":" + plan_.genHabitat + ",\"type\":" + plan_.genType + ",\"diff\":" + plan_.genDiff +
              ",\"group\":" + plan_.genGroup + ",\"budgets\":[";
        for (int d = 0; d < 3; d++) out += String(d ? "," : "") + encounter::budget(plan_.partyLevel, d) * plan_.partySize;
        out += String("],\"result\":{\"text\":") + web::js(encounter::describe(result_)) + ",\"xp\":" + totalXp() +
               ",\"budget\":" + budget_ + ",\"group\":" + web::js(result_.empty() ? "" : GROUP_SHORT[groupUsed_]) +
               ",\"note\":" + web::js(note_) + ",\"activity\":" + web::js(activity_) + ",\"monsters\":[";
        for (size_t i = 0; i < result_.size(); i++) {
            const auto& m = result_[i];
            out += String(i ? "," : "") + "{\"name\":" + web::js(m.name) + ",\"count\":" + m.count + ",\"xp\":" + m.xp +
                   ",\"hp\":" + m.hp + ",\"ac\":" + m.ac + ",\"cr\":" + web::js(m.cr) + "}";
        }
        out += "]}}";
        return 200;
    }

    void generate() {
        encounter::GenOptions o;
        o.partySize = plan_.partySize;
        o.partyLevel = plan_.partyLevel;
        o.habitat = plan_.genHabitat;
        o.type = plan_.genType;
        o.diff = plan_.genDiff;
        o.group = plan_.genGroup;
        encounter::GenResult r;
        if (!gen_.generate(o, r)) { sfx::error(); toast("No monsters fit that budget", C_BLOOD); return; }
        result_ = r.monsters;
        budget_ = r.budget;
        groupUsed_ = r.group;
        note_ = r.note;
        activity_ = rng::pick(ACTIVITY);
        plan_.save();
        view_ = RESULT;
        resSel_ = 0;
        sfx::crit();
    }

    // ---------------------------------------------------------------- result
    // rows: monsters..., send rolled, send average, save to Monsters, add to session, reroll
    int resRows() const { return result_.size() + 5; }

    bool handleResult(const input::Event& e) {
        int n = resRows(), action = resSel_ - (int)result_.size();
        switch (e.key) {
            case input::K_BACK: view_ = OPTIONS; sfx::back(); return true;
            case input::K_UP: resSel_ = (resSel_ + n - 1) % n; sfx::click(); return true;
            case input::K_DOWN: resSel_ = (resSel_ + 1) % n; sfx::click(); return true;
            case input::K_ENTER:
                if (action < 0) viewMonster(result_[resSel_].name);
                else if (action <= 1) send(action == 0);
                else if (action == 2) saveToPlan();
                else if (action == 3) addToSession();
                else generate();
                return true;
            default: break;
        }
        if (e.key == input::K_CHAR && e.base == 'r') generate();
        return true;
    }

    void send(bool rolledHp) {
        int n = encounter::enqueue(result_, rolledHp);
        if (!n) return;
        sfx::crit();
        requestApp("Initiative");
    }

    void saveToPlan() {
        plan_.monsters = result_;
        plan_.save();
        toast("Saved as the Monsters fight (Fn+E)", C_EMERALD);
        sfx::select();
    }

    void addToSession() {
        String text = encounter::describe(result_) + " (" + HABITATS[plan_.genHabitat] + ", " + DIFF[plan_.genDiff] + ", " +
                      totalXp() + " XP)";
        String title;
        if (!campaign::addToCurrentSession("Encounters", text, title)) {
            sfx::error();
            toast("Open a session in Campaign first", C_BLOOD);
            return;
        }
        toast((String("Added to ") + title).c_str(), C_EMERALD);
        sfx::select();
    }

    void viewMonster(const String& m) {
        viewer_.reset(newMonstersBrowser());
        viewer_->closeOnBack = true;
        viewer_->onEnter();
        if (!viewer_->openByName(m)) { closeViewer(); toast("Stat block not found", C_BLOOD); }
    }
    void closeViewer() {
        if (viewer_) { viewer_->onExit(); viewer_.reset(); input::setTextMode(false); }
    }

    void drawResult() {
        drawBackground();
        drawHeader((String(HABITATS[plan_.genHabitat]) + " encounter").c_str(), ACCENT);
        gfx.setFont(ui::font(&fonts::Font0));
        int xp = totalXp(), b[3];
        for (int d = 0; d < 3; d++) b[d] = encounter::budget(plan_.partyLevel, d) * plan_.partySize;
        int diff = xp <= b[0] ? 0 : xp <= b[1] ? 1 : xp <= b[2] ? 2 : 3;
        uint16_t col = diff == 0 ? C_EMERALD : diff == 1 ? C_GOLD : diff == 2 ? rgb(240, 130, 60) : C_BLOOD;
        int y = HEADER_H + u(3);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(col);
        gfx.drawString(String(xp) + " XP " + (diff < 3 ? DIFF[diff] : "Deadly"), u(6), y);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(GROUP_SHORT[groupUsed_]) + "  " + plan_.partySize + "xL" + plan_.partyLevel, W - u(6), y);
        float scale = b[2] * 1.25f;
        int bx = u(6), bw = W - u(12);
        gfx.fillRect(bx, y + u(10), bw, u(5), C_BG2);
        gfx.fillRect(bx, y + u(10), std::min(bw, (int)(bw * xp / scale)), u(5), col);
        for (int d = 0; d < 3; d++) gfx.fillRect(bx + (int)(bw * b[d] / scale), y + u(8), u(1), u(9), C_TEXT);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(note_.length() ? C_BLOOD : C_ICE);
        gfx.drawString(note_.length() ? note_ : activity_, u(6), y + u(18));

        const int rowH = touchRow(11), pad = touchPad(11), y0 = y + u(30);
        int rows = (H - FOOTER_H - y0) / rowH, n = resRows();
        int first = std::max(0, std::min(resSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int ry = y0 + (i - first) * rowH;
            bool s = i == resSel_;
            if (s) gfx.fillRoundRect(u(2), ry - u(1), W - u(4), rowH, u(2), C_PANEL);
            touch::row(0, ry - u(1), W, rowH, resSel_, i);
            ry += pad;
            int action = i - (int)result_.size();
            String l, r;
            uint16_t c = C_TEXT;
            if (action < 0) {
                auto& m = result_[i];
                l = String(m.count) + "x " + m.name;
                r = String("CR") + m.cr + " " + (m.xp * m.count);
            } else if (action == 0) { l = "Fight! Send to Initiative (roll HP)"; c = C_EMERALD; }
            else if (action == 1) { l = "Send to Initiative (average HP)"; c = C_EMERALD; }
            else if (action == 2) { l = "Save as Monsters fight plan"; c = C_ICE; }
            else if (action == 3) { l = "Add to session plan (Campaign)"; c = ACCENT; }
            else { l = "Reroll"; c = C_GOLD; }
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : c);
            gfx.drawString(wide() ? l : l.substring(0, action < 0 ? 26 : 38), u(6), ry + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(r, W - u(6), ry + u(1));
        }
        drawFooter("Ent stats/do  R reroll  Esc options");
    }

    // ---------------------------------------------------------------- big screen
    // One screen: the choices on the left, the fight on the right with its
    // difficulty bar, the monsters as cards and what to do with them.
    float monPos_ = 0;

    void focus(int x, int y, int w, int h, bool on) {
        if (on) gfx.drawRoundRect(x - 6, y - 5, w + 12, h + 10, 12, ACCENT);
    }

    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawBackground();
        drawHeader("Encounter Generator", ACCENT);
        const int LX = 22, LW = 590, top = HEADER_H + 14, bottom = H - FOOTER_H - 12;
        const bool opts = view_ == OPTIONS;
        auto wrap = [](int v, int n) { return (v + n) % n; };
        int y = top;
        stepper(LX, y, LW, 52, "Party size", String(plan_.partySize),
                [this](int d) { plan_.partySize = std::max(1, std::min(10, plan_.partySize + d)); optSel_ = 0; }, 0, nullptr, 186);
        focus(LX, y, LW, 52, opts && optSel_ == 0);
        y += 64;
        stepper(LX, y, LW, 52, "Level", String(plan_.partyLevel),
                [this](int d) { plan_.partyLevel = std::max(1, std::min(20, plan_.partyLevel + d)); optSel_ = 1; }, 0, nullptr, 186);
        focus(LX, y, LW, 52, opts && optSel_ == 1);
        y += 64;
        stepper(LX, y, LW, 52, "Habitat", HABITATS[plan_.genHabitat],
                [this, wrap](int d) { plan_.genHabitat = wrap(plan_.genHabitat + (d > 0 ? 1 : -1), NHAB); optSel_ = 2; }, 0, nullptr, 186);
        focus(LX, y, LW, 52, opts && optSel_ == 2);
        y += 64;
        stepper(LX, y, LW, 52, "Creature", TYPES[plan_.genType],
                [this, wrap](int d) { plan_.genType = wrap(plan_.genType + (d > 0 ? 1 : -1), NTYPES); optSel_ = 4; }, 0, nullptr, 186);
        focus(LX, y, LW, 52, opts && optSel_ == 4);
        y += 70;
        caption(LX, y, "DIFFICULTY");
        y += 30;
        const int dw = (LW - 2 * 10) / 3;
        for (int d = 0; d < 3; d++)
            button(LX + d * (dw + 10), y, dw, 50, DIFF[d], plan_.genDiff == d, ACCENT, [this, d] { plan_.genDiff = d; optSel_ = 3; sfx::click(); });
        focus(LX, y, LW, 50, opts && optSel_ == 3);
        y += 64;
        caption(LX, y, "GROUP");
        y += 30;
        const int gw = (LW - 4 * 10) / 5;
        for (int g = 0; g < NGROUPS; g++)
            button(LX + g * (gw + 10), y, gw, 50, g ? GROUP_SHORT[g] : "Random", plan_.genGroup == g, ACCENT,
                   [this, g] { plan_.genGroup = g; optSel_ = 5; sfx::click(); });
        focus(LX, y, LW, 50, opts && optSel_ == 5);
        button(LX, bottom - 62, LW, 62, result_.empty() ? "Generate encounter" : "Generate another", true, C_GOLD,
               [this] { generate(); });
        focus(LX, bottom - 62, LW, 62, opts && optSel_ == 6);

        // the fight
        const int RX = LX + LW + 26, RW = W - RX - 16, py = top - 4, ph = bottom - py;
        drawPanel(RX, py, RW, ph, C_BG2, C_EDGE);
        int b[3];
        for (int d = 0; d < 3; d++) b[d] = encounter::budget(plan_.partyLevel, d) * plan_.partySize;
        const int xp = result_.empty() ? b[plan_.genDiff] : totalXp();
        const int diff = xp <= b[0] ? 0 : xp <= b[1] ? 1 : xp <= b[2] ? 2 : 3;
        const uint16_t col = diff == 0 ? C_EMERALD : diff == 1 ? C_GOLD : diff == 2 ? rgb(240, 130, 60) : C_BLOOD;
        gfx.setTextDatum(top_left);
        gfx.setFont(m.title);
        gfx.setTextColor(result_.empty() ? C_DIM : col);
        gfx.drawString(result_.empty() ? String("Budget ") + b[plan_.genDiff] + " XP" : String(xp) + " XP", RX + 18, py + 14);
        gfx.setFont(m.bold);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(result_.empty() ? C_DIM : col);
        gfx.drawString(result_.empty() ? String(DIFF[plan_.genDiff]) : String(diff < 3 ? DIFF[diff] : "Deadly"), RX + RW - 18, py + 18);
        // the bar, with the three budgets marked
        const float scale = b[2] * 1.25f;
        const int bx = RX + 18, bw = RW - 36, by = py + 62;
        gfx.fillRoundRect(bx, by, bw, 14, 6, C_BG);
        if (!result_.empty()) gfx.fillRoundRect(bx, by, std::max(14, std::min(bw, (int)(bw * xp / scale))), 14, 6, col);
        // the marks can sit close together, so their labels go left, middle and right
        gfx.setFont(m.small);
        const uint8_t datum[3] = {top_left, top_center, top_right};
        const int lx[3] = {bx, bx + bw / 2, bx + bw};
        for (int d = 0; d < 3; d++) {
            const uint16_t dc = d == 0 ? C_EMERALD : d == 1 ? C_GOLD : rgb(240, 130, 60);
            gfx.fillRect(bx + (int)(bw * b[d] / scale) - 1, by - 4, 3, 22, dc);
            gfx.setTextDatum(datum[d]);
            gfx.setTextColor(dc);
            gfx.drawString(String(DIFF[d]) + " " + b[d], lx[d], by + 24);
        }
        if (result_.empty()) {
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Pick the party and the place,", RX + RW / 2, py + ph / 2 + 10);
            gfx.drawString("then generate a fight.", RX + RW / 2, py + ph / 2 + 50);
            drawFooter("</> change  Ent generate  Esc exit");
            return;
        }
        gfx.setFont(m.small);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(note_.length() ? C_BLOOD : C_ICE);
        gfx.drawString(fitText(String(GROUP_SHORT[groupUsed_]) + ": " + (note_.length() ? note_ : activity_), RW - 36), RX + 18, py + 122);

        // monsters
        const int ay = bottom - 12 - 2 * 56 - 10, ly = py + 160, lh = ay - 12 - ly, cardH = 82;
        const int n = result_.size();
        const float maxPos = std::max(0, n * cardH - 8 - lh);
        if (!opts && resSel_ < n) {   // the keyboard's choice stays in view
            monPos_ = std::min(monPos_, (float)(resSel_ * cardH));
            monPos_ = std::max(monPos_, (float)((resSel_ + 1) * cardH - 8 - lh));
        }
        monPos_ = std::max(0.0f, std::min(monPos_, maxPos + 60));
        touch::scroller(RX + 12, ly, RW - 24, lh, &monPos_, maxPos);
        gfx.setClipRect(RX + 12, ly, RW - 24, lh);
        for (int i = 0; i < n; i++) {
            const encounter::Entry& e = result_[i];
            const int cx = RX + 12, cw = RW - 24, cy = ly + i * cardH - (int)monPos_;
            if (cy + cardH < ly || cy > ly + lh) continue;
            const bool on = !opts && i == resSel_;
            gfx.fillRoundRect(cx, cy, cw, cardH - 8, 10, on ? C_PANEL : C_BG);
            gfx.drawRoundRect(cx, cy, cw, cardH - 8, 10, on ? ACCENT : C_EDGE);
            gfx.setFont(m.title);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(ACCENT);
            gfx.drawString(String(e.count) + "x", cx + 44, cy + (cardH - 8) / 2 + 2);
            gfx.setFont(m.bold);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_TEXT);
            gfx.drawString(fitText(e.name, cw - 120 - 110), cx + 92, cy + 10);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String("CR ") + e.cr + "   AC " + e.ac + "   HP " + e.hp, cx + 92, cy + 44);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(String(e.xp * e.count) + " XP", cx + cw - 16, cy + (cardH - 8) / 2);
            const int vy = std::max(cy, ly), vh = std::min(cy + cardH - 8, ly + lh) - vy;
            if (vh <= 0) continue;
            touch::zone(cx, vy, cw, vh, [this, i] { resSel_ = i; view_ = RESULT; viewMonster(result_[i].name); });
            touch::onHold(cx, vy, cw, vh, [this, i] { resSel_ = i; view_ = RESULT; holdMonster(i); });
        }
        gfx.clearClipRect();

        // what to do with it
        const int a = resSel_ - n, hw = (RW - 24 - 10) / 2, tw = (RW - 24 - 20) / 3, ax = RX + 12;
        button(ax, ay, hw, 56, "Fight! Roll HP", !opts && a == 0, C_EMERALD, [this] { send(true); });
        button(ax + hw + 10, ay, hw, 56, "Fight, average HP", !opts && a == 1, C_EMERALD, [this] { send(false); });
        button(ax, ay + 66, tw, 56, "Monsters plan", !opts && a == 2, C_ICE, [this] { saveToPlan(); });
        button(ax + tw + 10, ay + 66, tw, 56, "Add to session", !opts && a == 3, ACCENT, [this] { addToSession(); });
        button(ax + 2 * (tw + 10), ay + 66, tw, 56, "Reroll", !opts && a == 4, C_GOLD, [this] { generate(); });
        drawFooter(opts ? "</> change  Ent generate  Esc exit" : "Ent stats/do  R reroll  hold for more  Esc options");
    }

    void holdMonster(int i) {
        std::vector<holdmenu::Item> items;
        items.push_back({"Stat block", [this, i] { viewMonster(result_[i].name); }});
        items.push_back({"One more", [this, i] { result_[i].count++; sfx::click(); }});
        if (result_[i].count > 1) items.push_back({"One fewer", [this, i] { result_[i].count--; sfx::click(); }});
        if (result_.size() > 1)
            items.push_back({"Remove", [this, i] {
                result_.erase(result_.begin() + i);
                resSel_ = std::min(resSel_, (int)result_.size() - 1);
                sfx::back();
            }, C_BLOOD});
        holdmenu::open(result_[i].name, std::move(items));
    }
};

}  // namespace

App* makeEncountersApp() { return new EncountersApp(); }

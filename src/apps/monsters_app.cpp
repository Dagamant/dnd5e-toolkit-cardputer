#include "db_browser.h"
#include "../core/character.h"
#include "../core/dice_expr.h"
#include "../core/encounter.h"

using namespace ui;

// Monsters: SRD bestiary with rollable stat blocks, plus an encounter builder
// that uses the SRD XP budgets and sends the fight to the Initiative tracker.

namespace {

const char* const TYPES[] = {"All", "Aberration", "Beast", "Celestial", "Construct", "Dragon", "Elemental", "Fey",
                             "Fiend", "Giant", "Humanoid", "Monstrosity", "Ooze", "Plant", "Undead"};
constexpr int NTYPES = 15;
const char* const DIFF[] = {"Low", "Moderate", "High"};
const uint16_t ACCENT = rgb(200, 70, 60);

float crValue(const char* cr) {
    if (!strcmp(cr, "1/8")) return 0.125f;
    if (!strcmp(cr, "1/4")) return 0.25f;
    if (!strcmp(cr, "1/2")) return 0.5f;
    return atof(cr);
}


class MonstersBrowser : public DbBrowser {
public:
    const char* name() const override { return "Monsters"; }
    const char* blurb() const override { return "Bestiary & encounter builder."; }
    uint16_t accent() const override { return ACCENT; }

    void drawIcon(int cx, int cy, float t) override {
        // dragon-ish head: horned skull
        gfx.fillTriangle(cx - 14, cy - 10, cx - 20, cy - 24, cx - 8, cy - 14, C_TEXT);
        gfx.fillTriangle(cx + 14, cy - 10, cx + 20, cy - 24, cx + 8, cy - 14, C_TEXT);
        gfx.fillRoundRect(cx - 15, cy - 14, 30, 26, 8, rgb(150, 50, 45));
        gfx.drawRoundRect(cx - 15, cy - 14, 30, 26, 8, C_GOLD);
        float p = 0.5f + 0.5f * sinf(t * 4);
        uint16_t eye = rgb(255, 200 + (int)(55 * p), 60);
        gfx.fillCircle(cx - 6, cy - 4, 3, eye);
        gfx.fillCircle(cx + 6, cy - 4, 3, eye);
        for (int i = -2; i <= 2; i++) gfx.fillTriangle(cx + i * 4 - 2, cy + 6, cx + i * 4 + 2, cy + 6, cx + i * 4, cy + 11, C_TEXT);
    }

    void onEnter() override {
        DbBrowser::onEnter();
        plan_.load();
        showPlan_ = false;
        rollMenu_ = false;
    }

    bool handle(const input::Event& e) override {
        if (showPlan_) return handlePlan(e);
        if (rollMenu_) return handleRollMenu(e);
        if (mode_ != Mode::Wizard && e.key == input::K_CHAR && e.fn) {
            if (e.base == 'e') { openPlan(); return true; }
            if (e.base == 'a') { addToPlan(); return true; }
        }
        return DbBrowser::handle(e);
    }

    void draw() override {
        if (showPlan_) { drawPlan(); return; }
        DbBrowser::draw();
        if (rollMenu_) drawRollMenu();
    }

protected:
    int typeF_ = 0, crF_ = -1;
    bool rollMenu_ = false, showPlan_ = false;
    int rollSel_ = 0, planSel_ = 0;
    struct RollItem { String label; std::vector<rollui::Line> lines; String title; };
    std::vector<RollItem> rolls_;
    encounter::Plan plan_;
    bool confirmClear_ = false;

    const char* title() const override { return "Monsters"; }
    bool canCreate() const override { return false; }
    const char* listFooter() const override { return "Ent open Tab type 0-9 CR FnA add FnE fight"; }
    const char* detailFooter() const override { return closeOnBack ? "Ent roll  Esc back" : "Ent roll  FnA add to fight  FnE fight"; }

    void openTable() override {
        table_.open({{"/gmtools/srd/monsters", {"cr", "type", "xp", "hp", "ac", "init", "size", "habitat"}}},
                    "/gmtools/custom/monsters.txt", {"cr", "type", "xp", "hp", "ac", "init", "size", "habitat"});
    }

    bool passFilter(int i) const override {
        if (typeF_ > 0 && strcmp(table_.col(i, "type"), TYPES[typeF_]) != 0 &&
            !(strstr(table_.col(i, "type"), TYPES[typeF_]))) return false;
        if (crF_ >= 0) {
            float v = crValue(table_.col(i, "cr"));
            if (crF_ == 0 ? v >= 1 : (int)v != crF_) return false;
        }
        return true;
    }
    String filterLabel() const override {
        String s = TYPES[typeF_];
        if (crF_ == 0) s += " / CR <1";
        else if (crF_ > 0) s += String(" / CR ") + crF_;
        return s;
    }
    void cycleFilter(int d) override { typeF_ = (typeF_ + d + NTYPES) % NTYPES; }
    bool quickFilter(char c) override { int v = c - '0'; crF_ = crF_ == v ? -1 : v; return true; }

    void drawRow(int i, int y, bool s) override {
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(s ? C_TEXT : rgb(205, 195, 195));
        String n = table_.at(i).name;
        if (n.length() > 25) n = n.substring(0, 24) + ".";
        gfx.drawString(n, 8, y + 1);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_DIM);
        String t = table_.col(i, "type");
        gfx.drawString(t.substring(0, 6), W - 40, y + 1);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(String("CR") + table_.col(i, "cr"), W - 6, y + 1);
    }

    void buildDetail() override {
        const db::Rec& r = cur_;
        tv_.add(r.get("typeline"), TextView::DIM);
        tv_.add(String("AC ") + r.get("ac") + "   HP " + r.get("hp") + (r.has("hpdice") ? " (" + r.get("hpdice") + ")" : String("")) +
                    "   Init " + r.get("init"), TextView::ACCENT);
        tv_.add(String("Speed ") + r.get("speed"));
        auto sc = dnd::splitList(r.get("scores"), ',');
        auto sv = dnd::splitList(r.get("saves"), ',');
        if (sc.size() == 6) {
            String l1, l2;
            for (int a = 0; a < 6; a++) {
                String cell = String(dnd::ABIL_SHORT[a]) + " " + sc[a] + "(" + dnd::fmtMod(dnd::modOf(sc[a].toInt())) + ")";
                (a < 3 ? l1 : l2) += cell + "  ";
            }
            tv_.add(l1, TextView::GOOD);
            tv_.add(l2, TextView::GOOD);
            String saves;
            for (int a = 0; a < 6 && a < (int)sv.size(); a++)
                if (sv[a].toInt() != dnd::modOf(sc[a].toInt())) saves += String(saves.length() ? ", " : "") + dnd::ABIL_SHORT[a] + " " + sv[a];
            if (saves.length()) tv_.add("Saves " + saves);
        }
        for (const char* k : {"skills", "resistances", "vulnerabilities", "immunities", "gear", "senses", "languages"}) {
            if (!r.has(k)) continue;
            String lab = k;
            lab.setCharAt(0, toupper(lab[0]));
            tv_.add(lab + ": " + r.get(k));
        }
        tv_.add(String("CR ") + r.get("cr") + "  (" + r.get("xp") + " XP" + (r.has("pb") ? ", PB +" + r.get("pb") : String("")) + ")", TextView::ACCENT);
        const char* secs[][2] = {{"traits", "Traits"}, {"actions", "Actions"}, {"bonus_actions", "Bonus Actions"},
                                 {"reactions", "Reactions"}, {"legendary_actions", "Legendary Actions"}};
        for (auto& s : secs) {
            if (!r.has(s[0])) continue;
            tv_.gap();
            tv_.add(s[1], TextView::HEAD);
            for (auto& para : dnd::splitList(db::unescape(r.get(s[0])), '\n')) tv_.add(para, TextView::BULLET);
        }
        buildRolls();
    }

    void buildRolls() {
        rolls_.clear();
        const db::Rec& r = cur_;
        String nm = r.name();
        for (int i = 1; i <= 12; i++) {
            String rl = r.get((String("roll") + i).c_str());
            if (!rl.length()) break;
            // name|atk|+9|2d6+5|Bludgeoning|extra|xtype   or   name|save|DC 14 DEX|dice|type||
            int p[7], k = 0, start = 0;
            String f[7];
            for (k = 0; k < 7; k++) {
                int bar = rl.indexOf('|', start);
                f[k] = bar < 0 ? rl.substring(start) : rl.substring(start, bar);
                start = bar < 0 ? rl.length() : bar + 1;
            }
            (void)p;
            RollItem it;
            it.title = nm + ": " + f[0];
            if (f[1] == "atk") {
                it.label = f[0] + "  " + f[2] + "  " + f[3];
                it.lines.push_back({"Attack", "d20" + f[2], ""});
                it.lines.push_back({"Damage", f[3], f[4]});
                if (f[5].length()) it.lines.push_back({"Plus", f[5], f[6]});
            } else {
                it.label = f[0] + "  " + f[2] + (f[3].length() ? "  " + f[3] : String(""));
                it.title = f[0] + " " + f[2];
                if (f[3].length()) it.lines.push_back({"Damage (half on save)", f[3], f[4]});
            }
            rolls_.push_back(it);
        }
        RollItem ini;
        ini.label = "Initiative " + r.get("init");
        ini.title = nm + " initiative";
        ini.lines.push_back({"Initiative", "d20" + r.get("init"), ""});
        rolls_.push_back(ini);
        if (r.has("hpdice")) {
            RollItem hp;
            hp.label = "Hit points " + r.get("hpdice") + " (avg " + r.get("hp") + ")";
            hp.title = nm + " HP";
            hp.lines.push_back({"Hit points", r.get("hpdice"), "HP"});
            rolls_.push_back(hp);
        }
        auto sv = dnd::splitList(r.get("saves"), ',');
        for (int a = 0; a < 6 && a < (int)sv.size(); a++) {
            RollItem s;
            s.label = String(dnd::ABIL_SHORT[a]) + " save " + sv[a];
            s.title = nm + " " + dnd::ABIL_SHORT[a] + " save";
            s.lines.push_back({"Save", "d20" + sv[a], ""});
            rolls_.push_back(s);
        }
    }

    void rollCurrent() override { rollMenu_ = true; rollSel_ = 0; sfx::select(); }

    bool handleRollMenu(const input::Event& e) {
        int n = rolls_.size();
        if (e.key == input::K_BACK || !n) { rollMenu_ = false; sfx::back(); }
        else if (e.key == input::K_UP) { rollSel_ = (rollSel_ + n - 1) % n; sfx::click(); }
        else if (e.key == input::K_DOWN) { rollSel_ = (rollSel_ + 1) % n; sfx::click(); }
        else if (e.key == input::K_ENTER && n) rollui::show(rolls_[rollSel_].title, rolls_[rollSel_].lines);
        return true;
    }

    void drawRollMenu() {
        const int w = 220, rows = std::min(8, (int)rolls_.size()), h = 14 + rows * 11, x = (W - w) / 2, y = std::max(18, (H - h) / 2);
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, ACCENT);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(ACCENT);
        gfx.drawString("Roll for " + cur_.name(), x + 6, y + 3);
        int first = std::max(0, std::min(rollSel_ - rows / 2, (int)rolls_.size() - rows));
        for (int i = first; i < (int)rolls_.size() && i < first + rows; i++) {
            int ry = y + 13 + (i - first) * 11;
            bool s = i == rollSel_;
            if (s) gfx.fillRect(x + 3, ry - 1, w - 6, 10, C_PANEL);
            String l = rolls_[i].label;
            while (l.length() > 3 && gfx.textWidth(l) > w - 16) l = l.substring(0, l.length() - 1);
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 190));
            gfx.drawString(l, x + 8, ry);
        }
    }

    // ---------------------------------------------------------------- encounter plan
    void addToPlan() {
        db::Rec r;
        if (mode_ == Mode::Detail) r = cur_;
        else if (!vis_.empty() && table_.read(vis_[sel_], r)) {}
        else return;
        for (auto& m : plan_.monsters)
            if (m.name == r.name()) { m.count++; plan_.save(); toast((m.name + " x" + m.count + " in fight").c_str(), C_GOLD); sfx::click(); return; }
        encounter::Entry en;
        en.name = r.name();
        en.xp = r.num("xp");
        en.hp = r.num("hp", 1);
        en.ac = r.num("ac", 10);
        en.init = r.get("init").toInt();
        en.hpdice = r.get("hpdice");
        en.cr = r.get("cr");
        plan_.monsters.push_back(en);
        plan_.save();
        toast((en.name + " added to fight").c_str(), C_GOLD);
        sfx::click();
    }

    void openPlan() { showPlan_ = true; planSel_ = 0; confirmClear_ = false; input::setTextMode(false); sfx::select(); }

    // rows: 0 party size, 1 party level, monsters..., send (rolled), send (average), clear
    int planRows() const { return 2 + plan_.monsters.size() + 3; }

    bool handlePlan(const input::Event& e) {
        int n = planRows();
        int mIdx = planSel_ - 2;
        bool onMonster = mIdx >= 0 && mIdx < (int)plan_.monsters.size();
        int action = planSel_ - 2 - (int)plan_.monsters.size();   // 0 send rolled, 1 send avg, 2 clear
        if (!(e.key == input::K_ENTER && action == 2)) confirmClear_ = false;
        int d = 0;
        if (e.key == input::K_CHAR && e.base == '-') d = -1;
        if (e.key == input::K_CHAR && e.base == '=') d = 1;
        if (e.key == input::K_LEFT) d = -1;
        if (e.key == input::K_RIGHT) d = 1;
        switch (e.key) {
            case input::K_BACK: showPlan_ = false; plan_.save(); sfx::back(); return true;
            case input::K_UP: planSel_ = (planSel_ + n - 1) % n; sfx::click(); return true;
            case input::K_DOWN: planSel_ = (planSel_ + 1) % n; sfx::click(); return true;
            case input::K_ENTER:
                if (action == 0 || action == 1) sendToInitiative(action == 0);
                if (action == 2) {
                    if (!confirmClear_) { confirmClear_ = true; sfx::error(); return true; }
                    plan_.monsters.clear(); plan_.save(); planSel_ = 0; confirmClear_ = false; toast("Fight cleared", C_DIM);
                }
                return true;
            default: break;
        }
        if (e.key == input::K_CHAR && (e.base == 'x' || e.base == '\b') && onMonster) {
            plan_.monsters.erase(plan_.monsters.begin() + mIdx);
            plan_.save();
            return true;
        }
        if (!d) return true;
        if (planSel_ == 0) plan_.partySize = std::max(1, std::min(10, plan_.partySize + d));
        else if (planSel_ == 1) plan_.partyLevel = std::max(1, std::min(20, plan_.partyLevel + d));
        else if (onMonster) {
            auto& m = plan_.monsters[mIdx];
            m.count += d;
            if (m.count <= 0) plan_.monsters.erase(plan_.monsters.begin() + mIdx);
        }
        plan_.save();
        sfx::click();
        return true;
    }

    void sendToInitiative(bool rolledHp) {
        int added = encounter::enqueue(plan_.monsters, rolledHp);
        if (!added) { sfx::error(); toast("Add monsters first (Fn+A)", C_BLOOD); return; }
        sfx::crit();
        showPlan_ = false;
        requestApp("Initiative");
    }

    void drawPlan() {
        drawBackground();
        drawHeader("Encounter", ACCENT);
        int xp = plan_.totalXp();
        int b[3];
        for (int d = 0; d < 3; d++) b[d] = encounter::budget(plan_.partyLevel, d) * plan_.partySize;
        // difficulty meter
        int y = HEADER_H + 3;
        gfx.setFont(&fonts::Font0);
        int diff = xp == 0 ? -1 : xp <= b[0] ? 0 : xp <= b[1] ? 1 : xp <= b[2] ? 2 : 3;
        const char* label = diff < 0 ? "Empty" : diff == 0 ? "Low" : diff == 1 ? "Moderate" : diff == 2 ? "High" : "Beyond High!";
        uint16_t col = diff <= 0 ? C_EMERALD : diff == 1 ? C_GOLD : diff == 2 ? rgb(240, 130, 60) : C_BLOOD;
        float scale = b[2] * 1.25f;
        int bx = 6, bw = W - 12;
        gfx.fillRect(bx, y + 10, bw, 6, C_BG2);
        gfx.fillRect(bx, y + 10, std::min(bw, (int)(bw * xp / scale)), 6, col);
        for (int d = 0; d < 3; d++) {
            int mx = bx + (int)(bw * b[d] / scale);
            gfx.drawFastVLine(mx, y + 8, 10, C_TEXT);
        }
        gfx.setTextDatum(top_left);
        gfx.setTextColor(col);
        gfx.drawString(String(xp) + " XP: " + label, 6, y);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String("L") + b[0] + " M" + b[1] + " H" + b[2], W - 6, y);

        const int rowH = 11, y0 = y + 21;
        int rows = (H - FOOTER_H - y0) / rowH, n = planRows();
        int first = std::max(0, std::min(planSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int ry = y0 + (i - first) * rowH;
            bool s = i == planSel_;
            if (s) gfx.fillRoundRect(2, ry - 1, W - 4, rowH, 2, C_PANEL);
            String l, r;
            uint16_t c = C_TEXT;
            int mIdx = i - 2, action = i - 2 - (int)plan_.monsters.size();
            if (i == 0) { l = "Party size"; r = String("< ") + plan_.partySize + " >"; c = C_ICE; }
            else if (i == 1) { l = "Party level"; r = String("< ") + plan_.partyLevel + " >"; c = C_ICE; }
            else if (mIdx < (int)plan_.monsters.size()) {
                auto& m = plan_.monsters[mIdx];
                l = m.name + "  (CR " + m.cr + ")";
                r = String("x") + m.count + "  " + (m.xp * m.count) + " XP";
            } else if (action == 0) { l = "Send to Initiative (roll HP)"; c = C_EMERALD; }
            else if (action == 1) { l = "Send to Initiative (average HP)"; c = C_EMERALD; }
            else { l = confirmClear_ ? "Enter again to clear" : "Clear fight"; c = rgb(230, 140, 130); }
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : c);
            gfx.drawString(l.substring(0, 30), 6, ry + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(r, W - 6, ry + 1);
        }
        drawFooter("-/= or </> change  X remove  Esc back");
    }
};

}  // namespace

App* makeMonstersApp() { return new MonstersBrowser(); }
DbBrowser* newMonstersBrowser() { return new MonstersBrowser(); }

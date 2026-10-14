#include "../core/app.h"
#include "../core/dice_expr.h"
#include "../core/prompt.h"
#include "../core/rng.h"
#include "../core/settings.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/character.h"
#include "../core/encounter.h"
#include "db_browser.h"
#include <memory>
#include <algorithm>
#include <vector>

using namespace ui;

namespace {

struct Condition { String name; int rounds; };  // rounds 0 = until removed

struct Combatant {
    String name;
    int init = 0;
    int hp = 0, maxHp = 0;  // maxHp 0 = HP not tracked (e.g. a PC)
    int ac = 0;
    bool pc = false;        // a player character (kept by "remove monsters")
    String monster;         // SRD stat block name, for V (view)
    std::vector<Condition> conds;
    bool tracked() const { return maxHp > 0; }
    bool down() const { return tracked() && hp <= 0; }
};

// D&D 5e conditions, plus common effects worth tracking by duration.
const char* const COND_5E[] = {"Blessed", "Blinded", "Charmed", "Concentrating", "Deafened", "Dodging",
    "Exhaustion", "Frightened", "Grappled", "Hasted", "Hexed", "Hunter's Mark", "Incapacitated", "Invisible",
    "Paralyzed", "Petrified", "Poisoned", "Prone", "Raging", "Restrained", "Slowed", "Stunned", "Unconscious"};

constexpr const char* SAVE_PATH = "/gmtools/encounter.txt";

class InitiativeApp : public App {
public:
    const char* name() const override { return "Initiative"; }
    const char* blurb() const override { return "Turn order, HP & conditions."; }
    uint16_t accent() const override { return C_BLOOD; }

    void drawIcon(int cx, int cy, float t) override {
        // crossed swords
        for (int s = -1; s <= 1; s += 2) {
            gfx.drawWideLine(cx - 14 * s, cy + 14, cx + 12 * s, cy - 14, 3, C_TEXT);
            gfx.drawWideLine(cx - 15 * s + 4 * s, cy + 6, cx - 7 * s + 4 * s, cy + 14, 2, C_GOLD);
        }
        gfx.fillCircle(cx, cy, 3 + (int)(2 * fabsf(sinf(t * 2))), C_BLOOD);
    }

    void onEnter() override {
        input::setTextMode(false);
        if (!loaded_) { load(); loaded_ = true; }
        loadConditions();
        // combatants sent from the Monsters encounter builder
        if (!encounter::queue.empty()) {
            for (auto& p : encounter::queue) {
                Combatant c;
                c.name = p.name; c.init = p.init; c.hp = p.hp; c.maxHp = p.maxHp; c.ac = p.ac; c.monster = p.monster;
                insertSorted(c);
            }
            toast((String(encounter::queue.size()) + " combatants joined").c_str(), C_GOLD);
            encounter::queue.clear();
            sel_ = turn_;
            save();
        }
    }
    void onExit() override { save(); closeViewer(); }

    bool handle(const input::Event& e) override {
        bool stay = handleKey(e);
        saveAt_ = millis() + 1500;   // autosave shortly after changes (see tick)
        return stay;
    }

    bool handleKey(const input::Event& e) {
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done()) onPrompt(prompt_.tag(), prompt_.value());
            else if (prompt_.cancelled()) step_ = 0;
            return true;
        }
        if (viewer_) {
            if (!viewer_->handle(e)) closeViewer();
            return true;
        }
        if (ruleShown_) {
            if (e.key == input::K_UP) ruleTv_.scroll(-1);
            else if (e.key == input::K_DOWN) ruleTv_.scroll(1);
            else { ruleShown_ = false; sfx::back(); }
            return true;
        }
        if (picking_) return handlePicker(e);
        if (menu_) return handleMenu(e);

        int n = list_.size();
        if (e.key != input::K_CHAR || e.base != 'x') confirmDelete_ = false;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: if (n) { sel_ = (sel_ + n - 1) % n; sfx::click(); } return true;
            case input::K_DOWN: if (n) { sel_ = (sel_ + 1) % n; sfx::click(); } return true;
            case input::K_ENTER: nextTurn(); return true;
            case input::K_DEL: removeSelected(); return true;
            case input::K_TAB: menu_ = true; menuSel_ = 0; menuConfirm_ = -1; sfx::select(); return true;
            case input::K_CHAR: break;
            default: return true;
        }
        switch (e.base) {
            case ' ': nextTurn(); break;
            case 'a': step_ = 1; prompt_.open("Name (add combatant)", "", 1, 16); break;
            case 'd': if (n) prompt_.open("Damage", "", 10, 10); break;
            case 'h': if (n) prompt_.open("Heal", "", 11, 10); break;
            case 'c': if (n) { picking_ = true; pickSel_ = 0; sfx::select(); } break;
            case 'u': duplicate(); break;
            case 'v': viewMonster(); break;
            case 'i': setInitiative(); break;
            case 'm': case '\t': menu_ = true; menuSel_ = 0; menuConfirm_ = -1; sfx::select(); break;
            case 'x': removeSelected(); break;
            case 'n':
                if (confirmClear_) { list_.clear(); round_ = 1; turn_ = 0; sel_ = 0; confirmClear_ = false; toast("Encounter cleared"); }
                else { confirmClear_ = true; toast("Press N again to clear", C_BLOOD); }
                return true;
        }
        confirmClear_ = false;
        return true;
    }

    void tick(uint32_t dt) override {
        if (saveAt_ && millis() >= saveAt_) { saveAt_ = 0; save(); }
        int n = list_.size();
        float target = n ? (float)turn_ : 0;
        turnAnim_ = approach(turnAnim_, target, std::min(1.0f, dt * 0.02f));
        if (flash_) flash_ = flash_ > dt ? flash_ - dt : 0;
    }

    void draw() override {
        drawBackground();
        drawHeader("Initiative", C_BLOOD);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(String("Round ") + round_, W - 30, HEADER_H / 2);

        if (list_.empty()) {
            gfx.setTextColor(C_DIM);
            gfx.setTextDatum(middle_center);
            gfx.drawString("No combatants yet.", W / 2, 55);
            gfx.drawString("Press A to add one.", W / 2, 68);
            gfx.drawString("Init: number, +mod, or dice", W / 2, 84);
            gfx.drawString("HP: number or dice (blank = PC)", W / 2, 95);
        } else {
            drawList();
        }

        if (viewer_) { viewer_->draw(); return; }
        if (picking_) drawPicker();
        if (ruleShown_) drawRule();
        if (menu_) drawMenu();
        drawFooter(picking_ ? "Ent add  > read rule  Esc cancel" : menu_ ? "Ent choose  Esc close" :
                   "Ent next A add D dmg H heal V stats M menu");
        const char* hint = nullptr;
        if (prompt_.tag() == 2) hint = "15, +3 (rolls d20+3), or 2d6";
        if (prompt_.tag() == 3) hint = "12, 2d8+2, or blank for a PC";
        if (prompt_.tag() == 20) hint = "rounds, 0 = until removed";
        prompt_.draw(hint);
    }

private:
    std::vector<Combatant> list_;
    std::vector<String> conds_;
    Prompt prompt_;
    Combatant pending_;
    int sel_ = 0, turn_ = 0, round_ = 1, step_ = 0;
    bool loaded_ = false, picking_ = false, confirmClear_ = false, confirmDelete_ = false;
    int pickSel_ = 0;
    bool menu_ = false;
    int menuSel_ = 0, menuConfirm_ = -1;
    String pendingCond_;
    float turnAnim_ = 0;
    uint32_t flash_ = 0, saveAt_ = 0;
    String lastSaved_;

    static constexpr int ROW_H = 13, ROWS = 6, TOP = HEADER_H + 3;

    void loadConditions() {
        conds_.clear();
        std::vector<String> lines;
        if (storage::readLines("/gmtools/conditions.txt", lines) && !lines.empty()) { conds_ = lines; return; }
        for (auto c : COND_5E) conds_.push_back(c);
    }

    // Accepts "15", "+3"/"-1" (roll d20+mod), or any dice expression.
    static bool evalNumber(const String& sIn, int& out, bool modMeansD20) {
        String s = sIn;
        s.trim();
        if (!s.length()) return false;
        if (modMeansD20 && (s[0] == '+' || s[0] == '-')) s = "d20" + s;
        auto r = dice::roll(s);
        if (!r.ok) return false;
        out = r.total;
        return true;
    }

    void onPrompt(int tag, const String& v) {
        switch (tag) {
            case 1:  // name
                if (!v.length()) { step_ = 0; return; }
                pending_ = Combatant();
                pending_.name = v;
                prompt_.open("Initiative", "", 2, 12);
                break;
            case 2: {
                int x;
                if (!evalNumber(v, x, true)) { sfx::error(); toast("Bad initiative", C_BLOOD); prompt_.open("Initiative", v, 2, 12); return; }
                pending_.init = x;
                prompt_.open("HP", "", 3, 12);
                break;
            }
            case 3: {
                int x = 0;
                if (v.length() && !evalNumber(v, x, false)) { sfx::error(); prompt_.open("HP", v, 3, 12); return; }
                pending_.hp = pending_.maxHp = std::max(0, x);
                insertSorted(pending_);
                toast((pending_.name + " joins (init " + pending_.init + ")").c_str());
                step_ = 0;
                break;
            }
            case 10:
            case 11: {
                int x;
                if (!evalNumber(v, x, false)) return;
                Combatant& c = list_[sel_];
                if (!c.tracked()) { toast("HP not tracked", C_DIM); return; }
                if (tag == 10) { c.hp = std::max(0, c.hp - x); flash_ = 300; sfx::fumble(); }
                else { c.hp = std::min(c.maxHp, c.hp + x); sfx::crit(); }
                if (c.down()) toast((c.name + " is down!").c_str(), C_BLOOD);
                break;
            }
            case 12: {
                int x;
                if (!evalNumber(v, x, true)) return;
                Combatant c = list_[sel_];
                bool wasTurn = sel_ == turn_;
                list_.erase(list_.begin() + sel_);
                if (sel_ < turn_) turn_--;
                c.init = x;
                int at = insertSorted(c);
                if (wasTurn) turn_ = at;
                break;
            }
            case 13:
                if (v.length()) list_[sel_].name = v;
                break;
            case 14: {
                int x;
                Combatant& c = list_[sel_];
                if (!v.length()) { c.maxHp = c.hp = 0; break; }  // blank = stop tracking (PC)
                if (!evalNumber(v, x, false) || x <= 0) { sfx::error(); return; }
                // keep damage taken when max changes
                int dmg = c.tracked() ? c.maxHp - c.hp : 0;
                c.maxHp = x;
                c.hp = std::max(0, x - dmg);
                break;
            }
            case 20: {
                int r = v.toInt();
                list_[sel_].conds.push_back({pendingCond_, std::max(0, r)});
                break;
            }
        }
    }

    int insertSorted(const Combatant& c) {
        // descending initiative; ties go after existing entries
        size_t i = 0;
        while (i < list_.size() && list_[i].init >= c.init) i++;
        list_.insert(list_.begin() + i, c);
        if ((int)i <= turn_ && list_.size() > 1) turn_++;
        sel_ = i;
        return i;
    }

    void duplicate() {
        if (list_.empty()) return;
        Combatant c = list_[sel_];
        // "Goblin" -> "Goblin 2", "Goblin 2" -> "Goblin 3"
        String base = c.name;
        int num = 1;
        int sp = base.lastIndexOf(' ');
        if (sp > 0 && base.substring(sp + 1).toInt() > 0) { num = base.substring(sp + 1).toInt(); base = base.substring(0, sp); }
        int highest = num;
        for (auto& o : list_) {
            if (o.name.startsWith(base + " ")) highest = std::max(highest, (int)o.name.substring(base.length() + 1).toInt());
        }
        c.name = base + " " + (highest + 1);
        c.hp = c.maxHp;
        c.conds.clear();
        insertSorted(c);
        sfx::select();
    }

    void removeSelected() {
        if (list_.empty()) return;
        if (!confirmDelete_) { confirmDelete_ = true; toast(("X again to remove " + list_[sel_].name).c_str(), C_BLOOD); return; }
        confirmDelete_ = false;
        list_.erase(list_.begin() + sel_);
        if (sel_ < turn_) turn_--;
        int n = list_.size();
        if (turn_ >= n) turn_ = 0;
        if (sel_ >= n) sel_ = std::max(0, n - 1);
        sfx::back();
    }

    void nextTurn() {
        int n = list_.size();
        if (!n) return;
        // conditions tick down at the end of their owner's turn
        Combatant& cur = list_[turn_];
        for (int i = cur.conds.size() - 1; i >= 0; i--) {
            auto& c = cur.conds[i];
            if (c.rounds > 0 && --c.rounds == 0) {
                toast((cur.name + ": " + c.name + " ends").c_str(), C_ICE);
                cur.conds.erase(cur.conds.begin() + i);
            }
        }
        turn_++;
        if (turn_ >= n) {
            turn_ = 0;
            round_++;
            turnAnim_ = -1;
            sfx::alarm();
            char b[24];
            snprintf(b, sizeof(b), "Round %d", round_);
            toast(b, C_GOLD);
        } else {
            sfx::select();
        }
        sel_ = turn_;
    }

    // ---- actions menu ----
    struct MenuItem { const char* label; const char* key; bool needsSel; bool dangerous; };
    static constexpr int MENU_N = 10;
    const MenuItem MENU[MENU_N] = {
        {"Set initiative", "I", true, false},
        {"Rename", "", true, false},
        {"Set max HP (blank = PC)", "", true, false},
        {"Duplicate", "U", true, false},
        {"Add / remove condition", "C", true, false},
        {"Remove selected", "X X", true, true},
        {"Restart at round 1", "", false, true},
        {"Remove monsters, keep PCs", "", false, true},
        {"Clear encounter", "N N", false, true},
        {"Add party (saved characters)", "", false, false},
    };

    void setInitiative() {
        if (list_.empty()) return;
        prompt_.open((String("Initiative: ") + list_[sel_].name).c_str(), String(list_[sel_].init), 12, 8);
    }

    bool handleMenu(const input::Event& e) {
        if (e.key == input::K_BACK || e.key == input::K_TAB) { menu_ = false; sfx::back(); return true; }
        if (e.key == input::K_UP) { menuSel_ = (menuSel_ + MENU_N - 1) % MENU_N; menuConfirm_ = -1; sfx::click(); }
        if (e.key == input::K_DOWN) { menuSel_ = (menuSel_ + 1) % MENU_N; menuConfirm_ = -1; sfx::click(); }
        if (e.key != input::K_ENTER) return true;
        const MenuItem& m = MENU[menuSel_];
        if (m.needsSel && list_.empty()) { sfx::error(); return true; }
        if (m.dangerous && menuConfirm_ != menuSel_) { menuConfirm_ = menuSel_; sfx::error(); return true; }
        menu_ = false;
        menuConfirm_ = -1;
        switch (menuSel_) {
            case 0: setInitiative(); break;
            case 1: prompt_.open("Rename", list_[sel_].name, 13, 16); break;
            case 2: prompt_.open("Max HP", list_[sel_].tracked() ? String(list_[sel_].maxHp) : String(""), 14, 12); break;
            case 3: duplicate(); break;
            case 4: picking_ = true; pickSel_ = 0; sfx::select(); break;
            case 5: confirmDelete_ = true; removeSelected(); break;
            case 6:
                round_ = 1; turn_ = 0; sel_ = 0;
                for (auto& c : list_) c.conds.clear();
                toast("Back to round 1", C_GOLD);
                break;
            case 7: {
                // PCs are combatants without tracked HP
                std::vector<Combatant> keep;
                for (auto& c : list_) if (c.pc || !c.tracked()) { keep.push_back(c); keep.back().conds.clear(); }
                int removed = list_.size() - keep.size();
                list_ = keep;
                round_ = 1; turn_ = 0; sel_ = 0;
                toast((String("Removed ") + removed + " monsters").c_str(), C_GOLD);
                break;
            }
            case 8:
                list_.clear(); round_ = 1; turn_ = 0; sel_ = 0;
                toast("Encounter cleared", C_GOLD);
                break;
            case 9: addParty(); break;
        }
        save();
        sfx::select();
        return true;
    }

    void drawMenu() {
        const int x = 22, y = 17, w = W - 44, h = 108;
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_BLOOD);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_BLOOD);
        String who = list_.empty() ? String("Encounter") : list_[sel_].name;
        gfx.drawString(who, x + 6, y + 4);
        for (int i = 0; i < MENU_N; i++) {
            int ry = y + 16 + i * 9;
            bool s = i == menuSel_;
            bool disabled = MENU[i].needsSel && list_.empty();
            if (s) gfx.fillRect(x + 3, ry - 1, w - 6, 9, menuConfirm_ == i ? C_BLOOD_DK : C_PANEL);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(disabled ? C_EDGE : (MENU[i].dangerous ? rgb(230, 140, 130) : (s ? C_TEXT : rgb(200, 192, 180))));
            gfx.drawString(menuConfirm_ == i ? "Enter again to confirm" : MENU[i].label, x + 8, ry);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(MENU[i].key, x + w - 6, ry);
        }
    }

    bool handlePicker(const input::Event& e) {
        int n = conds_.size();
        if (e.key == input::K_BACK) { picking_ = false; sfx::back(); return true; }
        if (e.key == input::K_UP) { pickSel_ = (pickSel_ + n - 1) % n; sfx::click(); }
        if (e.key == input::K_DOWN) { pickSel_ = (pickSel_ + 1) % n; sfx::click(); }
        if (e.key == input::K_LEFT) { pickSel_ = std::max(0, pickSel_ - 7); sfx::click(); }
        if (e.key == input::K_RIGHT && n) { showRule(conds_[pickSel_]); return true; }
        if (e.key == input::K_ENTER && n) {
            picking_ = false;
            // toggle off if already present
            auto& cs = list_[sel_].conds;
            for (size_t i = 0; i < cs.size(); i++) {
                if (cs[i].name == conds_[pickSel_]) { cs.erase(cs.begin() + i); toast("Condition removed", C_DIM); return true; }
            }
            pendingCond_ = conds_[pickSel_];
            prompt_.open((pendingCond_ + ": rounds / value").c_str(), "0", 20, 3);
            prompt_.restoreTextModeTo(false);
        }
        // jump by first letter
        if (e.key == input::K_CHAR && n) {
            char c = toupper(e.ch);
            for (int i = 1; i <= n; i++) {
                int j = (pickSel_ + i) % n;
                if (toupper(conds_[j][0]) == c) { pickSel_ = j; sfx::click(); break; }
            }
        }
        return true;
    }

    void drawPicker() {
        const int x = 30, y = 20, w = 180, h = 100;
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_GOLD);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(String("Condition for ") + list_[sel_].name, x + 6, y + 4);
        int n = conds_.size(), rows = 7;
        int first = std::max(0, std::min(pickSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int ry = y + 16 + (i - first) * 11;
            bool s = i == pickSel_;
            bool has = false;
            for (auto& c : list_[sel_].conds) if (c.name == conds_[i]) has = true;
            if (s) gfx.fillRect(x + 4, ry - 1, w - 8, 10, C_PANEL);
            gfx.setTextColor(s ? C_GOLD : C_TEXT);
            gfx.drawString(conds_[i], x + 10, ry);
            if (has) { gfx.setTextColor(C_EMERALD); gfx.drawString("on", x + w - 22, ry); }
        }
    }

    void drawList() {
        int n = list_.size();
        int first = std::max(0, std::min(sel_ - ROWS / 2, n - ROWS));
        gfx.setFont(&fonts::Font0);

        // sliding turn marker
        float rel = turnAnim_ - first;
        if (rel > -1 && rel < ROWS) {
            int my = TOP + (int)(rel * ROW_H);
            gfx.fillRoundRect(2, my, W - 4, ROW_H - 1, 3, rgb(60, 28, 34));
        }
        for (int i = first; i < n && i < first + ROWS; i++) {
            const Combatant& c = list_[i];
            int y = TOP + (i - first) * ROW_H;
            bool s = i == sel_, turn = i == turn_;
            if (s) gfx.drawRoundRect(2, y, W - 4, ROW_H - 1, 3, C_GOLD_DK);
            gfx.setTextDatum(middle_left);
            int my = y + ROW_H / 2;
            if (turn) gfx.fillTriangle(5, my - 3, 5, my + 3, 9, my, C_GOLD);
            gfx.setTextColor(C_GOLD);
            gfx.setTextDatum(middle_right);
            gfx.drawString(String(c.init), 26, my);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(c.down() ? C_DIM : (turn ? C_TEXT : rgb(200, 192, 180)));
            String nm = c.name.length() > 14 ? c.name.substring(0, 13) + "." : c.name;
            gfx.drawString(nm, 31, my);
            if (c.down()) gfx.drawFastHLine(31, my, gfx.textWidth(nm), C_BLOOD);

            if (c.tracked()) {
                float f = (float)c.hp / c.maxHp;
                uint16_t col = f > 0.5f ? C_EMERALD : f > 0.25f ? C_GOLD : C_BLOOD;
                if (s && flash_) col = C_TEXT;
                drawBar(118, my - 2, 40, 5, f, col, C_BG2);
                gfx.setTextColor(C_DIM);
                gfx.drawString(String(c.hp) + "/" + c.maxHp, 162, my);
            } else {
                gfx.setTextColor(C_ARCANE);
                gfx.drawString("PC", 118, my);
            }
            if (!c.conds.empty()) {
                String chip = c.conds[0].name.substring(0, 4);
                if (c.conds.size() > 1) chip += "+" + String(c.conds.size() - 1);
                gfx.setTextColor(C_ICE);
                gfx.setTextDatum(middle_right);
                gfx.drawString(chip, W - 5, my);
            }
        }
        // detail line for the selection
        const Combatant& c = list_[sel_];
        int y = TOP + ROWS * ROW_H + 2;
        gfx.drawFastHLine(4, y - 2, W - 8, C_EDGE);
        gfx.setTextDatum(top_left);
        if (c.conds.empty()) {
            gfx.setTextColor(C_DIM);
            gfx.drawString(c.name + (c.ac ? String("  AC ") + c.ac : String("")) + (c.monster.length() ? "  (V: stats)" : "  no conditions"), 6, y);
        } else {
            String s;
            for (auto& k : c.conds) {
                if (s.length()) s += ", ";
                s += k.name;
                if (k.rounds) s += " " + String(k.rounds);
            }
            gfx.setTextColor(C_ICE);
            textWrapped(s.c_str(), 6, y, W - 12, 9, 2);
        }
    }

    // ---- monster stat blocks, SRD condition rules, party import
    std::unique_ptr<DbBrowser> viewer_;
    bool ruleShown_ = false;
    TextView ruleTv_;
    String ruleName_;

    void viewMonster() {
        if (list_.empty()) return;
        String m = list_[sel_].monster;
        if (!m.length()) { toast("No stat block (not from Monsters)", C_DIM); return; }
        viewer_.reset(newMonstersBrowser());
        viewer_->closeOnBack = true;
        viewer_->onEnter();
        if (!viewer_->openByName(m)) { closeViewer(); toast("Stat block not found", C_BLOOD); }
    }
    void closeViewer() {
        if (viewer_) { viewer_->onExit(); viewer_.reset(); input::setTextMode(false); }
    }

    void showRule(const String& cond) {
        db::Rec r;
        ruleTv_.clear();
        ruleName_ = cond;
        if (db::findRecord("/gmtools/srd/glossary", cond, r)) {
            String t = db::unescape(r.get("text"));
            int start = 0;
            while (start <= (int)t.length()) {
                int nl = t.indexOf('\n', start);
                ruleTv_.add(nl < 0 ? t.substring(start) : t.substring(start, nl), TextView::BODY, W - 24);
                if (nl < 0) break;
                start = nl + 1;
            }
        } else {
            ruleTv_.add("No SRD rule text for this effect.", TextView::DIM);
        }
        ruleShown_ = true;
        sfx::select();
    }
    void drawRule() {
        const int x = 8, y = 18, w = W - 16, h = H - FOOTER_H - 20;
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_ICE);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_ICE);
        gfx.drawString(ruleName_, x + 6, y + 3);
        ruleTv_.draw(x + 6, y + 14, h - 16, C_ICE);
    }

    void addParty() {
        std::vector<String> files;
        storage::listFiles("/gmtools/characters", files);
        int added = 0;
        for (auto& f : files) {
            dnd::Character ch;
            if (!ch.load(String("/gmtools/characters/") + f)) continue;
            bool exists = false;
            for (auto& c : list_) if (c.name == ch.name) exists = true;
            if (exists) continue;
            Combatant c;
            c.name = ch.name;
            c.init = dice::roll(String("d20") + dnd::fmtMod(ch.initiative())).total;
            c.hp = ch.hp; c.maxHp = ch.hpMax; c.ac = ch.ac(); c.pc = true;
            insertSorted(c);
            added++;
        }
        toast(added ? (String(added) + " heroes rolled initiative").c_str() : "No saved characters to add", added ? C_GOLD : C_DIM);
    }

    // ---- persistence: "round|turn" then "name|init|hp|max|cond:r,cond:r|ac|pc|monster"
    void save() {
        if (!storage::ok()) return;
        auto clean = [](String v) { v.replace("|", "/"); v.replace("\n", " "); return v; };
        String out = String(round_) + "|" + turn_ + "\n";
        for (auto& c : list_) {
            out += clean(c.name) + "|" + c.init + "|" + c.hp + "|" + c.maxHp + "|";
            for (size_t i = 0; i < c.conds.size(); i++) out += (i ? "," : "") + clean(c.conds[i].name) + ":" + c.conds[i].rounds;
            out += String("|") + c.ac + "|" + (c.pc ? "1" : "0") + "|" + clean(c.monster) + "\n";
        }
        if (out == lastSaved_) return;
        if (storage::writeText(SAVE_PATH, out)) lastSaved_ = out;
    }

    void load() {
        // not readLines: it drops lines starting with '#', and a combatant may be "#2"
        String txt;
        if (!storage::readText(SAVE_PATH, txt, 16384)) return;
        std::vector<String> lines;
        for (auto& l : dnd::splitList(txt, '\n')) { String t = l; t.trim(); if (t.length()) lines.push_back(t); }
        if (lines.empty()) return;
        int bar = lines[0].indexOf('|');
        round_ = std::max(1, (int)lines[0].substring(0, bar).toInt());
        turn_ = lines[0].substring(bar + 1).toInt();
        for (size_t i = 1; i < lines.size(); i++) {
            String f[8];
            int start = 0;
            for (int k = 0; k < 8; k++) {
                int b = lines[i].indexOf('|', start);
                f[k] = b < 0 ? lines[i].substring(start) : lines[i].substring(start, b);
                start = b < 0 ? lines[i].length() : b + 1;
            }
            Combatant c;
            c.name = f[0]; c.init = f[1].toInt(); c.hp = f[2].toInt(); c.maxHp = f[3].toInt();
            c.ac = f[5].toInt(); c.pc = f[6] == "1"; c.monster = f[7];
            int s = 0;
            while (s < (int)f[4].length()) {
                int comma = f[4].indexOf(',', s);
                String tok = comma < 0 ? f[4].substring(s) : f[4].substring(s, comma);
                int colon = tok.lastIndexOf(':');
                if (colon > 0) c.conds.push_back({tok.substring(0, colon), (int)tok.substring(colon + 1).toInt()});
                s = comma < 0 ? f[4].length() : comma + 1;
            }
            list_.push_back(c);
        }
        if (turn_ >= (int)list_.size()) turn_ = 0;
        sel_ = turn_;
        turnAnim_ = turn_;
    }
};

}  // namespace

App* makeInitiativeApp() { return new InitiativeApp(); }

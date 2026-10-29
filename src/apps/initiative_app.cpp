#include "../core/app.h"
#include "../core/touch.h"
#include "../core/dice_expr.h"
#include "../core/hold_menu.h"
#include "../core/prompt.h"
#include "../core/rng.h"
#include "../core/settings.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/character.h"
#include "../core/encounter.h"
#include "db_browser.h"
#include "../core/roll_overlay.h"
#include "../core/web.h"
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

    bool bigLayout() const override { return true; }

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
        mergeQueue();
    }

    // Combatants sent from the encounter builders.
    int mergeQueue() {
        const int n = encounter::queue.size();
        if (!n) return 0;
        for (auto& p : encounter::queue) {
            Combatant c;
            c.name = p.name; c.init = p.init; c.hp = p.hp; c.maxHp = p.maxHp; c.ac = p.ac; c.monster = p.monster;
            insertSorted(c);
        }
        toast((String(n) + " combatants joined").c_str(), C_GOLD);
        encounter::queue.clear();
        sel_ = turn_;
        save();
        return n;
    }

    // The web page's combat tracker (the Table page). Each change names the
    // combatant (i and who) so a stale page can't hit the wrong one.
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (!loaded_) { load(); loaded_ = true; }
        if (!encounter::queue.empty() && c.op == "state") {   // sent from Encounters or Dungeon23 meanwhile
            mergeQueue();
            if (prompt_.active() && prompt_.tag() >= 10) { prompt_.cancel(); step_ = 0; }
            picking_ = menu_ = confirmDelete_ = confirmClear_ = false;
        }
        const String& op = c.op;
        const int i = c.num("i", -1);
        const bool valid = i >= 0 && i < (int)list_.size() && list_[i].name == c.arg("who");
        auto needOne = [&]() { if (!valid) out = "That combatant changed - reload"; return valid; };
        if (op == "next") nextTurn();
        else if (op == "prev") {
            if (!list_.empty()) {
                if (turn_ == 0) { if (round_ > 1) { round_--; turn_ = list_.size() - 1; } }
                else turn_--;
                sel_ = turn_;
            }
        } else if (op == "hp") {
            if (!needOne()) return 409;
            Combatant& k = list_[i];
            if (!k.tracked()) { out = "HP not tracked"; return 400; }
            const bool wasDown = k.down();
            if (c.arg("set").length()) k.hp = std::max(0, std::min(k.maxHp, c.num("set")));
            else k.hp = std::max(0, std::min(k.maxHp, k.hp + c.num("d")));
            if (k.down() && !wasDown) toast((k.name + " is down!").c_str(), C_BLOOD);
        } else if (op == "maxhp") {
            if (!needOne()) return 409;
            Combatant& k = list_[i];
            const int x = c.num("max");
            const int dmg = k.tracked() ? k.maxHp - k.hp : 0;
            k.maxHp = std::max(0, x);
            k.hp = std::max(0, k.maxHp - dmg);
        } else if (op == "cond") {
            if (!needOne()) return 409;
            const String n = c.arg("name");
            if (!n.length()) { out = "No condition"; return 400; }
            auto& cs = list_[i].conds;
            bool had = false;
            for (int k = cs.size() - 1; k >= 0; k--) if (cs[k].name == n) { cs.erase(cs.begin() + k); had = true; }
            if (!had || c.arg("rounds").length()) cs.push_back({n, std::max(0, c.num("rounds"))});
        } else if (op == "uncond") {
            if (!needOne()) return 409;
            auto& cs = list_[i].conds;
            for (int k = cs.size() - 1; k >= 0; k--) if (cs[k].name == c.arg("name")) cs.erase(cs.begin() + k);
        } else if (op == "init") {
            if (!needOne()) return 409;
            int x;
            if (!evalNumber(c.arg("value"), x, true)) { out = "Bad initiative"; return 400; }
            Combatant k = list_[i];
            const bool wasTurn = i == turn_;
            list_.erase(list_.begin() + i);
            if (i < turn_) turn_--;
            k.init = x;
            const int at = insertSorted(k);
            if (wasTurn) turn_ = at;
        } else if (op == "rename") {
            if (!needOne()) return 409;
            if (c.arg("name").length()) list_[i].name = c.arg("name");
        } else if (op == "remove") {
            if (!needOne()) return 409;
            list_.erase(list_.begin() + i);
            if (i < turn_) turn_--;
            if (turn_ >= (int)list_.size()) turn_ = 0;
            sel_ = std::min(sel_, std::max(0, (int)list_.size() - 1));
        } else if (op == "dup") {
            if (!needOne()) return 409;
            sel_ = i;
            duplicate();
        } else if (op == "add") {
            Combatant k;
            k.name = c.arg("name");
            k.name.trim();
            if (!k.name.length()) { out = "No name"; return 400; }
            if (!evalNumber(c.arg("init").length() ? c.arg("init") : String("+0"), k.init, true)) { out = "Bad initiative"; return 400; }
            int hp = 0;
            if (c.arg("hp").length() && !evalNumber(c.arg("hp"), hp, false)) { out = "Bad HP"; return 400; }
            k.hp = k.maxHp = std::max(0, hp);
            k.ac = c.num("ac");
            k.pc = c.arg("pc") == "1";
            insertSorted(k);
            toast((k.name + " joins (init " + k.init + ")").c_str());
        } else if (op == "monsters") {
            String unknown;
            auto list = encounter::parseList(c.arg("text").length() ? c.arg("text") : c.body, unknown);
            if (list.empty()) { out = unknown.length() ? "Unknown: " + unknown : String("No monsters found (e.g. 3x Goblin Warrior)"); return 400; }
            encounter::enqueue(list, c.arg("rolled") != "0");
            mergeQueue();
        } else if (op == "party") addParty();
        else if (op == "restart") { round_ = 1; turn_ = 0; sel_ = 0; }
        else if (op == "nomonsters") {
            for (int k = list_.size() - 1; k >= 0; k--) if (!list_[k].pc) { list_.erase(list_.begin() + k); if (k < turn_) turn_--; }
            if (turn_ >= (int)list_.size()) turn_ = 0;
            sel_ = 0;
        } else if (op == "clear") { list_.clear(); round_ = 1; turn_ = sel_ = 0; }
        else if (op != "state") return 404;
        if (op != "state") save();
        // the list moved under the device: drop anything there aimed at one combatant
        if (op == "remove" || op == "clear" || op == "nomonsters" || op == "init" || op == "add" || op == "dup" ||
            op == "monsters" || op == "party") {
            if (prompt_.active() && prompt_.tag() >= 10) { prompt_.cancel(); step_ = 0; }
            picking_ = menu_ = confirmDelete_ = confirmClear_ = false;
        }
        // the state, after any change
        out = String("{\"round\":") + round_ + ",\"turn\":" + turn_ + ",\"list\":[";
        for (size_t k = 0; k < list_.size(); k++) {
            const Combatant& m = list_[k];
            out += String(k ? "," : "") + "{\"name\":" + web::js(m.name) + ",\"init\":" + m.init + ",\"hp\":" + m.hp +
                   ",\"max\":" + m.maxHp + ",\"ac\":" + m.ac + ",\"pc\":" + (m.pc ? "true" : "false") +
                   ",\"monster\":" + web::js(m.monster) + ",\"conds\":[";
            for (size_t j = 0; j < m.conds.size(); j++)
                out += String(j ? "," : "") + "{\"name\":" + web::js(m.conds[j].name) + ",\"rounds\":" + m.conds[j].rounds + "}";
            out += "]}";
        }
        out += "],\"conditions\":[";
        for (size_t k = 0; k < sizeof(COND_5E) / sizeof(COND_5E[0]); k++) out += String(k ? "," : "") + web::js(COND_5E[k]);
        out += "]}";
        return 200;
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
        if (wide()) {
            drawCombat();
            drawOverlays();
            return;
        }
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(String("Round ") + round_, W - u(30), HEADER_H / 2);

        if (list_.empty()) {
            gfx.setTextColor(C_DIM);
            gfx.setTextDatum(middle_center);
            gfx.drawString("No combatants yet.", W / 2, u(55));
            gfx.drawString("Press A to add one.", W / 2, u(68));
            gfx.drawString("Init: number, +mod, or dice", W / 2, u(84));
            gfx.drawString("HP: number or dice (blank = PC)", W / 2, u(95));
        } else {
            drawList();
        }

        drawOverlays();
    }

    void drawOverlays() {
        if (viewer_) { viewer_->draw(); return; }
        if (picking_) drawPicker();
        if (ruleShown_) drawRule();
        if (menu_) drawMenu();
        drawFooter(picking_ ? "Ent add  > read rule  Esc cancel" : menu_ ? "Ent choose  Esc close" :
                   wide() ? "Ent next  A add  M menu  hold cards for more" : "Ent next A add D dmg H heal V stats M menu");
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

    // list rows: 6 on the Cardputer
    int rowH() const { return touchRow(13); }
    int rows() const { return wide() ? (H - FOOTER_H - top() - u(4)) / rowH() : 6; }
    int top() const { return HEADER_H + u(wide() ? 12 : 3); }

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
        if (tag >= 10 && sel_ >= (int)list_.size()) return;   // the combatant went away meanwhile
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

    // A grid of big buttons (the big screen's menus and pickers).
    // label(i) gives each button's text and colour; sel is the keyboard selection.
    template <class F>
    void drawGrid(const char* title, uint16_t edge, int n, int cols, int& sel, F label) {
        const Metrics& m = metrics();
        const int rowsN = (n + cols - 1) / cols, cellH = 64, g = 8;
        const int w = W - 120, h = 64 + rowsN * (cellH + g) + 12;
        const int x = (W - w) / 2, y = std::max(HEADER_H + 4, (H - h) / 2);
        gfx.fillRoundRect(x + 6, y + 6, w, h, 14, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, edge);
        touch::modal(x, y, w, h);
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(edge);
        gfx.drawString(fitText(title, w - 40), x + 20, y + 12);
        const int cw = (w - 40 - (cols - 1) * g) / cols;
        gfx.setFont(m.body);
        for (int i = 0; i < n; i++) {
            const int cx = x + 20 + (i % cols) * (cw + g), cy = y + 64 + (i / cols) * (cellH + g);
            const bool s = i == sel;
            uint16_t col = C_TEXT;
            String t, right;
            label(i, t, right, col);
            gfx.fillRoundRect(cx, cy, cw, cellH, 10, s ? C_PANEL : C_BG);
            gfx.drawRoundRect(cx, cy, cw, cellH, 10, s ? edge : C_EDGE);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(right, cx + cw - 14, cy + cellH / 2);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(col);
            gfx.drawString(fitText(t, cw - 28 - (right.length() ? gfx.textWidth(right) + 10 : 0)), cx + 14, cy + cellH / 2);
            touch::row(cx, cy, cw, cellH, sel, i);
        }
    }

    void drawMenu() {
        if (wide()) {
            String who = list_.empty() ? String("Encounter") : list_[sel_].name;
            drawGrid(who.c_str(), C_BLOOD, MENU_N, 2, menuSel_, [this](int i, String& t, String& r, uint16_t& c) {
                bool disabled = MENU[i].needsSel && list_.empty();
                t = menuConfirm_ == i ? "Tap again to confirm" : MENU[i].label;
                r = MENU[i].key;
                c = disabled ? C_EDGE : MENU[i].dangerous || menuConfirm_ == i ? rgb(230, 140, 130) : C_TEXT;
            });
            return;
        }
        const int w = wide() ? u(200) : W - 44, x = (W - w) / 2, y = u(17), h = u(108);
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_BLOOD);
        touch::modal(x, y, w, h);   // a tap outside closes it
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_BLOOD);
        String who = list_.empty() ? String("Encounter") : list_[sel_].name;
        gfx.drawString(who, x + u(6), y + u(4));
        for (int i = 0; i < MENU_N; i++) {
            int ry = y + u(16) + i * u(9);
            bool s = i == menuSel_;
            bool disabled = MENU[i].needsSel && list_.empty();
            if (s) gfx.fillRect(x + u(3), ry - u(1), w - u(6), u(9), menuConfirm_ == i ? C_BLOOD_DK : C_PANEL);
            touch::row(x, ry - u(1), w, u(9), menuSel_, i);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(disabled ? C_EDGE : (MENU[i].dangerous ? rgb(230, 140, 130) : (s ? C_TEXT : rgb(200, 192, 180))));
            gfx.drawString(menuConfirm_ == i ? "Enter again to confirm" : MENU[i].label, x + u(8), ry);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(MENU[i].key, x + w - u(6), ry);
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
        if (wide()) {
            drawGrid((String("Condition for ") + list_[sel_].name).c_str(), C_GOLD, conds_.size(), 4, pickSel_,
                     [this](int i, String& t, String& r, uint16_t& c) {
                         t = conds_[i];
                         for (auto& x : list_[sel_].conds) if (x.name == conds_[i]) { r = "on"; c = C_EMERALD; }
                     });
            return;
        }
        const int pickRows = wide() ? 14 : 7;
        const int w = u(180), x = (W - w) / 2, y = u(20), h = u(100) + (pickRows - 7) * u(11);
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_GOLD);
        touch::modal(x, y, w, h);   // a tap outside closes it
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(String("Condition for ") + list_[sel_].name, x + u(6), y + u(4));
        int n = conds_.size(), rows = pickRows;
        int first = std::max(0, std::min(pickSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int ry = y + u(16) + (i - first) * u(11);
            bool s = i == pickSel_;
            touch::row(x, ry - u(1), w, u(11), pickSel_, i);
            bool has = false;
            for (auto& c : list_[sel_].conds) if (c.name == conds_[i]) has = true;
            if (s) gfx.fillRect(x + u(4), ry - u(1), w - u(8), u(10), C_PANEL);
            gfx.setTextColor(s ? C_GOLD : C_TEXT);
            gfx.drawString(conds_[i], x + u(10), ry);
            if (has) { gfx.setTextColor(C_EMERALD); gfx.drawString("on", x + w - u(22), ry); }
        }
    }

    void drawList() {
        const int ROWS = rows(), ROW_H = rowH(), TOP = top();
        int n = list_.size();
        int first = std::max(0, std::min(sel_ - ROWS / 2, n - ROWS));
        gfx.setFont(ui::font(&fonts::Font0));

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

    // ---- big screen: the combat screen. Turn order cards on the left; the DM
    // panel on the right: the selected combatant's HP and conditions, its
    // monster stats with tappable attacks, and a row of quick dice.
    static constexpr int CARD_X = 16, CARD_W = 500, CARD_H = 92, CARD_GAP = 8;
    static constexpr int DM_X = 532;
    static int dmW() { return W - 16 - DM_X; }

    // the selected combatant's SRD monster, looked up when the selection changes
    String monName_;
    bool monOk_ = false;
    db::Rec monRec_;
    std::vector<MonsterRoll> monRolls_;

    void loadMonster(const String& name) {
        if (name == monName_) return;
        monName_ = name;
        monRolls_.clear();
        monOk_ = name.length() && db::findRecord("/gmtools/srd/monsters", name, monRec_);
        if (monOk_) monRolls_ = monsterRolls(monRec_);
    }

    void adjustHp(int d) {
        if (list_.empty()) return;
        Combatant& c = list_[sel_];
        if (!c.tracked()) return;
        const bool wasDown = c.down();
        c.hp = std::max(0, std::min(c.maxHp, c.hp + d));
        if (d < 0) { flash_ = 300; sfx::click(); }
        else sfx::select();
        if (c.down() && !wasDown) toast((c.name + " is down!").c_str(), C_BLOOD);
        saveAt_ = millis() + 1500;
    }

    void key(char ch) {
        input::Event e;
        e.key = input::K_CHAR;
        e.ch = e.base = ch;
        input::inject(e);
    }

    // Short names and colours for condition badges: helpful ones green,
    // concentration violet, the rest red.
    static String condAbbr(const String& n) {
        static const char* const AB[][2] = {{"Blessed", "Bs"}, {"Blinded", "Bd"}, {"Concentrating", "Co"}, {"Deafened", "De"},
            {"Dodging", "Do"}, {"Exhaustion", "Ex"}, {"Hasted", "Ha"}, {"Hexed", "Hx"}, {"Hunter's Mark", "HM"},
            {"Incapacitated", "In"}, {"Invisible", "Iv"}, {"Paralyzed", "Pa"}, {"Petrified", "Pe"}, {"Poisoned", "Po"},
            {"Prone", "Pr"}, {"Restrained", "Re"}, {"Slowed", "Sl"}, {"Stunned", "St"}, {"Unconscious", "Un"}};
        for (auto& a : AB) if (n == a[0]) return a[1];
        return n.substring(0, 2);
    }
    static uint16_t condColor(const String& n) {
        if (n == "Concentrating") return C_ARCANE;
        for (const char* g : {"Blessed", "Dodging", "Hasted", "Invisible", "Raging"}) if (n == g) return C_EMERALD;
        return C_BLOOD;
    }

    static uint16_t hpColor(float f) { return f > 0.5f ? C_EMERALD : f > 0.25f ? C_GOLD : C_BLOOD; }

    // A rounded button with a centred label; on: highlighted.
    void button(int x, int y, int w, int h, const String& label, uint16_t edge = C_EDGE, uint16_t text = C_TEXT,
                bool on = false) {
        gfx.fillRoundRect(x, y, w, h, 10, on ? C_PANEL : C_BG);
        gfx.drawRoundRect(x, y, w, h, 10, edge);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(text);
        gfx.drawString(fitText(label, w - 12), x + w / 2, y + h / 2);
    }

    void drawShield(int cx, int y, int ac) {
        const uint16_t c = C_ICE;
        // 44 wide, 40 tall: a flat top, straight sides, a point below
        gfx.fillRoundRect(cx - 22, y, 44, 26, 6, C_BG);
        gfx.fillTriangle(cx - 22, y + 22, cx + 22, y + 22, cx, y + 40, C_BG);
        gfx.drawLine(cx - 22, y + 6, cx - 22, y + 22, c);
        gfx.drawLine(cx + 22, y + 6, cx + 22, y + 22, c);
        gfx.drawLine(cx - 22, y + 6, cx - 16, y, c);
        gfx.drawLine(cx + 22, y + 6, cx + 16, y, c);
        gfx.drawLine(cx - 16, y, cx + 16, y, c);
        gfx.drawLine(cx - 22, y + 22, cx, y + 40, c);
        gfx.drawLine(cx + 22, y + 22, cx, y + 40, c);
        gfx.setFont(metrics().bold);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_ICE);
        gfx.drawString(String(ac), cx, y + 17);
    }

    void drawCombat() {
        const Metrics& m = metrics();
        const int top = HEADER_H + 10, bottom = H - FOOTER_H - 10;
        // round, add, next turn
        gfx.setFont(m.title);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(String("Round ") + round_, CARD_X + 4, top + 30);
        gfx.setFont(m.bold);
        button(CARD_X + CARD_W - 330, top, 140, 60, "+ Add", C_EDGE, C_TEXT);
        touch::zone(CARD_X + CARD_W - 330, top, 140, 60, [this] { key('a'); });
        button(CARD_X + CARD_W - 180, top, 180, 60, "Next turn", C_GOLD, C_GOLD, true);
        touch::key(CARD_X + CARD_W - 180, top, 180, 60, input::K_ENTER);

        const int y0 = top + 70, per = CARD_H + CARD_GAP, fit = (bottom - y0 + CARD_GAP) / per;
        const int n = list_.size();
        if (!n) {
            gfx.setFont(m.body);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_DIM);
            gfx.drawString("No combatants yet.", CARD_X + 4, y0 + 10);
            gfx.drawString("Add them here, send a fight", CARD_X + 4, y0 + 50);
            gfx.drawString("from Monsters, or bring in", CARD_X + 4, y0 + 82);
            gfx.drawString("your saved characters:", CARD_X + 4, y0 + 114);
            gfx.setFont(m.bold);
            button(CARD_X, y0 + 160, CARD_W, 64, "Add the party", C_ARCANE, C_ARCANE);
            touch::zone(CARD_X, y0 + 160, CARD_W, 64, [this] { addParty(); save(); });
        }
        const int first = std::max(0, std::min(sel_ - fit / 2, n - fit));
        for (int i = first; i < n && i < first + fit; i++) drawCard(i, CARD_X, y0 + (i - first) * per);
        if (n > fit) {   // where the visible cards are in the whole order
            const int areaH = fit * per - CARD_GAP, sh = std::max(24, areaH * fit / n);
            gfx.fillRoundRect(CARD_X + CARD_W + 4, y0 + (areaH - sh) * first / std::max(1, n - fit), 6, sh, 3, C_BLOOD);
        }
        drawDmPanel(top, bottom);
    }

    // A long press on a card: what can be done to that combatant.
    void holdCard(int i) {
        sel_ = i;
        holdmenu::open(list_[i].name, {{"Damage", [this] { key('d'); }, C_BLOOD},
                                       {"Heal", [this] { key('h'); }, C_EMERALD},
                                       {"Add / remove condition", [this] { key('c'); }},
                                       {"Set initiative", [this] { key('i'); }},
                                       {"Rename", [this] { prompt_.open("Rename", list_[sel_].name, 13, 16); }},
                                       {"Duplicate", [this] { key('u'); }},
                                       {"Remove", [this] { confirmDelete_ = true; removeSelected(); }, C_BLOOD, true}});
    }

    void drawCard(int i, int x, int y) {
        const Metrics& m = metrics();
        const Combatant& c = list_[i];
        const bool s = i == sel_, turn = i == turn_;
        const int w = CARD_W, h = CARD_H;
        gfx.fillRoundRect(x, y, w, h, 14, turn ? C_PANEL : C_BG2);
        if (turn) {
            gfx.drawRoundRect(x, y, w, h, 14, C_GOLD);
            gfx.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, C_GOLD);
        } else {
            gfx.drawRoundRect(x, y, w, h, 14, s ? C_GOLD_DK : C_EDGE);
        }
        if (s && !turn) gfx.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, C_GOLD_DK);
        // a tap selects; tapping the selected card opens its menu
        touch::zone(x, y, w, h, [this, i] {
            if (sel_ == i) key('m');
            else { sel_ = i; sfx::click(); }
        });
        touch::onHold(x, y, w, h, [this, i] { holdCard(i); });

        // initiative badge
        const int bx = x + 46, by = y + h / 2;
        gfx.fillCircle(bx, by, 30, C_BG);
        gfx.drawCircle(bx, by, 30, turn ? C_GOLD : C_EDGE);
        gfx.drawCircle(bx, by, 29, turn ? C_GOLD : C_EDGE);
        gfx.setFont(m.bold);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(String(c.init), bx, by);

        // AC shield on the right, condition badges before it
        const int nx = x + 92;
        int right = x + w - 12;
        if (c.ac) { drawShield(right - 24, y + 7, c.ac); right -= 56; }
        int cx = right - 16;
        const int shown = std::min<int>(c.conds.size(), 4);
        for (int k = shown - 1; k >= 0; k--) {
            const uint16_t col = condColor(c.conds[k].name);
            gfx.fillCircle(cx, y + 26, 16, C_BG);
            gfx.drawCircle(cx, y + 26, 16, col);
            gfx.drawCircle(cx, y + 26, 15, col);
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(col);
            gfx.drawString(condAbbr(c.conds[k].name), cx, y + 27);
            cx -= 38;
        }
        if ((int)c.conds.size() > shown) {
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String("+") + (c.conds.size() - shown), cx + 16, y + 27);
            cx -= 34;
        }
        // tap the badges: pick a condition
        if (!c.conds.empty()) touch::zone(cx + 16, y, right - cx - 16, 50, [this, i] { sel_ = i; key('c'); });

        // name
        gfx.setFont(turn ? m.bold : m.body);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(c.down() ? C_DIM : turn ? C_TEXT : rgb(205, 205, 215));
        const String nm = fitText(c.name, cx + 4 - nx);
        gfx.drawString(nm, nx, y + 27);
        if (c.down()) gfx.fillRect(nx, y + 27, gfx.textWidth(nm), 3, C_BLOOD);

        // HP bar
        const int hy = y + 60, hw = x + w - 140 - nx;
        if (c.tracked()) {
            const float f = (float)c.hp / c.maxHp;
            gfx.fillRoundRect(nx, hy, hw, 18, 9, C_BG);
            const int fw = (int)(hw * f);
            if (fw > 0) gfx.fillRoundRect(nx, hy, std::max(18, fw), 18, 9, s && flash_ ? C_TEXT : hpColor(f));
            gfx.drawRoundRect(nx, hy, hw, 18, 9, C_EDGE);
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(c.down() ? C_BLOOD : C_TEXT);
            gfx.drawString(String(c.hp) + "/" + c.maxHp, x + w - 14, hy + 9);
        } else {
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(C_ARCANE);
            gfx.drawString("Player character", nx, hy + 9);
        }
    }

    void drawDmPanel(int top, int bottom) {
        const Metrics& m = metrics();
        const int x = DM_X, w = dmW(), h = bottom - top, ix = x + 18, iw = w - 36;
        const bool any = !list_.empty();
        const bool turn = any && sel_ == turn_;
        drawPanel(x, top, w, h, C_BG2, turn ? C_GOLD_DK : C_EDGE);
        int y = top + 14;

        // quick dice along the bottom
        static const char* const QUICK[] = {"d4", "d6", "d8", "d10", "d12", "d20", "d100", "adv", "dis"};
        const int qn = 9, qg = 8, qh = 60, qw = (iw - (qn - 1) * qg) / qn, qy = bottom - 14 - qh;
        gfx.setFont(m.small);
        for (int k = 0; k < qn; k++) {
            const int qx = ix + k * (qw + qg);
            button(qx, qy, qw, qh, QUICK[k], k < 7 ? C_GOLD_DK : k == 7 ? C_EMERALD : C_BLOOD, k < 7 ? C_GOLD : C_TEXT);
            const char* e = QUICK[k];
            touch::zone(qx, qy, qw, qh, [e] { rollui::show(e, e); });
        }

        if (!any) {
            gfx.setFont(m.body);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_DIM);
            gfx.drawString("The selected combatant shows here:", ix, y + 6);
            gfx.drawString("HP, conditions, and a monster's", ix, y + 40);
            gfx.drawString("stats and attacks, ready to roll.", ix, y + 74);
            return;
        }
        Combatant& c = list_[sel_];
        loadMonster(c.monster);

        // name, then what it is
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(turn ? C_GOLD : C_TEXT);
        int nameW = iw - (monOk_ ? 170 : 0) - (turn ? 110 : 0);
        String nm = fitText(c.name, nameW);
        gfx.drawString(nm, ix, y);
        if (turn) {
            const int tx = ix + gfx.textWidth(nm) + 14;
            gfx.fillRoundRect(tx, y + 2, 90, 34, 17, C_GOLD_DK);
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_TEXT);
            gfx.drawString("TURN", tx + 45, y + 19);
        }
        if (monOk_) {
            gfx.setFont(m.small);
            button(ix + iw - 160, y - 2, 160, 44, "Stat block", C_EDGE, C_DIM);
            touch::zone(ix + iw - 160, y - 2, 160, 44, [this] { key('v'); });
        }
        y += 50;

        // HP: -5 -1 [hp] +1 +5, Damage..., Heal... (hold a +/- to repeat)
        const int bh = 64, g = 8;
        if (c.tracked()) {
            const int sw = 72, hpw = 160, pw = (iw - 4 * sw - hpw - 6 * g) / 2;
            int bx = ix;
            gfx.setFont(m.bold);
            for (int d : {-5, -1}) {
                button(bx, y, sw, bh, String(d), C_BLOOD, C_BLOOD);
                touch::repeat(bx, y, sw, bh, [this, d] { adjustHp(d); });
                bx += sw + g;
            }
            const float f = (float)c.hp / c.maxHp;
            gfx.fillRoundRect(bx, y, hpw, bh, 10, C_BG);
            gfx.drawRoundRect(bx, y, hpw, bh, 10, hpColor(f));
            gfx.setFont(font(&fonts::FreeSerifBold12pt7b));
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(c.down() ? C_BLOOD : C_TEXT);
            gfx.drawString(String(c.hp) + "/" + c.maxHp, bx + hpw / 2, y + bh / 2 - 6);
            gfx.fillRect(bx + 14, y + bh - 14, (int)((hpw - 28) * f), 6, hpColor(f));
            bx += hpw + g;
            gfx.setFont(m.bold);
            for (int d : {1, 5}) {
                button(bx, y, sw, bh, String("+") + d, C_EMERALD, C_EMERALD);
                touch::repeat(bx, y, sw, bh, [this, d] { adjustHp(d); });
                bx += sw + g;
            }
            button(bx, y, pw, bh, "Hit", C_BLOOD, C_TEXT);
            touch::zone(bx, y, pw, bh, [this] { key('d'); });
            bx += pw + g;
            button(bx, y, pw, bh, "Heal", C_EMERALD, C_TEXT);
            touch::zone(bx, y, pw, bh, [this] { key('h'); });
        } else {
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(C_ARCANE);
            gfx.drawString("Player character: HP not tracked", ix, y + bh / 2);
            gfx.setFont(m.bold);
            button(ix + iw - 180, y, 180, bh, "Track HP", C_EDGE, C_TEXT);
            touch::zone(ix + iw - 180, y, 180, bh, [this] {
                prompt_.open("Max HP", "", 14, 12);
            });
        }
        y += bh + 12;

        // conditions: tap one for its rule, + to add or remove
        int cx = ix;
        const int ch = 52;
        gfx.setFont(m.small);
        for (size_t k = 0; k < c.conds.size(); k++) {
            const auto& cd = c.conds[k];
            String t = cd.name + (cd.rounds ? String(" ") + cd.rounds : String(""));
            const int cw = gfx.textWidth(t) + 32;
            if (cx + cw > ix + iw - 210) break;
            const uint16_t col = condColor(cd.name);
            gfx.fillRoundRect(cx, y, cw, ch, ch / 2, C_BG);
            gfx.drawRoundRect(cx, y, cw, ch, ch / 2, col);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(col);
            gfx.drawString(t, cx + cw / 2, y + ch / 2);
            const String name = cd.name;
            touch::zone(cx, y, cw, ch, [this, name] { showRule(name); });
            cx += cw + g;
        }
        if (c.conds.empty()) {
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(C_DIM);
            gfx.drawString("No conditions", ix, y + ch / 2);
        }
        const int addW = gfx.textWidth("+ Condition") + 40;
        gfx.fillRoundRect(ix + iw - addW, y, addW, ch, ch / 2, C_BG);
        gfx.drawRoundRect(ix + iw - addW, y, addW, ch, ch / 2, C_ICE);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_ICE);
        gfx.drawString("+ Condition", ix + iw - addW / 2, y + ch / 2);
        touch::zone(ix + iw - addW, y, addW, ch, [this] { key('c'); });
        y += ch + 14;
        gfx.drawFastHLine(ix, y, iw, C_EDGE);
        y += 12;

        const int limit = qy - 12;
        if (!monOk_) {
            gfx.setFont(m.body);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_DIM);
            if (c.monster.length()) gfx.drawString(fitText(c.monster + ": no SRD stat block", iw), ix, y);
            else gfx.drawString(c.pc ? "Roll for the party with the dice below." : "Not from the Monsters bestiary.", ix, y);
            return;
        }
        // the monster: a stats line, its ability scores (tap: save), its attacks (tap: roll)
        gfx.setFont(m.small);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString(fitText(String("AC ") + monRec_.get("ac") + "   HP " + monRec_.get("hp") + "   Speed " + monRec_.get("speed") +
                                   "   CR " + monRec_.get("cr"), iw), ix, y);
        y += 32;
        auto sc = dnd::splitList(monRec_.get("scores"), ',');
        auto sv = dnd::splitList(monRec_.get("saves"), ',');
        if (sc.size() == 6) {
            const int aw = (iw - 5 * g) / 6, ah = 56;
            for (int a = 0; a < 6; a++) {
                const int ax = ix + a * (aw + g);
                const String save = a < (int)sv.size() ? sv[a] : dnd::fmtMod(dnd::modOf(sc[a].toInt()));
                gfx.fillRoundRect(ax, y, aw, ah, 10, C_BG);
                gfx.drawRoundRect(ax, y, aw, ah, 10, C_EDGE);
                gfx.setFont(m.small);
                gfx.setTextDatum(middle_left);
                gfx.setTextColor(C_DIM);
                gfx.drawString(dnd::ABIL_SHORT[a], ax + 10, y + ah / 2);
                gfx.setFont(m.bold);
                gfx.setTextDatum(middle_right);
                gfx.setTextColor(C_EMERALD);
                gfx.drawString(dnd::fmtMod(dnd::modOf(sc[a].toInt())), ax + aw - 10, y + ah / 2);
                const String title = c.name + " " + dnd::ABIL_SHORT[a] + " save";
                touch::zone(ax, y, aw, ah, [title, save] { rollui::show(title, {{"Save", "d20" + save, ""}}); });
            }
            y += ah + 10;
        }
        // attacks and other actions, three across
        const int acols = 3, aw = (iw - (acols - 1) * g) / acols, ah = 64;
        int k = 0;
        for (auto& r : monRolls_) {
            if (!r.attack) continue;
            const int ax = ix + (k % acols) * (aw + g), ay = y + (k / acols) * (ah + g);
            if (ay + ah > limit) break;
            // name above, to-hit / DC and damage below; red when there's something to roll
            const bool hasDice = !r.lines.empty();
            gfx.fillRoundRect(ax, ay, aw, ah, 10, C_BG);
            gfx.drawRoundRect(ax, ay, aw, ah, 10, hasDice ? C_BLOOD : C_EDGE);
            gfx.setFont(m.body);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(hasDice ? C_TEXT : C_DIM);
            gfx.drawString(fitText(r.name, aw - 24), ax + 12, ay + 6);
            gfx.setFont(m.small);
            gfx.setTextColor(hasDice ? C_GOLD : C_DIM);
            gfx.drawString(fitText(r.detail, aw - 24), ax + 12, ay + 36);
            const String title = c.name + ": " + r.title.substring(r.title.indexOf(':') + 1);
            const auto lines = r.lines;
            touch::zone(ax, ay, aw, ah, [title, lines] { rollui::show(title, lines); });
            k++;
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
                ruleTv_.add(nl < 0 ? t.substring(start) : t.substring(start, nl), TextView::BODY, W - u(24));
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
        const int x = u(8), y = u(18), w = W - u(16), h = H - FOOTER_H - u(20);
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_ICE);
        touch::modal(x, y, w, h);
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_ICE);
        gfx.drawString(ruleName_, x + u(6), y + u(3));
        ruleTv_.draw(x + u(6), y + u(14), h - u(16), C_ICE);
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

#include "../core/app.h"
#include "../core/character.h"
#include "../core/dice_expr.h"
#include "../core/prompt.h"
#include "../core/roll_overlay.h"
#include "../core/sfx.h"
#include "../core/spellmath.h"
#include "../core/storage.h"
#include "../core/textview.h"
#include "../core/ui.h"
#include "../core/wizard.h"
#include "char_builder.h"
#include "db_browser.h"
#include <memory>

using namespace ui;
using namespace dnd;

// Characters: list, creation wizard, tabbed sheet where every number rolls,
// inventory, spells with slots, features, and levelling up.
//
// Sheet keys: Left/Right switch tabs, Up/Down select, Enter rolls/uses,
// '-' / '=' adjust counters (resources, slots, quantities), Fn+A adds
// (spells / items), X X removes, Esc back.

namespace {

const char* const TABS[] = {"Main", "Abil", "Skill", "Atk", "Spell", "Item", "Feat"};
constexpr int NTABS = 7;
const uint16_t ACCENT = rgb(230, 120, 90);

struct SpellInfo {
    String name;
    int level = 0;
    bool mi = false;            // from Magic Initiate
    String damage, dtype, heal, attack, save, higher;
    bool found = false;
};

enum RowKind : uint8_t {
    R_NONE, R_HP, R_AC, R_INIT, R_HD, R_DEATH, R_INSP, R_RES, R_VALUE, R_SHORT, R_LONG, R_LEVEL,
    R_ABIL, R_SKILL, R_ATTACK, R_SLOTS, R_SPELL, R_COINS, R_ITEM, R_FEATURE, R_TRAIT, R_FEAT
};

struct Row {
    RowKind kind;
    int ref;
    String left, right;
    uint16_t color;
};

class CharactersApp : public App {
public:
    const char* name() const override { return "Characters"; }
    const char* blurb() const override { return "Build, level up & play heroes."; }
    uint16_t accent() const override { return ACCENT; }

    void drawIcon(int cx, int cy, float t) override {
        // helmet
        gfx.fillCircle(cx, cy - 2, 16, rgb(150, 150, 165));
        gfx.fillRect(cx - 16, cy - 2, 32, 16, rgb(150, 150, 165));
        gfx.fillRect(cx - 12, cy + 1, 24, 4, C_BG);
        gfx.fillRect(cx - 2, cy + 1, 4, 14, rgb(150, 150, 165));
        gfx.drawCircle(cx, cy - 2, 16, C_TEXT);
        int plume = (int)(sinf(t * 3) * 2);
        gfx.fillTriangle(cx - 3, cy - 18, cx + 3, cy - 18, cx + plume, cy - 30, C_BLOOD);
    }

    void onEnter() override {
        input::setTextMode(false);
        mode_ = Mode::List;
        refreshList();
    }
    void onExit() override {
        if (loaded_) c_.save();
        closePicker();
        builder_.unload();
        spellTable_.close();
        input::setTextMode(false);
    }

    bool handle(const input::Event& e) override;
    void tick(uint32_t dt) override {}
    void draw() override;

private:
    enum class Mode { List, Create, Sheet, LevelUp, Detail, Picker, ItemMenu };
    Mode mode_ = Mode::List;
    std::vector<String> files_, names_, lines_;
    int listSel_ = 0;
    bool confirmDel_ = false;

    Character c_;
    bool loaded_ = false;
    int tab_ = 0, sel_ = 0, top_ = 0;
    std::vector<Row> rows_;
    Wizard wiz_;
    CharBuilder builder_;
    Prompt prompt_;
    TextView tv_;
    String detailTitle_;
    std::unique_ptr<DbBrowser> picker_;
    std::vector<SpellInfo> spellInfo_;
    db::Table spellTable_;
    std::vector<String> featureNames_;
    std::vector<uint32_t> featureOffs_;
    int itemMenuSel_ = 0, itemRef_ = 0;
    std::vector<String> itemMenu_;
    bool confirmRemove_ = false, confirmLong_ = false;
    int castSlot_ = 0, castRef_ = -1;

    // ------------------------------------------------------------ list
    void refreshList() {
        files_.clear(); names_.clear(); lines_.clear();
        std::vector<String> f;
        storage::listFiles("/gmtools/characters", f);
        for (auto& fn : f) {
            String txt;
            storage::readText((String("/gmtools/characters/") + fn).c_str(), txt, 600);
            String nm = fn.substring(0, fn.length() - 4), ln;
            for (auto& l : splitList(txt, '\n')) {
                if (l.startsWith("name: ")) nm = l.substring(6);
                if (l.startsWith("class: ")) ln = l.substring(7) + ln;
                if (l.startsWith("level: ")) ln = String("Lv") + l.substring(7) + " " + ln;
                if (l.startsWith("species: ")) ln += " " + l.substring(9);
            }
            files_.push_back(String("/gmtools/characters/") + fn);
            names_.push_back(nm);
            lines_.push_back(ln);
        }
        if (listSel_ > (int)files_.size()) listSel_ = 0;
    }

    void openCharacter(const String& path) {
        if (!c_.load(path)) { sfx::error(); toast("Couldn't load character", C_BLOOD); return; }
        loaded_ = true;
        mode_ = Mode::Sheet;
        tab_ = sel_ = top_ = 0;
        spellInfo_.clear();
        featureNames_.clear();
        sfx::select();
    }

    void startCreate() {
        if (!storage::ok()) { toast("No SD card", C_BLOOD); return; }
        if (!builder_.loadSrd()) { sfx::error(); toast("SRD data missing on SD (/gmtools/srd)", C_BLOOD); return; }
        wiz_.begin("New character", builder_.creationSteps(), ACCENT);
        mode_ = Mode::Create;
    }

    // ------------------------------------------------------------ helpers
    void save() { c_.save(); }

    void rollCheck(const String& title, int mod) { rollui::show(title, "d20" + fmtMod(mod)); }

    void loadSpellInfo() {
        spellInfo_.clear();
        if (!spellTable_.isOpen())
            spellTable_.open({{"/gmtools/srd/spells", {"level", "school", "classes", "ritual", "conc"}}},
                             "/gmtools/custom/spells.txt", {"level"});
        auto addOne = [&](const String& n, bool mi) {
            SpellInfo si;
            si.name = n;
            si.mi = mi;
            int i = spellTable_.find(n);
            db::Rec r;
            if (i >= 0 && spellTable_.read(i, r)) {
                si.found = true;
                si.level = r.num("level");
                si.damage = r.get("damage"); si.dtype = r.get("dtype"); si.heal = r.get("heal");
                si.attack = r.get("attack"); si.save = r.get("save"); si.higher = r.get("higher");
            }
            spellInfo_.push_back(si);
        };
        for (auto& n : c_.spells) addOne(n, false);
        for (auto& n : c_.miSpells) addOne(n, true);
        std::stable_sort(spellInfo_.begin(), spellInfo_.end(), [](const SpellInfo& a, const SpellInfo& b) { return a.level < b.level; });
    }

    void loadFeatures() {
        featureNames_.clear();
        featureOffs_.clear();
        String idx;
        if (!storage::readText("/gmtools/srd/features.idx", idx, 16384)) return;
        for (auto& line : splitList(idx, '\n')) {
            auto f = splitList(line, '\t');
            if (f.size() < 4 || f[2] != c_.cls) continue;
            String sub = f.size() >= 5 ? f[3] : String("");
            int lv = f.back().toInt();
            if (lv > c_.level || (sub.length() && sub != c_.subclass)) continue;
            if (f[1].endsWith("Subclass") || f[1] == "Subclass feature") continue;
            featureNames_.push_back(String(lv) + "|" + f[1]);
            featureOffs_.push_back(f[0].toInt());
        }
    }

    int attunedCount() const { int n = 0; for (auto& it : c_.items) n += it.attuned; return n; }

    // ------------------------------------------------------------ rows per tab
    void buildRows() {
        rows_.clear();
        auto add = [&](RowKind k, int ref, const String& l, const String& r, uint16_t col = C_TEXT) { rows_.push_back({k, ref, l, r, col}); };
        switch (tab_) {
            case 0: {
                String hp = String(c_.hp) + "/" + c_.hpMax + (c_.hpTemp ? String(" +") + c_.hpTemp + " tmp" : String(""));
                add(R_HP, 0, "Hit Points", hp, c_.hp * 2 < c_.hpMax ? C_BLOOD : C_EMERALD);
                String how;
                int ac = c_.ac(&how);
                add(R_AC, 0, "Armor Class  (" + how + ")", String(ac), C_ICE);
                add(R_INIT, 0, "Initiative", fmtMod(c_.initiative()), C_GOLD);
                add(R_NONE, 0, String("Speed ") + c_.speed + " ft  Prof " + fmtMod(c_.prof()), String("PP ") + c_.passivePerception(), C_DIM);
                add(R_HD, 0, "Hit Dice (Enter: spend)", String(c_.level - c_.hitDiceUsed) + "/" + c_.level + " d" + c_.hitDie(), C_TEXT);
                if (c_.hp == 0 || c_.deathOk || c_.deathFail)
                    add(R_DEATH, 0, "Death save", String(c_.deathOk) + " ok / " + c_.deathFail + " fail", C_BLOOD);
                add(R_INSP, 0, "Heroic Inspiration", c_.inspiration ? "Yes" : "No", c_.inspiration ? C_GOLD : C_DIM);
                for (size_t i = 0; i < c_.res.size(); i++)
                    add(R_RES, i, c_.res[i].name + "  (-/= adjust)", String(c_.res[i].cur) + "/" + c_.res[i].max, C_ARCANE);
                for (const char* v : {"Sneak Attack", "Martial Arts", "Rage Damage", "Bardic Die", "Unarmored Movement", "Weapon Mastery", "Eldritch Invocations"}) {
                    String val = c_.ci.cell(c_.level, v);
                    if (val.length()) add(R_VALUE, 0, v, val, C_TEXT);
                }
                add(R_SHORT, 0, "Short rest", "", C_EMERALD);
                add(R_LONG, 0, confirmLong_ ? "Long rest - Enter again" : "Long rest", "", C_EMERALD);
                if (c_.level < 20) add(R_LEVEL, 0, String("Level up to ") + (c_.level + 1), "", C_GOLD);
                break;
            }
            case 1:
                for (int a = 0; a < 6; a++) {
                    String r = fmtMod(c_.mod(a)) + "  save " + fmtMod(c_.saveMod(a)) + (c_.saveProficient(a) ? "*" : " ");
                    add(R_ABIL, a, String(ABIL_NAMES[a]) + " " + c_.score[a], r, c_.saveProficient(a) ? C_GOLD : C_TEXT);
                }
                break;
            case 2:
                for (int i = 0; i < 18; i++) {
                    int lv = c_.skillLevel(i);
                    String l = String(lv == 2 ? "** " : lv == 1 ? "*  " : "   ") + SKILLS[i].name + " (" + ABIL_SHORT[SKILLS[i].ab] + ")";
                    add(R_SKILL, i, l, fmtMod(c_.skillMod(i)), lv ? C_GOLD : C_TEXT);
                }
                break;
            case 3: {
                auto atks = c_.attacks();
                for (size_t i = 0; i < atks.size(); i++) {
                    auto& a = atks[i];
                    String hit = a.hit.startsWith("d20") ? a.hit.substring(3) : a.note;
                    String l = String(a.equipped ? "" : "  ") + a.name;
                    add(R_ATTACK, i, l, hit + "  " + a.damage, a.equipped || a.name == "Unarmed Strike" ? C_TEXT : C_DIM);
                }
                break;
            }
            case 4: {
                if (spellInfo_.size() != c_.spells.size() + c_.miSpells.size()) loadSpellInfo();
                int ab = c_.spellAbil();
                String head = ab >= 0 ? String("DC ") + c_.spellDC() + "  Atk " + fmtMod(c_.spellAttack()) : String("No spellcasting");
                String slots;
                for (int s = 1; s <= 9; s++) {
                    int mx = c_.maxSlots(s);
                    if (!mx) continue;
                    slots += String(s) + ":" + (mx - c_.slotsUsed[s]) + "/" + mx + " ";
                }
                add(R_SLOTS, 0, head, slots, C_ARCANE);
                for (size_t i = 0; i < spellInfo_.size(); i++) {
                    auto& s = spellInfo_[i];
                    String tag = s.level ? String(s.level) : String("C");
                    String r = s.damage.length() ? s.damage + " " + s.dtype.substring(0, 4) : s.heal.length() ? "heal " + s.heal : s.save.length() ? s.save.substring(0, 3) + " save" : "";
                    r.replace("+MOD", "+m");
                    add(R_SPELL, i, "[" + tag + "] " + s.name + (s.mi ? " (MI)" : ""), r, s.found ? (s.level ? C_TEXT : C_ICE) : C_DIM);
                }
                break;
            }
            case 5: {
                const char* cn[] = {"cp", "sp", "ep", "gp", "pp"};
                String coins;
                for (int i = 4; i >= 0; i--) if (c_.coins[i] || i == 3) coins += String(c_.coins[i]) + cn[i] + " ";
                add(R_COINS, 0, "Coins (Enter to change)", coins, C_GOLD);
                for (size_t i = 0; i < c_.items.size(); i++) {
                    auto& it = c_.items[i];
                    String l = String(it.equipped ? "E " : "  ") + (it.attuned ? "A " : "") + it.name;
                    String r = it.qty != 1 ? String("x") + it.qty : String("");
                    if (it.chargesMax) r = String(it.charges) + "/" + it.chargesMax + "ch " + r;
                    add(R_ITEM, i, l, r, it.equipped ? C_TEXT : rgb(190, 184, 200));
                }
                break;
            }
            case 6: {
                if (featureNames_.empty()) loadFeatures();
                for (size_t i = 0; i < featureNames_.size(); i++) {
                    auto p = splitList(featureNames_[i]);
                    add(R_FEATURE, i, p.size() > 1 ? p[1] : featureNames_[i], String("L") + (p.size() ? p[0] : ""), C_TEXT);
                }
                add(R_TRAIT, 0, c_.species + (c_.speciesOpt.length() ? " - " + c_.speciesOpt : String("")) + " traits", "", C_EMERALD);
                for (size_t i = 0; i < c_.feats.size(); i++) add(R_FEAT, i, c_.feats[i], "feat", C_GOLD);
                if (c_.darkvision) add(R_NONE, 0, String("Darkvision ") + c_.darkvision + " ft", "", C_DIM);
                for (auto& t : c_.toolProf) add(R_NONE, 0, "Tool: " + t, "", C_DIM);
                break;
            }
        }
        if (sel_ >= (int)rows_.size()) sel_ = std::max(0, (int)rows_.size() - 1);
    }

    // ------------------------------------------------------------ actions
    void applyHpChange(const String& in) {
        String v = in;
        v.trim();
        if (!v.length()) return;
        bool temp = v[0] == 't' || v[0] == 'T';
        bool heal = v[0] == '+';
        String expr = (temp || heal || v[0] == '-') ? v.substring(1) : v;
        auto r = dice::roll(expr);
        if (!r.ok) { sfx::error(); toast("Use -7, +5, t5 or dice", C_BLOOD); return; }
        int n = std::max(0, r.total);
        if (temp) { c_.hpTemp = std::max(c_.hpTemp, n); toast((String("Temp HP ") + c_.hpTemp).c_str(), C_ICE); }
        else if (heal) {
            c_.hp = std::min(c_.hpMax, c_.hp + n);
            if (c_.hp > 0) c_.deathOk = c_.deathFail = 0;
            toast((String("Healed ") + n).c_str(), C_EMERALD);
            sfx::crit();
        } else {
            int absorb = std::min(c_.hpTemp, n);
            c_.hpTemp -= absorb;
            c_.hp = std::max(0, c_.hp - (n - absorb));
            toast((String("Took ") + n + " damage").c_str(), C_BLOOD);
            sfx::fumble();
        }
        save();
    }

    void applyCoins(const String& in) {
        // "+10 gp", "-3 sp", "25gp"
        String v = in;
        v.trim();
        v.toLowerCase();
        const char* cn[] = {"cp", "sp", "ep", "gp", "pp"};
        int unit = 3;
        for (int i = 0; i < 5; i++) if (v.endsWith(cn[i])) { unit = i; v = v.substring(0, v.length() - 2); }
        v.trim();
        if (!v.length()) return;
        bool rel = v[0] == '+' || v[0] == '-';
        int n = v.toInt();
        c_.coins[unit] = std::max(0, rel ? c_.coins[unit] + n : n);
        save();
    }

    void castSpell(int ref, int slot) {
        auto& s = spellInfo_[ref];
        if (s.level > 0 && !s.mi) {
            if (slot < s.level || c_.slotsUsed[slot] >= c_.maxSlots(slot)) { sfx::error(); toast("No slot of that level left", C_BLOOD); return; }
            c_.slotsUsed[slot]++;
            save();
        }
        int ab = s.mi ? abilIndex(c_.miAbility) : c_.spellAbil();
        int m = ab >= 0 ? c_.mod(ab) : 0;
        std::vector<rollui::Line> lines;
        if (s.attack.length()) lines.push_back({"Spell attack", "d20" + fmtMod(m + c_.prof()), ""});
        String base = s.damage.length() ? s.damage : s.heal;
        if (base.length()) {
            String e = spellmath::castExpr(base, s.higher, s.level, std::max(s.level, slot), c_.level, m);
            lines.push_back({s.damage.length() ? "Damage" : "Healing", e, s.damage.length() ? s.dtype : String("HP")});
        }
        String title = s.name + (s.level && slot > s.level ? String(" @") + slot : String(""));
        if (s.save.length() && lines.size() <= 1) title += String(" DC ") + (8 + m + c_.prof()) + " " + s.save.substring(0, 3);
        if (lines.empty()) {
            toast(s.level > 0 && !s.mi ? (String("Cast ") + s.name + " (slot " + slot + ")").c_str() : (String("Cast ") + s.name).c_str(), C_ARCANE);
            sfx::select();
            return;
        }
        rollui::show(title, lines);
    }

    void openFeatureText(const Row& r) {
        tv_.clear();
        if (r.kind == R_FEATURE) {
            db::Rec rec;
            if (db::readRecordAt("/gmtools/srd/features.txt", featureOffs_[r.ref], rec)) {
                detailTitle_ = rec.name();
                tv_.add(String("Level ") + rec.get("level") + " " + rec.get("class") + (rec.has("subclass") ? " (" + rec.get("subclass") + ")" : String("")), TextView::ACCENT);
                tv_.gap();
                tv_.add(db::unescape(rec.get("text")));
            }
        } else if (r.kind == R_TRAIT) {
            detailTitle_ = c_.species;
            String idx;
            storage::readText("/gmtools/srd/species.idx", idx, 2048);
            for (auto& line : splitList(idx, '\n')) {
                auto f = splitList(line, '\t');
                if (f.size() < 2 || f[1] != c_.species) continue;
                db::Rec rec;
                if (!db::readRecordAt("/gmtools/srd/species.txt", f[0].toInt(), rec)) break;
                if (c_.speciesOpt.length()) tv_.add("Chosen: " + c_.speciesOpt, TextView::ACCENT);
                for (int i = 1; i < 12; i++) {
                    String t = rec.get((String("trait") + i).c_str());
                    if (!t.length()) break;
                    int colon = t.indexOf(':');
                    tv_.gap();
                    tv_.add(t.substring(0, colon), TextView::HEAD);
                    tv_.add(db::unescape(t.substring(colon + 2)));
                }
            }
        } else if (r.kind == R_FEAT) {
            String fname = c_.feats[r.ref];
            detailTitle_ = fname;
            String idx;
            storage::readText("/gmtools/srd/feats.idx", idx, 4096);
            bool found = false;
            for (auto& line : splitList(idx, '\n')) {
                auto f = splitList(line, '\t');
                if (f.size() < 2 || f[1] != fname) continue;
                db::Rec rec;
                if (db::readRecordAt("/gmtools/srd/feats.txt", f[0].toInt(), rec)) {
                    tv_.add(rec.get("category") + " feat" + (rec.has("prereq") ? " (" + rec.get("prereq") + ")" : String("")), TextView::ACCENT);
                    tv_.gap();
                    tv_.add(db::unescape(rec.get("text")));
                    found = true;
                }
            }
            if (!found) tv_.add("No SRD text for this entry.", TextView::DIM);
        }
        mode_ = Mode::Detail;
        sfx::select();
    }

    void openItemMenu(int ref) {
        auto& it = c_.items[ref];
        itemRef_ = ref;
        itemMenu_.clear();
        if (it.isWeapon() || it.heal.length() || it.damage.length() || it.roll.length()) itemMenu_.push_back(it.heal.length() ? "Drink / use (heal)" : "Roll");
        if (it.isWeapon() || it.isArmor() || it.isShield() || it.acbonus) itemMenu_.push_back(it.equipped ? "Unequip" : "Equip");
        if (it.attune.length()) itemMenu_.push_back(it.attuned ? "End attunement" : "Attune");
        if (it.chargesMax) { itemMenu_.push_back("Use a charge"); itemMenu_.push_back("Recharge"); }
        itemMenu_.push_back("Quantity +1");
        itemMenu_.push_back("Quantity -1");
        itemMenu_.push_back("Rename");
        itemMenu_.push_back(confirmRemove_ ? "Remove - Enter again" : "Remove");
        itemMenuSel_ = 0;
        mode_ = Mode::ItemMenu;
    }

    void itemAction(const String& a) {
        auto& it = c_.items[itemRef_];
        if (a == "Roll") {
            if (it.isWeapon()) {
                for (auto& at : c_.attacks()) if (at.name == it.name) {
                    std::vector<rollui::Line> l = {{"Attack", at.hit, ""}, {"Damage", at.damage, at.dtype}};
                    if (at.alt.length()) l.push_back({"Two-handed", at.alt, at.dtype});
                    rollui::show(at.name, l);
                    break;
                }
            } else if (it.damage.length()) rollui::show(it.name, {{"Damage", it.damage, it.dtype}});
            else rollui::show(it.name, {{it.rolllabel.length() ? it.rolllabel : String("Roll"), it.roll, ""}});
        } else if (a.startsWith("Drink")) {
            rollui::show(it.name, {{"Healing", it.heal, "HP"}});
            rollui::lockResult();
            int n = rollui::total(0);
            c_.hp = std::min(c_.hpMax, c_.hp + n);
            if (c_.hp > 0) c_.deathOk = c_.deathFail = 0;
            if (--it.qty <= 0) c_.items.erase(c_.items.begin() + itemRef_);
        } else if (a == "Equip" || a == "Unequip") {
            bool on = a == "Equip";
            if (on && (it.isArmor() || it.isShield()))  // only one armor / one shield
                for (auto& o : c_.items) if (&o != &it && o.kind == it.kind) o.equipped = false;
            it.equipped = on;
        } else if (a == "Attune") {
            if (attunedCount() >= 3) { sfx::error(); toast("Already attuned to 3 items", C_BLOOD); return; }
            it.attuned = true;
        } else if (a == "End attunement") it.attuned = false;
        else if (a == "Use a charge") { if (it.charges > 0) it.charges--; else { sfx::error(); toast("No charges", C_BLOOD); } }
        else if (a == "Recharge") it.charges = it.chargesMax;
        else if (a == "Quantity +1") it.qty++;
        else if (a == "Quantity -1") { if (--it.qty <= 0) c_.items.erase(c_.items.begin() + itemRef_); }
        else if (a == "Rename") { prompt_.open("New name", it.name, 30, 30); mode_ = Mode::Sheet; return; }
        else if (a.startsWith("Remove")) {
            if (!confirmRemove_) { confirmRemove_ = true; openItemMenu(itemRef_); itemMenuSel_ = itemMenu_.size() - 1; return; }
            c_.items.erase(c_.items.begin() + itemRef_);
        }
        confirmRemove_ = false;
        save();
        mode_ = Mode::Sheet;
    }

    void openPicker(bool spells) {
        closePicker();
        spellTable_.close();   // free RAM while the browser is open
        picker_.reset(spells ? newSpellsBrowser() : newItemsBrowser());
        picker_->pickVerb = spells ? "learn" : "add";
        if (spells) picker_->presetFilter(c_.cls);
        picker_->onPick = [this, spells](const db::Rec& r) {
            if (spells) {
                if (listHas(c_.spells, r.name())) { toast("Already known", C_DIM); return; }
                c_.spells.push_back(r.name());
                spellInfo_.clear();
            } else {
                Item it = Item::fromRec(r);
                bool merged = false;
                for (auto& e : c_.items) if (e.name == it.name && !e.chargesMax) { e.qty++; merged = true; break; }
                if (!merged) c_.items.push_back(it);
            }
            save();
            toast((String(spells ? "Learned " : "Added ") + r.name()).c_str(), C_EMERALD);
        };
        picker_->onEnter();
        mode_ = Mode::Picker;
    }

    void closePicker() {
        if (picker_) { picker_->onExit(); picker_.reset(); }
    }

    void rowEnter(const Row& r) {
        switch (r.kind) {
            case R_HP: prompt_.open("Damage, +heal or tTemp (e.g. 7, +5, t4)", "", 1, 12); break;
            case R_INIT: rollCheck("Initiative", c_.initiative()); break;
            case R_HD:
                if (c_.hitDiceUsed >= c_.level) { sfx::error(); toast("No hit dice left", C_BLOOD); break; }
                c_.hitDiceUsed++;
                rollui::show("Hit Die", {{"Healing", String("1d") + c_.hitDie() + fmtMod(c_.mod(CON)), "HP"}});
                rollui::lockResult();
                c_.hp = std::min(c_.hpMax, c_.hp + std::max(0, rollui::total(0)));
                save();
                break;
            case R_DEATH: {
                rollui::show("Death save", "d20");
                rollui::lockResult();
                int t = rollui::total(0);
                if (t == 20) { c_.hp = 1; c_.deathOk = c_.deathFail = 0; toast("Back on your feet!", C_GOLD); }
                else if (t == 1) c_.deathFail += 2;
                else if (t >= 10) c_.deathOk++;
                else c_.deathFail++;
                if (c_.deathOk >= 3) { toast("Stable", C_EMERALD); c_.deathOk = c_.deathFail = 0; }
                if (c_.deathFail >= 3) toast("Dead...", C_BLOOD);
                save();
                break;
            }
            case R_INSP: c_.inspiration = !c_.inspiration; save(); sfx::click(); break;
            case R_RES: {
                auto& res = c_.res[r.ref];
                if (res.cur > 0) { res.cur--; save(); toast((String("Used ") + res.name).c_str(), C_ARCANE); }
                else { sfx::error(); toast("None left", C_BLOOD); }
                break;
            }
            case R_VALUE:
                if (r.right.indexOf('d') >= 0) rollui::show(r.left, r.right.startsWith("d") ? "1" + r.right : r.right);
                break;
            case R_SHORT: c_.shortRest(); save(); toast("Short rest taken", C_EMERALD); sfx::select(); break;
            case R_LONG:
                if (!confirmLong_) { confirmLong_ = true; sfx::click(); break; }
                confirmLong_ = false;
                c_.longRest(); save(); toast("Long rest: all restored", C_EMERALD); sfx::crit();
                break;
            case R_LEVEL:
                if (!builder_.loadSrd()) { toast("SRD data missing", C_BLOOD); break; }
                wiz_.begin(String(c_.name) + ": level up", builder_.levelUpSteps(c_), C_GOLD);
                mode_ = Mode::LevelUp;
                break;
            case R_ABIL: rollCheck(String(ABIL_NAMES[r.ref]) + " check", c_.mod(r.ref)); break;
            case R_SKILL: rollCheck(SKILLS[r.ref].name, c_.skillMod(r.ref)); break;
            case R_ATTACK: {
                auto atks = c_.attacks();
                auto& a = atks[r.ref];
                std::vector<rollui::Line> l;
                if (a.hit.length()) l.push_back({"Attack", a.hit, ""});
                l.push_back({"Damage", a.damage, a.dtype});
                if (a.alt.length()) l.push_back({"Two-handed", a.alt, a.dtype});
                rollui::show(a.name + (a.note.length() && !a.hit.length() ? " " + a.note : String("")), l);
                break;
            }
            case R_SPELL: {
                auto& s = spellInfo_[r.ref];
                if (s.level == 0 || s.mi) { castSpell(r.ref, 0); break; }
                // pick the lowest slot available at or above the spell's level
                castRef_ = r.ref;
                castSlot_ = 0;
                for (int sl = s.level; sl <= 9; sl++) if (c_.maxSlots(sl) - c_.slotsUsed[sl] > 0) { castSlot_ = sl; break; }
                if (!castSlot_) { sfx::error(); toast("No spell slots left", C_BLOOD); castRef_ = -1; }
                break;
            }
            case R_COINS: prompt_.open("Coins: 25 gp, +10 gp, -3 sp", "", 2, 14); break;
            case R_ITEM: confirmRemove_ = false; openItemMenu(r.ref); break;
            case R_FEATURE: case R_TRAIT: case R_FEAT: openFeatureText(r); break;
            default: break;
        }
    }

    void adjust(const Row& r, int d) {
        switch (r.kind) {
            case R_RES: { auto& x = c_.res[r.ref]; x.cur = std::max(0, std::min(x.max, x.cur + d)); break; }
            case R_ITEM: { auto& it = c_.items[r.ref]; it.qty = std::max(0, it.qty + d); break; }
            case R_HD: c_.hitDiceUsed = std::max(0, std::min(c_.level, c_.hitDiceUsed - d)); break;
            case R_ABIL: c_.score[r.ref] = std::max(1, std::min(30, c_.score[r.ref] + d)); break;
            case R_SLOTS: {
                // '-' uses the lowest available slot, '=' restores the highest used one
                if (d < 0) { for (int s = 1; s <= 9; s++) if (c_.maxSlots(s) - c_.slotsUsed[s] > 0) { c_.slotsUsed[s]++; break; } }
                else { for (int s = 9; s >= 1; s--) if (c_.slotsUsed[s] > 0) { c_.slotsUsed[s]--; break; } }
                break;
            }
            case R_HP: c_.hp = std::max(0, std::min(c_.hpMax, c_.hp + d)); break;
            default: return;
        }
        sfx::click();
        save();
    }

    // ------------------------------------------------------------ drawing
    void drawList() {
        drawBackground();
        drawHeader("Characters", ACCENT);
        gfx.setFont(&fonts::Font0);
        int n = files_.size() + 1;
        const int rowH = 13, y0 = HEADER_H + 4;
        int rows = (H - FOOTER_H - y0) / rowH;
        int first = std::max(0, std::min(listSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = y0 + (i - first) * rowH;
            bool s = i == listSel_;
            if (s) drawPanel(4, y - 2, W - 8, rowH, C_PANEL, ACCENT);
            gfx.setTextDatum(top_left);
            if (i == 0) { gfx.setTextColor(C_GOLD); gfx.drawString("+ New character", 12, y + 1); continue; }
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            gfx.drawString(names_[i - 1], 12, y + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(lines_[i - 1], W - 10, y + 1);
        }
        drawFooter(storage::ok() ? "Ent open  FnX delete  Esc back" : "Needs an SD card");
    }

    void drawSheet() {
        drawBackground();
        // header: name + class line + HP bar
        gfx.fillRect(0, 0, W, HEADER_H, C_BG2);
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(ACCENT);
        String nm = c_.name;
        while (nm.length() > 2 && gfx.textWidth(nm) > 110) nm = nm.substring(0, nm.length() - 1);
        gfx.drawString(nm, 5, HEADER_H / 2);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String("Lv") + c_.level + " " + c_.cls.substring(0, 9), W - 4, 5);
        drawBar(W - 64, 10, 60, 3, c_.hpMax ? (float)c_.hp / c_.hpMax : 0, c_.hp * 2 < c_.hpMax ? C_BLOOD : C_EMERALD, C_BG);
        // tabs
        int ty = HEADER_H;
        gfx.fillRect(0, ty, W, 11, C_BG);
        int tw = W / NTABS;
        for (int i = 0; i < NTABS; i++) {
            bool on = i == tab_;
            if (on) gfx.fillRect(i * tw, ty, tw, 11, C_PANEL);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(on ? ACCENT : C_DIM);
            gfx.drawString(TABS[i], i * tw + tw / 2, ty + 2);
        }
        gfx.drawFastHLine(0, ty + 11, W, C_EDGE);

        buildRows();
        const int rowH = 11, y0 = ty + 13;
        int rows = (H - FOOTER_H - y0) / rowH;
        if (sel_ < top_) top_ = sel_;
        if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
        for (int i = top_; i < (int)rows_.size() && i < top_ + rows; i++) {
            const Row& r = rows_[i];
            int y = y0 + (i - top_) * rowH;
            bool s = i == sel_;
            if (s) gfx.fillRoundRect(2, y - 1, W - 4, rowH, 2, C_PANEL);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : r.color);
            String l = r.left;
            gfx.setTextDatum(top_right);
            int rw = gfx.textWidth(r.right);
            gfx.setTextDatum(top_left);
            while (l.length() > 2 && gfx.textWidth(l) > W - 16 - rw) l = l.substring(0, l.length() - 1);
            gfx.drawString(l, 6, y + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(r.color);
            gfx.drawString(r.right, W - 6, y + 1);
        }
        if ((int)rows_.size() > rows) {
            int sh = std::max(5, rows * rowH * rows / (int)rows_.size());
            int sy = y0 + (rows * rowH - sh) * top_ / std::max(1, (int)rows_.size() - rows);
            gfx.fillRect(W - 2, sy, 2, sh, ACCENT);
        }
        const char* foot[] = {"</> tab  Ent use  -/= adjust  Esc",
                              "Ent check  S save  -/= score  Esc",
                              "Ent roll  P prof  E expertise",
                              "Ent attack+damage  </> tab",
                              "Ent cast  FnA learn  XX forget  -/= slots",
                              "Ent actions  FnA add item  -/= qty",
                              "Ent read  </> tab  Esc back"};
        drawFooter(foot[tab_]);
        if (castRef_ >= 0) drawCast();
        const char* hint = nullptr;
        if (prompt_.tag() == 1) hint = "Blank = cancel. Dice work: 2d6";
        prompt_.draw(hint);
    }

    void drawCast() {
        auto& s = spellInfo_[castRef_];
        const int w = 190, h = 46, x = (W - w) / 2, y = 44;
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_ARCANE);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_ARCANE);
        gfx.drawString(String("Cast ") + s.name, x + 6, y + 5);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(String("< slot level ") + castSlot_ + " >  (" + (c_.maxSlots(castSlot_) - c_.slotsUsed[castSlot_]) + " left)", x + 6, y + 18);
        gfx.setTextColor(C_DIM);
        gfx.drawString("Ent cast  Esc cancel", x + 6, y + 32);
    }

    void drawItemMenu() {
        drawSheet();
        const int w = 150, h = 14 + itemMenu_.size() * 11, x = (W - w) / 2, y = std::max(18, (H - h) / 2);
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_GOLD);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        String nm = c_.items[itemRef_].name;
        if (nm.length() > 24) nm = nm.substring(0, 23) + ".";
        gfx.drawString(nm, x + 6, y + 3);
        for (size_t i = 0; i < itemMenu_.size(); i++) {
            int ry = y + 13 + i * 11;
            bool s = (int)i == itemMenuSel_;
            if (s) gfx.fillRect(x + 3, ry - 1, w - 6, 10, C_PANEL);
            gfx.setTextColor(itemMenu_[i].startsWith("Remove") ? rgb(230, 140, 130) : s ? C_TEXT : rgb(200, 192, 180));
            gfx.drawString(itemMenu_[i], x + 9, ry);
        }
    }
};

bool CharactersApp::handle(const input::Event& e) {
    switch (mode_) {
        case Mode::Create:
            wiz_.handle(e);
            if (wiz_.finished()) {
                drawBackground();
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(C_GOLD);
                gfx.drawString("Creating character...", W / 2, H / 2);
                gfx.pushSprite(0, 0);
                Character nc;
                bool ok = builder_.applyCreation(wiz_, nc);
                builder_.unload();
                if (!ok) { sfx::error(); toast("Couldn't create (SD?)", C_BLOOD); mode_ = Mode::List; refreshList(); return true; }
                refreshList();
                openCharacter(nc.path);
                toast((nc.name + " is ready!").c_str(), C_GOLD);
                sfx::crit();
            } else if (wiz_.cancelled()) { builder_.unload(); mode_ = Mode::List; }
            return true;
        case Mode::LevelUp:
            wiz_.handle(e);
            if (wiz_.finished()) {
                String sum = builder_.applyLevelUp(wiz_, c_);
                builder_.unload();
                spellInfo_.clear();
                featureNames_.clear();
                mode_ = Mode::Sheet;
                toast(sum.c_str(), C_GOLD);
                sfx::crit();
            } else if (wiz_.cancelled()) { builder_.unload(); mode_ = Mode::Sheet; }
            return true;
        case Mode::Detail:
            if (e.key == input::K_BACK) { mode_ = Mode::Sheet; sfx::back(); }
            else if (e.key == input::K_UP) tv_.scroll(-1);
            else if (e.key == input::K_DOWN) tv_.scroll(1);
            else if (e.key == input::K_CHAR && e.base == ' ') tv_.page(1);
            return true;
        case Mode::Picker:
            if (!picker_->handle(e)) { closePicker(); mode_ = Mode::Sheet; spellInfo_.clear(); }
            return true;
        case Mode::ItemMenu: {
            int n = itemMenu_.size();
            if (e.key == input::K_BACK) { mode_ = Mode::Sheet; confirmRemove_ = false; sfx::back(); }
            else if (e.key == input::K_UP) { itemMenuSel_ = (itemMenuSel_ + n - 1) % n; sfx::click(); }
            else if (e.key == input::K_DOWN) { itemMenuSel_ = (itemMenuSel_ + 1) % n; sfx::click(); }
            else if (e.key == input::K_ENTER) itemAction(itemMenu_[itemMenuSel_]);
            return true;
        }
        case Mode::List: {
            int n = files_.size() + 1;
            if (!(e.key == input::K_CHAR && e.fn && e.base == 'x')) confirmDel_ = false;
            switch (e.key) {
                case input::K_BACK: return false;
                case input::K_UP: listSel_ = (listSel_ + n - 1) % n; sfx::click(); break;
                case input::K_DOWN: listSel_ = (listSel_ + 1) % n; sfx::click(); break;
                case input::K_ENTER:
                    if (listSel_ == 0) startCreate();
                    else openCharacter(files_[listSel_ - 1]);
                    break;
                case input::K_CHAR:
                    if (e.fn && e.base == 'x' && listSel_ > 0) {
                        if (!confirmDel_) { confirmDel_ = true; toast("Fn+X again to delete", C_BLOOD); break; }
                        storage::remove(files_[listSel_ - 1].c_str());
                        confirmDel_ = false;
                        refreshList();
                        toast("Character deleted", C_DIM);
                    }
                    break;
                default: break;
            }
            return true;
        }
        case Mode::Sheet: break;
    }

    // ---- sheet
    if (prompt_.active()) {
        prompt_.handle(e);
        if (prompt_.done()) {
            if (prompt_.tag() == 1) applyHpChange(prompt_.value());
            else if (prompt_.tag() == 2) applyCoins(prompt_.value());
            else if (prompt_.tag() == 30 && prompt_.value().length()) { c_.items[itemRef_].name = prompt_.value(); save(); }
        }
        return true;
    }
    if (castRef_ >= 0) {
        auto& s = spellInfo_[castRef_];
        if (e.key == input::K_BACK) castRef_ = -1;
        else if (e.key == input::K_LEFT || e.key == input::K_RIGHT) {
            int d = e.key == input::K_RIGHT ? 1 : -1;
            for (int sl = castSlot_ + d; sl >= s.level && sl <= 9; sl += d)
                if (c_.maxSlots(sl) - c_.slotsUsed[sl] > 0) { castSlot_ = sl; sfx::click(); break; }
        } else if (e.key == input::K_ENTER) { int r = castRef_; castRef_ = -1; castSpell(r, castSlot_); }
        return true;
    }
    buildRows();
    int n = rows_.size();
    const Row* r = n ? &rows_[sel_] : nullptr;
    if (!(r && r->kind == R_LONG && e.key == input::K_ENTER)) confirmLong_ = false;
    switch (e.key) {
        case input::K_BACK:
            save();
            loaded_ = false;
            spellTable_.close();
            mode_ = Mode::List;
            refreshList();
            sfx::back();
            return true;
        case input::K_LEFT: tab_ = (tab_ + NTABS - 1) % NTABS; sel_ = top_ = 0; sfx::click(); return true;
        case input::K_RIGHT: case input::K_TAB: tab_ = (tab_ + 1) % NTABS; sel_ = top_ = 0; sfx::click(); return true;
        case input::K_UP: if (n) { sel_ = (sel_ + n - 1) % n; sfx::click(); } return true;
        case input::K_DOWN: if (n) { sel_ = (sel_ + 1) % n; sfx::click(); } return true;
        case input::K_ENTER: if (r) rowEnter(*r); return true;
        case input::K_CHAR: break;
        default: return true;
    }
    if (e.fn && e.base == 'a') {
        if (tab_ == 4) openPicker(true);
        else if (tab_ == 5) openPicker(false);
        return true;
    }
    if (!r) return true;
    if (e.base == '-') { adjust(*r, -1); return true; }
    if (e.base == '=') { adjust(*r, 1); return true; }
    if (r->kind == R_ABIL && e.base == 's') { rollCheck(String(ABIL_NAMES[r->ref]) + " save", c_.saveMod(r->ref)); return true; }
    if (r->kind == R_SKILL && (e.base == 'p' || e.base == 'e')) {
        String sk = SKILLS[r->ref].name;
        auto& v = e.base == 'p' ? c_.skillProf : c_.expertise;
        bool had = listHas(v, sk);
        v.erase(std::remove_if(v.begin(), v.end(), [&](const String& x) { return x.equalsIgnoreCase(sk); }), v.end());
        if (!had) v.push_back(sk);
        if (e.base == 'e' && !had && !listHas(c_.skillProf, sk)) c_.skillProf.push_back(sk);
        save();
        sfx::click();
        return true;
    }
    if (r->kind == R_SPELL && e.base == 'x') {
        if (!confirmRemove_) { confirmRemove_ = true; toast("X again to forget this spell", C_BLOOD); return true; }
        confirmRemove_ = false;
        auto& s = spellInfo_[r->ref];
        auto& v = s.mi ? c_.miSpells : c_.spells;
        v.erase(std::remove(v.begin(), v.end(), s.name), v.end());
        spellInfo_.clear();
        save();
        return true;
    }
    confirmRemove_ = false;
    return true;
}

void CharactersApp::draw() {
    switch (mode_) {
        case Mode::List: drawList(); break;
        case Mode::Create: case Mode::LevelUp: wiz_.draw(); break;
        case Mode::Sheet: drawSheet(); break;
        case Mode::ItemMenu: drawItemMenu(); break;
        case Mode::Picker: picker_->draw(); break;
        case Mode::Detail:
            drawBackground();
            drawHeader(detailTitle_.c_str(), ACCENT);
            tv_.draw(6, HEADER_H + 3, H - FOOTER_H - HEADER_H - 5, ACCENT);
            drawFooter("Up/Dn scroll  Space page  Esc back");
            break;
    }
}

}  // namespace

App* makeCharactersApp() { return new CharactersApp(); }

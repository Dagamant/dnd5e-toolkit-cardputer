#include "../board/board.h"
#include "../core/app.h"
#include "../core/character.h"
#include "../core/ddb_import.h"
#include "../core/dice_expr.h"
#include "../core/hold_menu.h"
#include "../core/prompt.h"
#include "../core/roll_overlay.h"
#include "../core/sfx.h"
#include "../core/spellmath.h"
#include "../core/storage.h"
#include "../core/textview.h"
#include "../core/ui.h"
#include "../core/wizard.h"
#include "../core/touch.h"
#include "../core/web.h"
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
// the big screen's sheet pages hold the tabs in this order (Left/Right walks it)
constexpr int PAGE_ORDER[NTABS] = {1, 2, 0, 3, 4, 5, 6};
const uint16_t ACCENT = rgb(230, 120, 90);

struct SpellInfo {
    String name;
    int level = 0;
    uint8_t src = 0;            // 0 the class, 1 a feat (Magic Initiate), 2 the species
    String from;                // for a granted spell: "feat", "Drow"...
    int freeMax = 0;            // free casts per Long Rest (granted levelled spells)
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
    bool bigLayout() const override { return true; }
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
    std::vector<String> files_, names_, lines_, ddb_;
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
    bool featuresLoaded_ = false;
    int itemMenuSel_ = 0, itemRef_ = 0;
    std::vector<String> itemMenu_;
    bool confirmRemove_ = false, confirmLong_ = false;
    int castSlot_ = 0, castRef_ = -1;   // castSlot_ 0: a free cast
    int usesRef_ = -1, usesSel_ = 0;    // big screen: the resource whose actions are open
    std::vector<Use> uses_;
    bool usesArm_ = false;              // "Remove counter" tapped once
    int abilPick_ = -1;   // big screen: the ability whose check-or-save choice is open

    // ------------------------------------------------------------ list
    void refreshList() {
        files_.clear(); names_.clear(); lines_.clear(); ddb_.clear();
        std::vector<String> f;
        storage::listFiles("/gmtools/characters", f);
        for (auto& fn : f) {
            String txt;
            storage::readText((String("/gmtools/characters/") + fn).c_str(), txt, 600);
            String nm = fn.substring(0, fn.length() - 4), ln, link;
            for (auto& l : splitList(txt, '\n')) {
                if (l.startsWith("name: ")) nm = l.substring(6);
                if (l.startsWith("ddb: ")) link = l.substring(5);
                if (l.startsWith("class: ")) ln = l.substring(7) + ln;
                if (l.startsWith("level: ")) ln = String("Lv") + l.substring(7) + " " + ln;
                if (l.startsWith("species: ")) ln += " " + l.substring(9);
            }
            files_.push_back(String("/gmtools/characters/") + fn);
            names_.push_back(nm);
            lines_.push_back(ln);
            ddb_.push_back(link);
        }
        if (listSel_ >= (int)files_.size() + head()) listSel_ = 0;
    }

    void openCharacter(const String& path) {
        if (!c_.load(path)) { sfx::error(); toast("Couldn't load character", C_BLOOD); return; }
        loaded_ = true;
        mode_ = Mode::Sheet;
        tab_ = wide() ? 1 : 0;   // the big screen opens on the Abilities page
        sel_ = top_ = 0;
        abilPick_ = castRef_ = -1;
        spellInfo_.clear();
        featuresLoaded_ = false;
        sfx::select();
    }

    void startCreate() {
        if (!storage::ok()) { toast("No SD card", C_BLOOD); return; }
        if (!builder_.loadSrd()) { sfx::error(); toast("SRD data missing on SD (/gmtools/srd)", C_BLOOD); return; }
        wiz_.begin("New character", builder_.creationSteps(), ACCENT);
        mode_ = Mode::Create;
    }

    // Rows above the characters: "+ New character", and on the Tab5 the import.
#ifdef BOARD_TAB5
    static int head() { return 2; }
#else
    static int head() { return 1; }
#endif

#ifdef BOARD_TAB5
    // Import (or update) from a D&D Beyond share link; the download takes a moment.
    bool importFromDdb(const String& link, String& path, String& msg) {
        drawBackground();
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_GOLD);
        gfx.drawString("Downloading from D&D Beyond...", W / 2, H / 2);
        board::present(gfx);
        const bool ok = ddb::import(link, path, msg);
        if (ok && loaded_ && c_.path == path) {   // it's open here: show the new sheet
            c_.load(path);
            spellInfo_.clear();
            featuresLoaded_ = false;
            castRef_ = -1;
            if (mode_ == Mode::ItemMenu) mode_ = Mode::Sheet;
            if (prompt_.active() && prompt_.tag() == 30) prompt_.cancel();
        }
        refreshList();
        for (size_t i = 0; ok && i < files_.size(); i++) if (files_[i] == path) listSel_ = i + head();
        return ok;
    }
    void runImport(const String& link) {
        String path, msg;
        const bool ok = importFromDdb(link, path, msg);
        if (ok) sfx::select(); else sfx::error();
        toast(msg.c_str(), ok ? C_EMERALD : C_BLOOD);
    }
#endif

    // ------------------------------------------------------------ helpers
    void save() { c_.save(); }

    void rollCheck(const String& title, int mod) { rollui::show(title, "d20" + fmtMod(mod)); }

    void loadSpellInfo() {
        spellInfo_.clear();
        if (!spellTable_.isOpen())
            spellTable_.open({{"/gmtools/srd/spells", {"level", "school", "classes", "ritual", "conc"}}},
                             "/gmtools/custom/spells.txt", {"level"});
        auto addOne = [&](const String& n, uint8_t src, const String& from, int freeMax) {
            SpellInfo si;
            si.name = n;
            si.src = src;
            si.from = from;
            int i = spellTable_.find(n);
            db::Rec r;
            if (i >= 0 && spellTable_.read(i, r)) {
                si.found = true;
                si.level = r.num("level");
                si.damage = r.get("damage"); si.dtype = r.get("dtype"); si.heal = r.get("heal");
                si.attack = r.get("attack"); si.save = r.get("save"); si.higher = r.get("higher");
            }
            si.freeMax = si.level > 0 ? freeMax : 0;
            spellInfo_.push_back(si);
        };
        for (auto& n : c_.spells) addOne(n, 0, "", 0);
        for (auto& n : c_.miSpells) addOne(n, 1, "feat", 1);
        for (auto& g : c_.speciesSpells()) addOne(g.name, 2, g.source, g.freeMax);
        std::stable_sort(spellInfo_.begin(), spellInfo_.end(), [](const SpellInfo& a, const SpellInfo& b) { return a.level < b.level; });
    }

    size_t spellCount() const { return c_.spells.size() + c_.miSpells.size() + c_.speciesSpells().size(); }

    int spellAbilFor(const SpellInfo& s) const {
        const String& a = s.src == 1 ? c_.miAbility : s.src == 2 ? c_.spAbility : String();
        return a.length() ? abilIndex(a) : c_.spellAbil();
    }

    int freeLeft(const SpellInfo& s) const { return s.src && s.level ? c_.freeLeft(s.name, s.freeMax) : 0; }

    // "[1] Faerie Fire (Drow, free 1/1)"
    String spellLabel(const SpellInfo& s) const {
        String l = "[" + (s.level ? String(s.level) : String("C")) + "] " + s.name;
        if (!s.src) return l;
        l += " (" + s.from;
        if (s.freeMax) l += String(", free ") + freeLeft(s) + "/" + s.freeMax;
        return l + ")";
    }

    // Pays for a cast: nothing for a cantrip, a free cast of a granted spell
    // (slot 0), else a slot of level `slot`. False (and why) if it can't be cast.
    bool payCast(const SpellInfo& s, int slot, bool& free, String& why) {
        free = false;
        if (!s.level) return true;
        if (slot == 0) {
            if (s.src && freeLeft(s) > 0) { c_.freeUsed.push_back(s.name); free = true; return true; }
            why = "Free cast used - it's back after a Long Rest";
            return false;
        }
        if (slot < s.level || slot > 9 || c_.slotsUsed[slot] >= c_.maxSlots(slot)) {
            why = "No slot of that level left";
            return false;
        }
        c_.slotsUsed[slot]++;
        return true;
    }

    // Ways to cast a levelled spell now: 0 (its free cast), then each slot level with one left.
    std::vector<int> castOptions(const SpellInfo& s) const {
        std::vector<int> v;
        if (s.src && freeLeft(s) > 0) v.push_back(0);
        for (int l = std::max(1, s.level); l <= 9; l++) if (c_.maxSlots(l) - c_.slotsUsed[l] > 0) v.push_back(l);
        return v;
    }

    // The upcast rule from a spell's "higher" text, without its heading.
    static String higherText(const SpellInfo& s) {
        String h = s.higher;
        const int dot = h.indexOf(". ");
        if (h.startsWith("Using a Higher-Level Spell Slot") && dot > 0) h = h.substring(dot + 2);
        return h;
    }

    void loadFeatures() {
        featuresLoaded_ = true;
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

    // Left/Right walks the tabs; on the big screen it walks the pages' lists
    // in page order instead.
    int nextTab(int d) const {
        if (!wide()) return (tab_ + NTABS + d) % NTABS;
        int i = 0;
        while (PAGE_ORDER[i] != tab_) i++;
        return PAGE_ORDER[(i + NTABS + d) % NTABS];
    }

    int attunedCount() const { int n = 0; for (auto& it : c_.items) n += it.attuned; return n; }

    // ------------------------------------------------------------ web page
    // The page's character sheet. It works on the character the device has
    // open when it's the same one, else on a copy loaded just for the call
    // (the device's open sheet is put back afterwards).
    bool withChar(const String& file, const std::function<void()>& fn) {
        if (!file.length() || file.indexOf('/') >= 0 || file.indexOf("..") >= 0) return false;
        const String path = String("/gmtools/characters/") + file;
        if (loaded_ && c_.path == path) { fn(); return true; }
        Character keep = std::move(c_);
        auto keepSpells = std::move(spellInfo_);
        auto keepNames = std::move(featureNames_);
        auto keepOffs = std::move(featureOffs_);
        const bool keepFeat = featuresLoaded_;
        c_ = Character();
        spellInfo_.clear();
        featuresLoaded_ = false;
        const bool ok = c_.load(path);
        if (ok) fn();
        c_ = std::move(keep);
        spellInfo_ = std::move(keepSpells);
        featureNames_ = std::move(keepNames);
        featureOffs_ = std::move(keepOffs);
        featuresLoaded_ = keepFeat;
        return ok;
    }

    String sheetJson() {
        auto js = [](const String& v) { return web::js(v); };
        auto b = [](bool v) { return v ? "true" : "false"; };
        String how;
        const int ac = c_.ac(&how);
        String o = String("{\"name\":") + js(c_.name) + ",\"ddb\":" + js(c_.ddb) + ",\"line\":" + js(c_.levelLine()) + ",\"species\":" + js(c_.species) +
                   ",\"speciesOpt\":" + js(c_.speciesOpt) + ",\"background\":" + js(c_.background) + ",\"alignment\":" + js(c_.alignment) +
                   ",\"cls\":" + js(c_.cls) + ",\"level\":" + c_.level + ",\"hp\":" + c_.hp + ",\"hpMax\":" + c_.hpMax +
                   ",\"hpTemp\":" + c_.hpTemp + ",\"hitDie\":" + c_.hitDie() + ",\"hdLeft\":" + (c_.level - c_.hitDiceUsed) +
                   ",\"deathOk\":" + c_.deathOk + ",\"deathFail\":" + c_.deathFail + ",\"insp\":" + b(c_.inspiration) +
                   ",\"ac\":" + ac + ",\"acHow\":" + js(how) + ",\"init\":" + c_.initiative() + ",\"speed\":" + c_.speed +
                   ",\"prof\":" + c_.prof() + ",\"passive\":" + c_.passivePerception() + ",\"darkvision\":" + c_.darkvision +
                   ",\"size\":" + js(c_.size) + ",\"notes\":" + js(c_.notes) + ",\"abil\":[";
        for (int a = 0; a < 6; a++)
            o += String(a ? "," : "") + "{\"name\":" + js(ABIL_NAMES[a]) + ",\"short\":" + js(ABIL_SHORT[a]) + ",\"score\":" + c_.score[a] +
                 ",\"mod\":" + c_.mod(a) + ",\"save\":" + c_.saveMod(a) + ",\"saveProf\":" + b(c_.saveProficient(a)) + "}";
        o += "],\"skills\":[";
        for (int i = 0; i < 18; i++)
            o += String(i ? "," : "") + "{\"name\":" + js(SKILLS[i].name) + ",\"ab\":" + js(ABIL_SHORT[SKILLS[i].ab]) +
                 ",\"mod\":" + c_.skillMod(i) + ",\"lv\":" + c_.skillLevel(i) + "}";
        o += "],\"attacks\":[";
        auto atks = c_.attacks();
        for (size_t i = 0; i < atks.size(); i++) {
            auto& a = atks[i];
            o += String(i ? "," : "") + "{\"name\":" + js(a.name) + ",\"hit\":" + js(a.hit) + ",\"damage\":" + js(a.damage) +
                 ",\"dtype\":" + js(a.dtype) + ",\"alt\":" + js(a.alt) + ",\"note\":" + js(a.note) + ",\"equipped\":" + b(a.equipped) + "}";
        }
        o += "],\"res\":[";
        for (size_t i = 0; i < c_.res.size(); i++) {
            const Resource& r = c_.res[i];
            o += String(i ? "," : "") + "{\"name\":" + js(r.name) + ",\"cur\":" + r.cur + ",\"max\":" + r.max + ",\"from\":" + js(r.from) +
                 ",\"rest\":\"" + r.rest + "\",\"custom\":" + b(r.custom) + ",\"uses\":[";
            const auto us = c_.uses(r);
            for (size_t k = 0; k < us.size(); k++) {
                o += String(k ? "," : "") + "{\"name\":" + js(us[k].name) + ",\"cost\":" + us[k].cost + ",\"note\":" + js(c_.expand(us[k].note)) + ",\"levels\":[";
                const auto lv = c_.useLevels(us[k]);
                for (size_t j = 0; j < lv.size(); j++) o += String(j ? "," : "") + lv[j];
                o += "]}";
            }
            o += "]}";
        }
        o += "],\"values\":[";
        bool first = true;
        for (const char* v : {"Sneak Attack", "Martial Arts", "Rage Damage", "Bardic Die", "Unarmored Movement", "Weapon Mastery", "Eldritch Invocations"}) {
            const String val = c_.ci.cell(c_.level, v);
            if (!val.length()) continue;
            o += String(first ? "" : ",") + "{\"name\":" + js(v) + ",\"value\":" + js(val) + "}";
            first = false;
        }
        const int ab = c_.spellAbil();
        o += String("],\"caster\":") + b(ab >= 0) + ",\"spellDC\":" + (ab >= 0 ? c_.spellDC() : 0) +
             ",\"spellAtk\":" + (ab >= 0 ? c_.spellAttack() : 0) + ",\"slots\":[";
        first = true;
        for (int sl = 1; sl <= 9; sl++) {
            const int mx = c_.maxSlots(sl);
            if (!mx) continue;
            o += String(first ? "" : ",") + "{\"level\":" + sl + ",\"max\":" + mx + ",\"left\":" + (mx - c_.slotsUsed[sl]) + "}";
            first = false;
        }
        o += "],\"spells\":[";
        if (spellInfo_.size() != spellCount()) loadSpellInfo();
        for (size_t i = 0; i < spellInfo_.size(); i++) {
            auto& sp = spellInfo_[i];
            o += String(i ? "," : "") + "{\"name\":" + js(sp.name) + ",\"level\":" + sp.level + ",\"from\":" + js(sp.from) +
                 ",\"free\":" + freeLeft(sp) + ",\"freeMax\":" + sp.freeMax + ",\"higher\":" + js(higherText(sp)) +
                 ",\"damage\":" + js(sp.damage) + ",\"dtype\":" + js(sp.dtype) + ",\"heal\":" + js(sp.heal) +
                 ",\"attack\":" + b(sp.attack.length()) + ",\"save\":" + js(sp.save) + ",\"found\":" + b(sp.found) + "}";
        }
        o += "],\"coins\":[";
        for (int i = 0; i < 5; i++) o += String(i ? "," : "") + c_.coins[i];
        o += "],\"items\":[";
        for (size_t i = 0; i < c_.items.size(); i++) {
            auto& it = c_.items[i];
            const bool rolls = it.isWeapon() || it.damage.length() || it.roll.length();
            o += String(i ? "," : "") + "{\"name\":" + js(it.name) + ",\"qty\":" + it.qty + ",\"equipped\":" + b(it.equipped) +
                 ",\"attuned\":" + b(it.attuned) + ",\"kind\":" + js(it.kind) + ",\"charges\":" + it.charges +
                 ",\"chargesMax\":" + it.chargesMax + ",\"heal\":" + js(it.heal) + ",\"rolls\":" + b(rolls) +
                 ",\"attune\":" + js(it.attune) + ",\"rarity\":" + js(it.rarity) + "}";
        }
        o += "],\"features\":[";
        if (!featuresLoaded_) loadFeatures();
        for (size_t i = 0; i < featureNames_.size(); i++) {
            auto p = splitList(featureNames_[i]);
            o += String(i ? "," : "") + "{\"name\":" + js(p.size() > 1 ? p[1] : featureNames_[i]) + ",\"level\":" +
                 (p.size() ? p[0].toInt() : 0) + ",\"off\":" + featureOffs_[i] + "}";
        }
        o += "],\"feats\":[";
        for (size_t i = 0; i < c_.feats.size(); i++) o += String(i ? "," : "") + js(c_.feats[i]);
        o += "],\"tools\":[";
        for (size_t i = 0; i < c_.toolProf.size(); i++) o += String(i ? "," : "") + js(c_.toolProf[i]);
        return o + "]}";
    }

    // A roll made for the page, logged like the device's own.
    dice::Result webRoll(const String& label, const String& expr) {
        dice::Result r = dice::roll(expr);
        if (r.ok) web::logRoll(c_.name, label, r.expr, r.total, r.breakdown(), false, r.natural);
        return r;
    }

    // One change from the page. Fills `msg` with what happened (or why not).
    bool webAct(const WebCall& c, String& msg, String& extra) {
        const String what = c.arg("what");
        if (what == "hp") applyHpChange(c.arg("v"));
        else if (what == "insp") c_.inspiration = !c_.inspiration;
        else if (what == "hd") {
            if (c_.hitDiceUsed >= c_.level) { msg = "No hit dice left"; return false; }
            c_.hitDiceUsed++;
            const dice::Result r = webRoll("Hit Die", String("1d") + c_.hitDie() + fmtMod(c_.mod(CON)));
            const int n = std::max(0, r.total);
            c_.hp = std::min(c_.hpMax, c_.hp + n);
            msg = String("Hit die: healed ") + n;
        } else if (what == "death") {
            const dice::Result r = webRoll("Death save", "d20");
            const int t = r.total;
            if (t == 20) { c_.hp = 1; c_.deathOk = c_.deathFail = 0; msg = "20! Back on your feet"; }
            else {
                if (t == 1) c_.deathFail += 2;
                else if (t >= 10) c_.deathOk++;
                else c_.deathFail++;
                c_.deathFail = std::min(3, c_.deathFail);
                msg = String("Death save: ") + t;
                if (c_.deathOk >= 3) { c_.deathOk = c_.deathFail = 0; msg += " - stable"; }
                if (c_.deathFail >= 3) msg += " - dead...";
            }
        } else if (what == "res") {
            const int i = c.num("i", -1);
            if (i < 0 || i >= (int)c_.res.size()) { msg = "No such resource"; return false; }
            auto& x = c_.res[i];
            x.cur = std::max(0, std::min(x.max, x.cur + c.num("d")));
        } else if (what == "use") {
            const int i = c.num("i", -1);
            if (i < 0 || i >= (int)c_.res.size() || c_.res[i].name != c.arg("name")) { msg = "That counter changed - reload"; return false; }
            const auto us = c_.uses(c_.res[i]);
            const int k = c.num("u", -1);
            if (k < 0 || k >= (int)us.size()) { msg = "No such action"; return false; }
            const Use& u = us[k];
            if (!c_.spendUse(i, u, c.num("level"), msg)) return false;
            const String roll = c_.expand(u.roll), note = c_.expand(u.note);
            if (roll.length() && (u.effect == U_HEAL || u.effect == U_TEMP)) {
                // rolled here: the result changes the sheet
                const dice::Result r = webRoll(u.name, roll);
                c_.applyUseRoll(u, r.total);
                msg = u.name + ": " + r.total + (u.effect == U_HEAL ? " HP healed" : " temporary HP");
            } else if (roll.length()) {
                extra = "[{\"label\":" + web::js(u.label.length() ? u.label : u.name) + ",\"expr\":" + web::js(roll) + "}]";
                msg = u.name + (note.length() ? " - " + note : String(""));
            } else msg = useMessage(u, msg, note);
        } else if (what == "addres") {
            String why;
            if (!addCounter(c.arg("name") + "," + c.arg("max") + "," + c.arg("rest"), why)) { msg = why.length() ? why : String("Give it a name"); return false; }
            msg = String("Added ") + c.arg("name");
        } else if (what == "delres") {
            const int i = c.num("i", -1);
            if (i < 0 || i >= (int)c_.res.size() || c_.res[i].name != c.arg("name") || !c_.res[i].custom) { msg = "Only counters you added can be removed"; return false; }
            removeCounter(i);
            msg = "Counter removed";
        } else if (what == "slot") {
            const int sl = c.num("level");
            if (sl < 1 || sl > 9 || !c_.maxSlots(sl)) { msg = "No such slot"; return false; }
            c_.slotsUsed[sl] = std::max(0, std::min(c_.maxSlots(sl), c_.slotsUsed[sl] - c.num("d")));
        } else if (what == "coins") applyCoins(c.arg("v"));
        else if (what == "short") { c_.shortRest(); msg = "Short rest taken"; }
        else if (what == "long") { c_.longRest(); msg = "Long rest taken"; }
        else if (what == "notes") c_.notes = c.body;
        else if (what == "item") {
            const int i = c.num("i", -1);
            if (i < 0 || i >= (int)c_.items.size() || c_.items[i].name != c.arg("name")) { msg = "That item changed - reload"; return false; }
            auto& it = c_.items[i];
            const String a = c.arg("a");
            if (a == "equip") {
                if (it.isArmor() || it.isShield())
                    for (auto& o : c_.items) if (&o != &it && o.kind == it.kind) o.equipped = false;
                it.equipped = true;
            } else if (a == "unequip") it.equipped = false;
            else if (a == "attune") {
                if (attunedCount() >= 3) { msg = "Already attuned to 3 items"; return false; }
                it.attuned = true;
            } else if (a == "unattune") it.attuned = false;
            else if (a == "charge") { if (it.charges > 0) it.charges--; else { msg = "No charges"; return false; } }
            else if (a == "recharge") it.charges = it.chargesMax;
            else if (a == "more") it.qty++;
            else if (a == "less") { if (--it.qty <= 0) c_.items.erase(c_.items.begin() + i); }
            else if (a == "remove") c_.items.erase(c_.items.begin() + i);
            else if (a == "drink") {
                if (!it.heal.length()) { msg = "Not a potion"; return false; }
                const dice::Result r = webRoll(it.name, it.heal);
                c_.hp = std::min(c_.hpMax, c_.hp + std::max(0, r.total));
                if (c_.hp > 0) c_.deathOk = c_.deathFail = 0;
                msg = String("Healed ") + r.total;
                if (--it.qty <= 0) c_.items.erase(c_.items.begin() + i);
            } else if (a == "roll") {
                // the dice to roll, for the page (attack and damage for a weapon)
                extra = "[";
                if (it.isWeapon()) {
                    for (auto& at : c_.attacks()) if (at.name == it.name) {
                        extra += "{\"label\":\"Attack\",\"expr\":" + web::js(at.hit) + "},{\"label\":\"Damage\",\"expr\":" + web::js(at.damage) + "}";
                        break;
                    }
                } else if (it.damage.length()) extra += "{\"label\":\"Damage\",\"expr\":" + web::js(it.damage) + "}";
                else if (it.roll.length()) extra += "{\"label\":" + web::js(it.rolllabel.length() ? it.rolllabel : String("Roll")) + ",\"expr\":" + web::js(it.roll) + "}";
                extra += "]";
                return true;
            } else { msg = "Unknown item action"; return false; }
        } else if (what == "cast") {
            if (spellInfo_.size() != spellCount()) loadSpellInfo();
            const int i = c.num("i", -1);
            if (i < 0 || i >= (int)spellInfo_.size() || spellInfo_[i].name != c.arg("name")) { msg = "That spell changed - reload"; return false; }
            auto& sp = spellInfo_[i];
            // slot 0: the free cast; none given: the free cast if there is one, else the lowest slot
            const auto opts = castOptions(sp);
            const int slot = c.arg("slot").length() ? c.num("slot") : opts.size() ? opts[0] : sp.level;
            bool free;
            if (!payCast(sp, slot, free, msg)) return false;
            const int abl = spellAbilFor(sp);
            const int m = abl >= 0 ? c_.mod(abl) : 0;
            extra = "[";
            if (sp.attack.length()) extra += "{\"label\":\"Spell attack\",\"expr\":" + web::js("d20" + fmtMod(m + c_.prof())) + "}";
            const String base = sp.damage.length() ? sp.damage : sp.heal;
            if (base.length()) {
                const String e = spellmath::castExpr(base, sp.higher, sp.level, std::max(sp.level, slot), c_.level, m);
                extra += String(sp.attack.length() ? "," : "") + "{\"label\":" + web::js(sp.damage.length() ? "Damage" : "Healing") +
                         ",\"expr\":" + web::js(e) + "}";
            }
            extra += "]";
            msg = String("Cast ") + sp.name + (free ? String(" (free cast)") : sp.level ? String(" (level ") + slot + " slot" + (slot > sp.level ? ", upcast" : "") + ")" : String(""));
            if (sp.save.length()) msg += String(", DC ") + (8 + m + c_.prof()) + " " + sp.save;
        } else { msg = "Unknown change"; return false; }
        save();
        return true;
    }

    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (c.op == "list") {
            refreshList();
            out = "[";
            for (size_t i = 0; i < files_.size(); i++)
                out += String(i ? "," : "") + "{\"file\":" + web::js(files_[i].substring(files_[i].lastIndexOf('/') + 1)) + ",\"name\":" + web::js(names_[i]) +
                       ",\"line\":" + web::js(lines_[i]) + ",\"ddb\":" + web::js(ddb_[i]) + "}";
            out += "]";
            return 200;
        }
#ifdef BOARD_TAB5
        if (c.op == "import") {
            String path, msg;
            if (!importFromDdb(c.arg("link").length() ? c.arg("link") : c.body, path, msg)) { out = msg; return 400; }
            out = String("{\"msg\":") + web::js(msg) + ",\"file\":" + web::js(path.substring(path.lastIndexOf('/') + 1)) + "}";
            return 200;
        }
#endif
        if (c.op != "sheet" && c.op != "act") return 404;
        int code = 200;
        if (c.op == "act" && loaded_ && c_.path == String("/gmtools/characters/") + c.arg("file")) {
            // the device has this one open: items or spells may move under its menus
            if (mode_ == Mode::ItemMenu) mode_ = Mode::Sheet;
            castRef_ = -1;
            usesRef_ = -1;   // a counter may be added or removed
            confirmRemove_ = false;
        }
        const bool found = withChar(c.arg("file"), [&] {
            String msg, extra;
            if (c.op == "act" && !webAct(c, msg, extra)) { out = msg; code = 400; return; }
            out = String("{\"msg\":") + web::js(msg) + ",\"rolls\":" + (extra.length() ? extra : String("[]")) + ",\"sheet\":" + sheetJson() + "}";
        });
        if (!found) { out = "No such character"; return 404; }
        return code;
    }

    // ------------------------------------------------------------ rows per tab
    void buildRows() {
        rowsFor(tab_, rows_);
        if (sel_ >= (int)rows_.size()) sel_ = std::max(0, (int)rows_.size() - 1);
    }

    // On the one-page sheet the stat strip shows AC, speed and proficiency,
    // so the Main panel leaves those rows out.
    void rowsFor(int tab, std::vector<Row>& out) {
        out.clear();
        auto add = [&](RowKind k, int ref, const String& l, const String& r, uint16_t col = C_TEXT) { out.push_back({k, ref, l, r, col}); };
        const bool page = wide();
        switch (tab) {
            case 0: {
                String hp = String(c_.hp) + "/" + c_.hpMax + (c_.hpTemp ? String(" +") + c_.hpTemp + " tmp" : String(""));
                add(R_HP, 0, "Hit Points", hp, c_.hp * 2 < c_.hpMax ? C_BLOOD : C_EMERALD);
                String how;
                int ac = c_.ac(&how);
                if (!page) add(R_AC, 0, "Armor Class  (" + how + ")", String(ac), C_ICE);
                add(R_INIT, 0, "Initiative", fmtMod(c_.initiative()), C_GOLD);
                if (!page) add(R_NONE, 0, String("Speed ") + c_.speed + " ft  Prof " + fmtMod(c_.prof()), String("PP ") + c_.passivePerception(), C_DIM);
                add(R_HD, 0, page ? "Hit Dice" : "Hit Dice (Enter: spend)", String(c_.level - c_.hitDiceUsed) + "/" + c_.level + " d" + c_.hitDie(), C_TEXT);
                if (c_.hp == 0 || c_.deathOk || c_.deathFail)
                    add(R_DEATH, 0, "Death save", String(c_.deathOk) + " ok / " + c_.deathFail + " fail", C_BLOOD);
                add(R_INSP, 0, "Heroic Inspiration", c_.inspiration ? "Yes" : "No", c_.inspiration ? C_GOLD : C_DIM);
                for (size_t i = 0; i < c_.res.size(); i++)
                    add(R_RES, i, c_.res[i].name + (page ? "" : "  (-/= adjust)"), String(c_.res[i].cur) + "/" + c_.res[i].max, C_ARCANE);
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
                    String l = String(lv == 2 ? "** " : lv == 1 ? "*  " : "   ") + SKILLS[i].name;
                    if (!page) l += String(" (") + ABIL_SHORT[SKILLS[i].ab] + ")";
                    add(R_SKILL, i, l, (page ? String(ABIL_SHORT[SKILLS[i].ab]) + " " : String("")) + fmtMod(c_.skillMod(i)), lv ? C_GOLD : C_TEXT);
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
                if (spellInfo_.size() != spellCount()) loadSpellInfo();
                int ab = c_.spellAbil();
                String head = ab >= 0 ? String("DC ") + c_.spellDC() + "  Atk " + fmtMod(c_.spellAttack()) : String("No spellcasting");
                add(R_NONE, 0, head, "", C_ARCANE);
                const int pact = c_.pactSlotLevel();
                for (int s = 1; s <= 9; s++) {
                    const int mx = c_.maxSlots(s);
                    if (!mx) continue;
                    const String l = pact ? String("Pact slots (level ") + s + ", Short Rest)" : String("Level ") + s + " slots";
                    add(R_SLOTS, s, l, String(mx - c_.slotsUsed[s]) + "/" + mx, C_ARCANE);
                }
                for (size_t i = 0; i < spellInfo_.size(); i++) {
                    auto& s = spellInfo_[i];
                    String r = s.damage.length() ? s.damage + " " + s.dtype.substring(0, 4) : s.heal.length() ? "heal " + s.heal : s.save.length() ? s.save.substring(0, 3) + " save" : "";
                    r.replace("+MOD", "+m");
                    add(R_SPELL, i, spellLabel(s), r, s.found ? (s.level ? C_TEXT : C_ICE) : C_DIM);
                }
                break;
            }
            case 5: {
                const char* cn[] = {"cp", "sp", "ep", "gp", "pp"};
                String coins;
                for (int i = 4; i >= 0; i--) if (c_.coins[i] || i == 3) coins += String(c_.coins[i]) + cn[i] + " ";
                add(R_COINS, 0, page ? "Coins" : "Coins (Enter to change)", coins, C_GOLD);
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
                if (!featuresLoaded_) loadFeatures();
                for (size_t i = 0; i < featureNames_.size(); i++) {
                    auto p = splitList(featureNames_[i]);
                    add(R_FEATURE, i, p.size() > 1 ? p[1] : featureNames_[i], String("L") + (p.size() ? p[0] : ""), C_TEXT);
                }
                add(R_TRAIT, 0, c_.species + (c_.speciesOpt.length() ? " - " + c_.speciesOpt : String("")) + " traits", "", C_EMERALD);
                for (size_t i = 0; i < c_.feats.size(); i++) {
                    String r = "feat";
                    for (auto& x : c_.res)
                        if (x.from == c_.feats[i]) r = x.name + " " + x.cur + "/" + x.max + "  feat";
                    add(R_FEAT, i, c_.feats[i], r, C_GOLD);
                }
                if (c_.darkvision) add(R_NONE, 0, String("Darkvision ") + c_.darkvision + " ft", "", C_DIM);
                for (auto& t : c_.toolProf) add(R_NONE, 0, "Tool: " + t, "", C_DIM);
                break;
            }
        }
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
        bool free;
        String why;
        if (!payCast(s, slot, free, why)) { sfx::error(); toast(why.c_str(), C_BLOOD); return; }
        if (s.level) save();
        int ab = spellAbilFor(s);
        int m = ab >= 0 ? c_.mod(ab) : 0;
        std::vector<rollui::Line> lines;
        if (s.attack.length()) lines.push_back({"Spell attack", "d20" + fmtMod(m + c_.prof()), ""});
        String base = s.damage.length() ? s.damage : s.heal;
        if (base.length()) {
            const String e = spellmath::castExpr(base, s.higher, s.level, std::max(s.level, slot), c_.level, m);
            lines.push_back({s.damage.length() ? "Damage" : "Healing", e, s.damage.length() ? s.dtype : String("HP")});
        }
        String title = s.name + (s.level && slot > s.level ? String(" @") + slot : String(""));
        if (s.save.length() && lines.size() <= 1) title += String(" DC ") + (8 + m + c_.prof()) + " " + s.save.substring(0, 3);
        if (lines.empty()) {
            toast((String("Cast ") + s.name + (free ? String(" (free cast)") : s.level ? String(" (slot ") + slot + ")" : String(""))).c_str(), C_ARCANE);
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
        if (spells && c_.ci.loaded && !c_.ci.caster() && c_.spells.empty()) {
            // feat and species spells come with the feat or species
            sfx::error();
            toast((c_.cls + "s don't learn spells").c_str(), C_DIM);
            return;
        }
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
                c_.deathFail = std::min(3, c_.deathFail);
                if (c_.deathOk >= 3) { toast("Stable", C_EMERALD); c_.deathOk = c_.deathFail = 0; }
                if (c_.deathFail >= 3) toast("Dead...", C_BLOOD);
                save();
                break;
            }
            case R_INSP: c_.inspiration = !c_.inspiration; save(); sfx::click(); break;
            case R_RES: {
                if (wide()) { openUses(r.ref); break; }
                auto& res = c_.res[r.ref];
                if (res.cur > 0) { res.cur--; save(); toast((String("Used ") + res.name).c_str(), C_ARCANE); }
                else { sfx::error(); toast("None left", C_BLOOD); }
                break;
            }
            case R_SLOTS: {
                const int sl = r.ref;
                if (sl < 1) break;
                if (c_.maxSlots(sl) - c_.slotsUsed[sl] <= 0) { sfx::error(); toast("None left - hold to get one back", C_BLOOD); break; }
                c_.slotsUsed[sl]++;
                save();
                sfx::click();
                toast((String("Used a level ") + sl + " slot (" + (c_.maxSlots(sl) - c_.slotsUsed[sl]) + " left)").c_str(), C_ARCANE);
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
                if (s.level == 0) { castSpell(r.ref, 0); break; }
                // the free cast if there is one, else the lowest slot; the panel picks another (upcasting)
                const auto opts = castOptions(s);
                if (opts.empty()) { sfx::error(); toast(s.src ? "Free cast used - it's back after a Long Rest" : "No spell slots left", C_BLOOD); break; }
                if (opts.size() == 1 && opts[0] == 0) { castSpell(r.ref, 0); break; }
                castRef_ = r.ref;
                castSlot_ = opts[0];
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
                const int s = r.ref;
                if (s < 1 || s > 9) return;
                if (d < 0 && c_.maxSlots(s) - c_.slotsUsed[s] > 0) c_.slotsUsed[s]++;
                else if (d > 0 && c_.slotsUsed[s] > 0) c_.slotsUsed[s]--;
                else { sfx::error(); return; }
                break;
            }
            case R_HP: c_.hp = std::max(0, std::min(c_.hpMax, c_.hp + d)); break;
            default: return;
        }
        sfx::click();
        save();
    }

    void deleteCharacter(int i) {
        if (i < 0 || i >= (int)files_.size()) return;
        storage::remove(files_[i].c_str());
        refreshList();
        listSel_ = std::min(listSel_, (int)files_.size() + head() - 1);
        toast("Character deleted", C_DIM);
    }

    void forgetSpell(int ref) {
        if (ref < 0 || ref >= (int)spellInfo_.size()) return;
        auto& s = spellInfo_[ref];
        if (s.src == 2) { sfx::error(); toast(("Comes with your species (" + s.from + ")").c_str(), C_DIM); return; }
        auto& v = s.src ? c_.miSpells : c_.spells;
        v.erase(std::remove(v.begin(), v.end(), s.name), v.end());
        spellInfo_.clear();
        save();
    }

    // ------------------------------------------------------------ resources and their actions
    void openUses(int ri) {
        if (ri < 0 || ri >= (int)c_.res.size()) return;
        usesRef_ = ri;
        uses_ = c_.uses(c_.res[ri]);
        usesSel_ = 0;
        usesArm_ = false;
        sfx::select();
    }

    // What to tell after a use: the rule's note, or for one that changes slots or
    // another resource, what changed (and the note).
    static String useMessage(const Use& u, const String& msg, const String& note) {
        if (u.effect == U_NONE && !u.target.length()) return note.length() ? u.name + ": " + note : msg;
        return note.length() ? msg + " - " + note : msg;
    }

    // Spends a resource on one of its actions; the two slot actions ask for a level first.
    void runUse(int ri, const Use& u, int slotLevel = 0) {
        if ((u.effect == U_MAKE_SLOT || u.effect == U_SLOT_POINTS) && !slotLevel) {
            const auto lv = c_.useLevels(u);
            if (lv.empty()) { sfx::error(); toast(u.effect == U_MAKE_SLOT ? "No slot you can create" : "No spell slots left", C_BLOOD); return; }
            std::vector<holdmenu::Item> items;
            for (int l : lv) {
                const String t = u.effect == U_MAKE_SLOT
                    ? String("Level ") + l + " slot  (" + Character::slotPointCost(l) + " points)"
                    : String("Level ") + l + " slot -> " + l + " points  (" + (c_.maxSlots(l) - c_.slotsUsed[l]) + " left)";
                items.push_back({t, [this, ri, u, l] { runUse(ri, u, l); }});
            }
            holdmenu::open(u.name, std::move(items));
            return;
        }
        String msg;
        if (!c_.spendUse(ri, u, slotLevel, msg)) { sfx::error(); toast(msg.c_str(), C_BLOOD); return; }
        save();
        const String roll = c_.expand(u.roll), note = c_.expand(u.note);
        if (roll.length()) {
            String title = u.name;
            if (note.length() && note.length() <= 36) title += " - " + note;
            rollui::show(title, std::vector<rollui::Line>{{u.label, roll, ""}});
            if (u.effect == U_HEAL || u.effect == U_TEMP) {
                rollui::lockResult();
                c_.applyUseRoll(u, rollui::total(0));
                save();
            }
        } else {
            toast(useMessage(u, msg, note).c_str(), C_ARCANE);
            sfx::select();
        }
    }

    // "Lucky, 3, long" (uses default to 1, rest to long)
    bool addCounter(const String& in, String& why) {
        auto p = splitList(in, ',');
        if (p.empty()) return false;
        for (auto& x : p) x.trim();
        if (!p[0].length()) return false;
        if (c_.resIndex(p[0]) >= 0) { why = "There's already a counter with that name"; return false; }
        Resource r;
        r.name = p[0];
        r.max = r.cur = std::max(1, std::min(99, p.size() > 1 ? (int)p[1].toInt() : 1));
        String rest = p.size() > 2 ? p[2] : String("long");
        rest.toLowerCase();
        r.rest = rest.startsWith("s") ? 'S' : 'L';
        r.custom = true;
        c_.res.push_back(r);
        save();
        sfx::select();
        toast((String("Added ") + r.name).c_str(), C_EMERALD);
        return true;
    }

    void removeCounter(int ri) {
        if (ri < 0 || ri >= (int)c_.res.size() || !c_.res[ri].custom) return;
        c_.res.erase(c_.res.begin() + ri);
        usesRef_ = -1;
        sel_ = 0;
        save();
        toast("Counter removed", C_DIM);
    }

    // The actions panel: Up/Down/Left/Right pick, Enter uses, -/= adjust, Esc closes.
    // buttons per row: three, or fewer (and wider) when there are fewer actions
    int useCols() const { return std::max(1, std::min(3, (int)uses_.size())); }
    void handleUses(const input::Event& e) {
        const int n = std::max(1, (int)uses_.size()), USE_COLS = useCols();
        Resource& r = c_.res[usesRef_];
        if (!(e.key == input::K_CHAR && e.base == 'x')) usesArm_ = false;
        switch (e.key) {
            case input::K_BACK: usesRef_ = -1; sfx::back(); return;
            case input::K_LEFT: usesSel_ = (usesSel_ + n - 1) % n; sfx::click(); return;
            case input::K_RIGHT: usesSel_ = (usesSel_ + 1) % n; sfx::click(); return;
            case input::K_UP: if (usesSel_ >= USE_COLS) { usesSel_ -= USE_COLS; sfx::click(); } return;
            case input::K_DOWN: if (usesSel_ + USE_COLS < n) { usesSel_ += USE_COLS; sfx::click(); } return;
            case input::K_ENTER:
                if (uses_.empty()) {
                    if (r.cur > 0) { r.cur--; save(); sfx::click(); toast((String("Used ") + r.name).c_str(), C_ARCANE); }
                    else { sfx::error(); toast("None left", C_BLOOD); }
                } else runUse(usesRef_, uses_[usesSel_]);
                return;
            case input::K_CHAR:
                if (e.base == '-' || e.base == '=') {
                    r.cur = std::max(0, std::min(r.max, r.cur + (e.base == '=' ? 1 : -1)));
                    save();
                    sfx::click();
                } else if (e.base == 'x' && r.custom) {
                    if (!usesArm_) { usesArm_ = true; toast("X again to remove this counter", C_BLOOD); }
                    else removeCounter(usesRef_);
                }
                return;
            default: return;
        }
    }

    void toggleSkill(int ref, bool expertise) {
        String sk = SKILLS[ref].name;
        auto& v = expertise ? c_.expertise : c_.skillProf;
        bool had = listHas(v, sk);
        v.erase(std::remove_if(v.begin(), v.end(), [&](const String& x) { return x.equalsIgnoreCase(sk); }), v.end());
        if (!had) v.push_back(sk);
        if (expertise && !had && !listHas(c_.skillProf, sk)) c_.skillProf.push_back(sk);
        save();
        sfx::click();
    }

    // A long press on a sheet row: what the key combos do, for fingers.
    void holdRow(int tab, int i, const Row& r) {
        tab_ = tab;
        sel_ = i;
        std::vector<holdmenu::Item> items;
        const String name = r.left;
        switch (r.kind) {
            case R_SPELL:
                items.push_back({"Cast", [this, r] { rowEnter(r); }});
                items.push_back({"Forget", [this, r] { forgetSpell(r.ref); }, C_BLOOD, true});
                break;
            case R_SKILL: {
                const String sk = SKILLS[r.ref].name;
                items.push_back({"Roll", [this, r] { rowEnter(r); }});
                items.push_back({listHas(c_.skillProf, sk) ? "Remove proficiency" : "Proficient", [this, r] { toggleSkill(r.ref, false); }});
                items.push_back({listHas(c_.expertise, sk) ? "Remove expertise" : "Expertise", [this, r] { toggleSkill(r.ref, true); }});
                break;
            }
            case R_ABIL:
                items.push_back({"Check", [this, r] { rollCheck(String(ABIL_NAMES[r.ref]) + " check", c_.mod(r.ref)); }});
                items.push_back({"Save", [this, r] { rollCheck(String(ABIL_NAMES[r.ref]) + " save", c_.saveMod(r.ref)); }});
                items.push_back({"Score +1", [this, r] { adjust(r, 1); }});
                items.push_back({"Score -1", [this, r] { adjust(r, -1); }});
                break;
            case R_RES:
                if (wide()) items.push_back({"Actions", [this, r] { openUses(r.ref); }});
                items.push_back({"Use one", [this, r] { adjust(r, -1); }});
                items.push_back({"Get one back", [this, r] { adjust(r, 1); }});
                if (c_.res[r.ref].custom) items.push_back({"Remove counter", [this, r] { removeCounter(r.ref); }, C_BLOOD, true});
                break;
            case R_HD: case R_SLOTS: case R_HP:
                items.push_back({"Use one", [this, r] { adjust(r, -1); }});
                items.push_back({"Get one back", [this, r] { adjust(r, 1); }});
                break;
            case R_ITEM:
                items.push_back({"Actions", [this, r] { rowEnter(r); }});
                items.push_back({"Quantity +1", [this, r] { adjust(r, 1); }});
                items.push_back({"Quantity -1", [this, r] { adjust(r, -1); }});
                break;
            default:
                if (tab != 0) return;   // nothing more than a tap does
                break;
        }
        if (tab == 0) items.push_back({"Add a counter", [this] { prompt_.open("Counter: name, uses, short/long", "", 31, 40); }});
        if (tab == 4) items.push_back({"Learn a spell", [this] { openPicker(true); }});
        if (tab == 5) items.push_back({"Add an item", [this] { openPicker(false); }});
        holdmenu::open(name, std::move(items));
    }

    // ------------------------------------------------------------ drawing
    void drawList() {
        drawBackground();
        drawHeader("Characters", ACCENT);
        gfx.setFont(ui::font(&fonts::Font0));
        int n = files_.size() + head();
        const int rowH = touchRow(13), pad = touchPad(13), y0 = HEADER_H + u(wide() ? 10 : 4);
        int rows = (H - FOOTER_H - y0) / rowH;
        int first = std::max(0, std::min(listSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = y0 + (i - first) * rowH + pad;
            bool s = i == listSel_;
            if (s) drawPanel(u(4), y - pad - u(2), W - u(8), rowH, C_PANEL, ACCENT);
            touch::row(0, y - pad - u(2), W, rowH, listSel_, i);
            gfx.setTextDatum(top_left);
            if (i == 0) { gfx.setTextColor(C_GOLD); gfx.drawString("+ New character", u(12), y + u(1)); continue; }
            if (i < head()) { gfx.setTextColor(C_ICE); gfx.drawString("+ Import from D&D Beyond", u(12), y + u(1)); continue; }
            const int f = i - head();
            touch::onHold(0, y - pad - u(2), W, rowH, [this, i, f] {
                listSel_ = i;
                std::vector<holdmenu::Item> menu = {{"Open", [this, f] { openCharacter(files_[f]); }}};
#ifdef BOARD_TAB5
                if (ddb_[f].length()) menu.push_back({"Update from D&D Beyond", [this, f] { runImport(ddb_[f]); }});
#endif
                menu.push_back({"Delete", [this, f] { deleteCharacter(f); }, C_BLOOD, true});
                holdmenu::open(names_[f], menu);
            });
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            gfx.drawString(names_[f], u(12), y + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(lines_[f] + (ddb_[f].length() ? "  DDB" : ""), W - u(10), y + u(1));
        }
        drawFooter(!storage::ok() ? "Needs an SD card" : wide() ? "Ent open  hold for more  Esc back" : "Ent open  FnX delete  Esc back");
        prompt_.draw("The link, or just the number in it");
    }

    void drawSheet() {
        if (wide()) { drawPage(); return; }
        drawBackground();
        // header: name + class line + HP bar
        gfx.fillRect(0, 0, W, HEADER_H, C_BG2);
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(ACCENT);
        String nm = c_.name;
        while (nm.length() > 2 && gfx.textWidth(nm) > u(110)) nm = nm.substring(0, nm.length() - 1);
        gfx.drawString(nm, u(5), HEADER_H / 2);
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String("Lv") + c_.level + " " + c_.cls.substring(0, 9), W - u(4), wide() ? HEADER_H / 3 : 5);
        drawBar(W - u(64), wide() ? HEADER_H * 2 / 3 : 10, u(60), u(3), c_.hpMax ? (float)c_.hp / c_.hpMax : 0, c_.hp * 2 < c_.hpMax ? C_BLOOD : C_EMERALD, C_BG);
        // tabs
        int ty = HEADER_H;
        gfx.fillRect(0, ty, W, u(11), C_BG);
        int tw = W / NTABS;
        for (int i = 0; i < NTABS; i++) {
            bool on = i == tab_;
            if (on) gfx.fillRect(i * tw, ty, tw, u(11), C_PANEL);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(on ? ACCENT : C_DIM);
            gfx.drawString(TABS[i], i * tw + tw / 2, ty + u(2));
        }
        gfx.drawFastHLine(0, ty + u(11), W, C_EDGE);

        buildRows();
        const int rowH = u(11), y0 = ty + u(13);
        int rows = (H - FOOTER_H - y0) / rowH;
        if (sel_ < top_) top_ = sel_;
        if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
        for (int i = top_; i < (int)rows_.size() && i < top_ + rows; i++) {
            const Row& r = rows_[i];
            int y = y0 + (i - top_) * rowH;
            bool s = i == sel_;
            if (s) gfx.fillRoundRect(u(2), y - u(1), W - u(4), rowH, u(2), C_PANEL);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : r.color);
            String l = r.left;
            gfx.setTextDatum(top_right);
            int rw = gfx.textWidth(r.right);
            gfx.setTextDatum(top_left);
            while (l.length() > 2 && gfx.textWidth(l) > W - u(16) - rw) l = l.substring(0, l.length() - 1);
            gfx.drawString(l, u(6), y + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(r.color);
            gfx.drawString(r.right, W - u(6), y + u(1));
        }
        if ((int)rows_.size() > rows) {
            int sh = std::max(u(5), rows * rowH * rows / (int)rows_.size());
            int sy = y0 + (rows * rowH - sh) * top_ / std::max(1, (int)rows_.size() - rows);
            gfx.fillRect(W - u(2), sy, u(2), sh, ACCENT);
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

    // ------------------------------------------------------------ sheet pages (big screen)
    // The big screen shows the sheet as pages with large, finger-sized
    // targets. A page holds one or two of the tabs' row lists (tab_ still
    // says which one the keys act on).
    struct SheetPage { const char* name; int tabs[2]; int n; };
    static constexpr int NPAGES = 6;
    static constexpr int PG_ROW = 60, PG_TITLE = 44, PG_GAP = 10, TAB_H = 60;
    const SheetPage* pages() const {
        static const SheetPage P[NPAGES] = {{"Abilities", {1}, 1}, {"Skills", {2}, 1}, {"Combat", {0, 3}, 2},
                                            {"Spells", {4}, 1}, {"Items", {5}, 1}, {"Features", {6}, 1}};
        return P;
    }
    int pageOf(int tab) const {
        for (int p = 0; p < NPAGES; p++)
            for (int i = 0; i < pages()[p].n; i++) if (pages()[p].tabs[i] == tab) return p;
        return 0;
    }
    int bodyTop() const { return HEADER_H + TAB_H + PG_GAP; }
    int bodyBottom() const { return H - FOOTER_H - PG_GAP; }

    void drawPage() {
        const Metrics& m = metrics();
        drawBackground();
        drawHeader(c_.name.c_str(), ACCENT);
        gfx.setFont(m.title);
        const int nx = headerTitleX() + gfx.textWidth(c_.name) + u(8);
        gfx.setFont(m.small);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_DIM);
        String sub = c_.levelLine() + "   " + c_.species + (c_.background.length() ? "   " + c_.background : String(""));
        gfx.drawString(fitText(sub, W - nx - u(40)), nx, HEADER_H / 2 + 2);

        // page tabs: big enough to hit with a thumb
        const int pg = pageOf(tab_), tw = (W - 2 * m.pad) / NPAGES;
        gfx.setFont(m.bold);
        gfx.setTextDatum(middle_center);
        for (int p = 0; p < NPAGES; p++) {
            const int x = m.pad + p * tw;
            const bool on = p == pg;
            gfx.fillRoundRect(x + 3, HEADER_H + 6, tw - 6, TAB_H - 8, 10, on ? C_PANEL : C_BG2);
            gfx.drawRoundRect(x + 3, HEADER_H + 6, tw - 6, TAB_H - 8, 10, on ? ACCENT : C_EDGE);
            gfx.setTextColor(on ? ACCENT : C_DIM);
            gfx.drawString(pages()[p].name, x + tw / 2, HEADER_H + 6 + (TAB_H - 8) / 2);
            const int first = pages()[p].tabs[0];
            touch::zone(x, HEADER_H, tw, TAB_H, [this, first] { if (pageOf(tab_) != pageOf(first)) { tab_ = first; sel_ = top_ = 0; sfx::click(); } });
        }

        buildRows();
        const int x0 = m.pad, y0 = bodyTop(), w = W - 2 * m.pad, h = bodyBottom() - y0;
        switch (pg) {
            case 0: drawAbilitiesPage(x0, y0, w, h); break;
            case 1: drawSkillsPage(x0, y0, w, h); break;
            case 2: {
                const int lw = w * 47 / 100;
                drawRowPanel(0, "Combat", x0, y0, lw, h);
                drawRowPanel(3, "Attacks", x0 + lw + PG_GAP, y0, w - lw - PG_GAP, h);
                break;
            }
            default: drawRowPanel(pages()[pg].tabs[0], pages()[pg].name, x0, y0, w, h); break;
        }

        const char* act[] = {"Ent use/spend  hold for more", "Ent check  hold for more", "Ent roll  hold for more",
                             "Ent attack+damage", "Ent cast  hold for more", "Ent actions/coins  hold for more", "Ent read"};
        drawFooter((String("</> next  ") + act[tab_] + "  Esc back").c_str());
        if (abilPick_ >= 0) drawAbilityPick();
        if (castRef_ >= 0) drawCast();
        if (usesRef_ >= 0) drawUses();
        prompt_.draw(prompt_.tag() == 1 ? "Blank = cancel. Dice work: 2d6" : prompt_.tag() == 31 ? "e.g. Lucky, 3, long  (or short)" : nullptr);
    }

    // Abilities: six big boxes on the left (tap one: check or save), the
    // numbers looked up most on the right.
    void drawAbilitiesPage(int x0, int y0, int w, int h) {
        const int g = PG_GAP, lw = w * 58 / 100, bw = (lw - 2 * g) / 3, bh = (h - g) / 2;
        for (int a = 0; a < 6; a++) drawAbilityBox(a, x0 + (a % 3) * (bw + g), y0 + (a / 3) * (bh + g), bw, bh);
        const int rx = x0 + lw + 2 * g, rw = x0 + w - rx, hpH = 170;
        const uint16_t col = c_.hp * 2 < c_.hpMax ? C_BLOOD : C_EMERALD;
        String label = c_.hpTemp ? String("HIT POINTS  +") + c_.hpTemp + " temp" : String("HIT POINTS");
        drawStatBox(rx, y0, rw, hpH, label.c_str(), String(c_.hp) + " / " + c_.hpMax, col, "tap: damage or heal");
        drawBar(rx + 20, y0 + hpH - 46, rw - 40, 12, c_.hpMax ? (float)c_.hp / c_.hpMax : 0, col, C_BG);
        touch::zone(rx, y0, rw, hpH, [this] { tapRow(0, R_HP); });
        const int sw = (rw - 2 * g) / 3, sy = y0 + hpH + g, sh = (y0 + h - sy - g) / 2;
        drawStatBox(rx, sy, sw, sh, "AC", String(c_.ac()), C_ICE, nullptr);
        drawStatBox(rx + sw + g, sy, sw, sh, "INITIATIVE", fmtMod(c_.initiative()), C_GOLD, "tap: roll");
        touch::zone(rx + sw + g, sy, sw, sh, [this] { tapRow(0, R_INIT); });
        drawStatBox(rx + 2 * (sw + g), sy, sw, sh, "SPEED", String(c_.speed), C_TEXT, nullptr);
        const int sy2 = sy + sh + g;
        drawStatBox(rx, sy2, sw, sh, "PROFICIENCY", fmtMod(c_.prof()), C_TEXT, nullptr);
        drawStatBox(rx + sw + g, sy2, sw, sh, "PASSIVE", String(c_.passivePerception()), C_TEXT, nullptr);
        drawStatBox(rx + 2 * (sw + g), sy2, sw, sh, "INSPIRATION", c_.inspiration ? "Yes" : "No",
                    c_.inspiration ? C_GOLD : C_DIM, "tap: toggle");
        touch::zone(rx + 2 * (sw + g), sy2, sw, sh, [this] { tapRow(0, R_INSP); });
    }

    void drawAbilityBox(int a, int x, int y, int w, int h) {
        const bool s = tab_ == 1 && sel_ == a;
        const Metrics& m = metrics();
        drawPanel(x, y, w, h, s ? C_PANEL : C_BG2, s ? ACCENT : C_EDGE);
        gfx.setFont(m.small);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(s ? ACCENT : C_DIM);
        String nm = ABIL_NAMES[a];
        nm.toUpperCase();
        gfx.drawString(nm, x + w / 2, y + 14);
        gfx.setFont(ui::font(&fonts::FreeSerifBold18pt7b));
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(fmtMod(c_.mod(a)), x + w / 2, y + h / 2 - 10);
        gfx.setFont(m.body);
        gfx.setTextDatum(bottom_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(c_.score[a]), x + 18, y + h - 14);
        gfx.setTextDatum(bottom_right);
        gfx.setTextColor(c_.saveProficient(a) ? C_GOLD : C_DIM);
        gfx.drawString("save " + fmtMod(c_.saveMod(a)), x + w - 18, y + h - 14);
        // touch: ask whether to roll a check or a save
        touch::zone(x, y, w, h, [this, a] { tab_ = 1; sel_ = a; abilPick_ = a; sfx::select(); });
        touch::onHold(x, y, w, h, [this, a] { holdRow(1, a, Row{R_ABIL, a, ABIL_NAMES[a], "", C_TEXT}); });
    }

    void drawStatBox(int x, int y, int w, int h, const char* label, const String& v, uint16_t col, const char* hint) {
        const Metrics& m = metrics();
        drawPanel(x, y, w, h, C_BG2, hint ? C_EDGE : C_BG2);
        gfx.setFont(m.small);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(fitText(label, w - 12), x + w / 2, y + 12);
        gfx.setFont(ui::font(&fonts::FreeSerifBold12pt7b));
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(col);
        gfx.drawString(v, x + w / 2, y + h / 2 + (hint && h < 170 ? 0 : 2));
        if (hint && h >= 150) {
            gfx.setFont(m.small);
            gfx.setTextDatum(bottom_center);
            gfx.setTextColor(C_EDGE);
            gfx.drawString(hint, x + w / 2, y + h - 10);
        }
    }

    // The check-or-save choice after tapping an ability.
    void drawAbilityPick() {
        const Metrics& m = metrics();
        const int a = abilPick_, w = 640, h = 290, x = (W - w) / 2, y = (H - h) / 2;
        gfx.fillRoundRect(x + 6, y + 6, w, h, 14, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, ACCENT);
        touch::modal(x, y, w, h);
        gfx.setFont(m.title);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(ACCENT);
        gfx.drawString(ABIL_NAMES[a], x + w / 2, y + 16);
        const int bw = (w - 3 * 24) / 2, bh = 150, by = y + 80;
        for (int b = 0; b < 2; b++) {
            const int bx = x + 24 + b * (bw + 24);
            const bool save = b == 1;
            const uint16_t edge = save && c_.saveProficient(a) ? C_GOLD : C_EDGE;
            gfx.fillRoundRect(bx, by, bw, bh, 14, C_PANEL);
            gfx.drawRoundRect(bx, by, bw, bh, 14, edge);
            gfx.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 13, edge);
            gfx.setFont(m.bold);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString(save ? "Saving throw" : "Ability check", bx + bw / 2, by + 16);
            gfx.setFont(ui::font(&fonts::FreeSerifBold18pt7b));
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(save && c_.saveProficient(a) ? C_GOLD : C_TEXT);
            gfx.drawString(fmtMod(save ? c_.saveMod(a) : c_.mod(a)), bx + bw / 2, by + bh / 2 + 18);
            touch::zone(bx, by, bw, bh, [this, a, save] {
                abilPick_ = -1;
                if (save) rollCheck(String(ABIL_NAMES[a]) + " save", c_.saveMod(a));
                else rollCheck(String(ABIL_NAMES[a]) + " check", c_.mod(a));
            });
        }
        gfx.setFont(m.small);
        gfx.setTextDatum(bottom_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString("C check   S save   tap outside to cancel", x + w / 2, y + h - 12);
    }

    // Skills: three columns of six big tiles; a tap rolls.
    void drawSkillsPage(int x0, int y0, int w, int h) {
        const Metrics& m = metrics();
        const int g = 8, cw = (w - 2 * g) / 3, ch = (h - 5 * g) / 6;
        for (int i = 0; i < 18 && i < (int)rows_.size(); i++) {
            const int x = x0 + (i / 6) * (cw + g), y = y0 + (i % 6) * (ch + g);
            const bool s = tab_ == 2 && sel_ == i;
            const int lv = c_.skillLevel(i);
            drawPanel(x, y, cw, ch, s ? C_PANEL : C_BG2, s ? ACCENT : lv ? C_GOLD_DK : C_EDGE);
            // proficiency: a dot, expertise: a ring around it
            const int cy = y + ch / 2;
            if (lv) gfx.fillCircle(x + 22, cy, 7, C_GOLD);
            else gfx.drawCircle(x + 22, cy, 7, C_EDGE);
            if (lv == 2) gfx.drawCircle(x + 22, cy, 11, C_GOLD);
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(lv ? C_GOLD : C_TEXT);
            gfx.drawString(SKILLS[i].name, x + 42, cy - 10);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(ABIL_SHORT[SKILLS[i].ab], x + 42, cy + 16);
            gfx.setFont(ui::font(&fonts::FreeSerifBold12pt7b));
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(lv ? C_GOLD : C_TEXT);
            gfx.drawString(fmtMod(c_.skillMod(i)), x + cw - 18, cy);
            touch::zone(x, y, cw, ch, [this, i] { tab_ = 2; sel_ = i; send(input::K_ENTER); });
            touch::onHold(x, y, cw, ch, [this, i] { holdRow(2, i, Row{R_SKILL, i, SKILLS[i].name, "", C_TEXT}); });
        }
    }

    // Touch helpers: focus a list's row and act on it as Enter would.
    static void send(input::Key k, char ch = 0) {
        input::Event e;
        e.key = k;
        e.ch = e.base = ch;
        input::inject(e);
    }
    void tapRow(int tab, RowKind kind) {
        std::vector<Row> rows;
        rowsFor(tab, rows);
        // use the row right here, without moving to its tab's page
        for (auto& r : rows)
            if (r.kind == kind) { rowEnter(r); return; }
    }

    // A titled list of a tab's rows; a tap on a row uses it.
    void drawRowPanel(int tab, const char* title, int x, int y, int w, int h) {
        const Metrics& m = metrics();
        std::vector<Row> own;
        if (tab != tab_) rowsFor(tab, own);
        const std::vector<Row>& rows = tab == tab_ ? rows_ : own;
        const bool focus = tab == tab_;
        drawPanel(x, y, w, h, C_BG2, focus ? ACCENT : C_EDGE);
        gfx.setFont(m.bold);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(focus ? ACCENT : C_GOLD_DK);
        gfx.drawString(title, x + 16, y + PG_TITLE / 2 + 2);
        gfx.drawFastHLine(x + 10, y + PG_TITLE, w - 20, C_EDGE);
        touch::zone(x, y, w, PG_TITLE, [this, tab] { if (tab_ != tab) { tab_ = tab; sel_ = top_ = 0; } });
        gfx.setFont(m.body);
        const int ry0 = y + PG_TITLE + 6, n = rows.size(), vis = std::max(1, (y + h - 6 - ry0) / PG_ROW);
        const int first = focus ? std::max(0, std::min(sel_ - vis / 2, n - vis)) : 0;
        for (int i = first; i < n && i < first + vis; i++) {
            const Row& r = rows[i];
            const int ry = ry0 + (i - first) * PG_ROW;
            const bool s = focus && i == sel_;
            gfx.fillRoundRect(x + 8, ry + 3, w - 16, PG_ROW - 6, 8, s ? C_PANEL : C_BG);
            if (s) gfx.drawRoundRect(x + 8, ry + 3, w - 16, PG_ROW - 6, 8, ACCENT);
            touch::zone(x, ry, w, PG_ROW, [this, tab, i] { tab_ = tab; sel_ = i; send(input::K_ENTER); });
            touch::onHold(x, ry, w, PG_ROW, [this, tab, i, r] { holdRow(tab, i, r); });
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(r.color);
            gfx.drawString(r.right, x + w - 24, ry + PG_ROW / 2);
            int rw = r.right.length() ? gfx.textWidth(r.right) + 16 : 0;
            if (r.kind == R_SLOTS && r.ref >= 1) rw += drawSlotPips(r.ref, x + w - 24 - rw, ry, tab);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(s ? C_TEXT : r.color);
            gfx.drawString(fitText(r.left, w - 48 - rw), x + 22, ry + PG_ROW / 2);
        }
        if (n > vis) {
            const int areaH = vis * PG_ROW, sh = std::max(20, areaH * vis / n);
            gfx.fillRect(x + w - 6, ry0 + (areaH - sh) * first / (n - vis), 4, sh, focus ? ACCENT : C_EDGE);
        }
        if (!n) {
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Nothing here yet", x + w / 2, ry0 + PG_ROW);
        }
    }

    // Slot pips right-aligned at `right`: filled ones are left. Returns the width used.
    int drawSlotPips(int sl, int right, int ry, int tab) {
        const int mx = c_.maxSlots(sl), left = mx - c_.slotsUsed[sl], n = std::max(mx, left);
        const int step = 46, rad = 13, cy = ry + PG_ROW / 2;
        for (int k = 0; k < n; k++) {
            const int cx = right - 8 - rad - (n - 1 - k) * step;
            const bool full = k < left;
            const uint16_t col = k >= mx ? C_GOLD : C_ARCANE;   // gold: an extra slot (Font of Magic)
            if (full) gfx.fillCircle(cx, cy, rad, col);
            gfx.drawCircle(cx, cy, rad, col);
            gfx.drawCircle(cx, cy, rad - 1, col);
            touch::zone(cx - step / 2, ry, step, PG_ROW, [this, sl, full, tab] {
                tab_ = tab;
                if (full) c_.slotsUsed[sl]++;
                else if (c_.slotsUsed[sl] > 0) c_.slotsUsed[sl]--;
                else return;
                save();
                sfx::click();
            });
        }
        return n * step + 8;
    }

    // A resource and what spends it, as big buttons.
    void drawUses() {
        const Metrics& m = metrics();
        const Resource& r = c_.res[usesRef_];
        const int USE_COLS = useCols(), n = uses_.size(), rowsN = std::max(1, (n + USE_COLS - 1) / USE_COLS);
        const int bh = 74, gap = 10, w = 1100, top = 150;
        const int h = top + rowsN * (bh + gap) + 14, x = (W - w) / 2, y = std::max(8, (H - h) / 2);
        gfx.fillRoundRect(x + 6, y + 6, w, h, 14, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_ARCANE);
        touch::modal(x, y, w, h);
        // title, how it comes back
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_ARCANE);
        gfx.drawString(fitText(r.name, w - 420), x + 24, y + 16);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        const char* back = r.rest == 'S' ? "All back on a Short Rest" : r.rest == '1' ? "One back on a Short Rest, all on a Long Rest" : "Back on a Long Rest";
        gfx.drawString(r.from.length() && r.from != r.name ? r.from + " - " + back : String(back), x + 24, y + 60);
        // count with - and +
        auto small = [&](int bx, const char* t, char ch) {
            gfx.fillRoundRect(bx, y + 16, 64, 64, 12, C_PANEL);
            gfx.drawRoundRect(bx, y + 16, 64, 64, 12, C_EDGE);
            gfx.setFont(m.title);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_TEXT);
            gfx.drawString(t, bx + 32, y + 48);
            touch::key(bx, y + 16, 64, 64, input::K_CHAR, ch);
        };
        small(x + w - 24 - 64, "+", '=');
        small(x + w - 24 - 64 - 180 - 64, "-", '-');
        gfx.setFont(ui::font(&fonts::FreeSerifBold18pt7b));
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(r.cur ? C_TEXT : C_BLOOD);
        gfx.drawString(String(r.cur) + " / " + r.max, x + w - 24 - 64 - 90, y + 50);
        if (r.custom) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(usesArm_ ? C_BLOOD : C_DIM);
            gfx.drawString(usesArm_ ? "X again / tap again to remove" : "X: remove counter", x + w - 24, y + 92);
            touch::key(x + w - 340, y + 88, 316, 40, input::K_CHAR, 'x');
        }
        // pips (when they fit)
        if (r.max <= 20) {
            for (int k = 0; k < r.max; k++) {
                const int cx = x + 24 + 14 + k * 36, cy = y + 112;
                if (k < r.cur) gfx.fillCircle(cx, cy, 12, C_ARCANE);
                gfx.drawCircle(cx, cy, 12, C_ARCANE);
            }
        }
        // actions
        const int bw = (w - 48 - (USE_COLS - 1) * gap) / USE_COLS, gy = y + top;
        gfx.setFont(m.body);
        if (!n) {
            gfx.fillRoundRect(x + 24, gy, w - 48, bh, 12, C_PANEL);
            gfx.drawRoundRect(x + 24, gy, w - 48, bh, 12, ACCENT);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(r.cur ? C_TEXT : C_DIM);
            gfx.drawString("Use one", x + w / 2, gy + bh / 2);
            touch::key(x + 24, gy, w - 48, bh, input::K_ENTER);
            return;
        }
        for (int i = 0; i < n; i++) {
            const Use& u = uses_[i];
            const int bx = x + 24 + (i % USE_COLS) * (bw + gap), by = gy + (i / USE_COLS) * (bh + gap);
            const bool sel = i == usesSel_;
            const bool can = u.effect == U_MAKE_SLOT ? r.cur >= 2 : r.cur >= u.cost;
            gfx.fillRoundRect(bx, by, bw, bh, 12, sel ? C_PANEL : C_BG);
            gfx.drawRoundRect(bx, by, bw, bh, 12, sel ? ACCENT : C_EDGE);
            String cost = u.cost > 1 ? String(u.cost) : u.cost == 1 ? String("1") : String("");
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_GOLD);
            if (cost.length()) gfx.drawString(cost, bx + bw - 14, by + bh / 2);
            const int cw = cost.length() ? gfx.textWidth(cost) + 20 : 0;
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(can ? C_TEXT : C_DIM);
            gfx.drawString(fitText(u.name, bw - 28 - cw), bx + 14, by + bh / 2);
            touch::zone(bx, by, bw, bh, [this, i] { usesSel_ = i; send(input::K_ENTER); });
        }
    }

    void drawCast() {
        auto& s = spellInfo_[castRef_];
        if (wide()) { drawCastWide(s); return; }
        const int w = u(190), h = u(46), x = (W - w) / 2, y = 44;
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_ARCANE);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_ARCANE);
        gfx.drawString(String("Cast ") + s.name, x + u(6), y + u(5));
        gfx.setTextColor(C_TEXT);
        gfx.drawString(castSlot_ ? String("< slot level ") + castSlot_ + " >  (" + (c_.maxSlots(castSlot_) - c_.slotsUsed[castSlot_]) + " left)"
                                 : String("< free cast >  (") + freeLeft(s) + " left)", x + u(6), y + u(18));
        gfx.setTextColor(C_DIM);
        gfx.drawString("Ent cast  Esc cancel", x + u(6), y + u(32));
    }

    // Big buttons: lower slot, higher slot, cast; and what the chosen slot does.
    void drawCastWide(const SpellInfo& s) {
        const Metrics& m = metrics();
        const int w = 760, h = 470, x = (W - w) / 2, y = (H - h) / 2;
        gfx.fillRoundRect(x + 6, y + 6, w, h, 14, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_ARCANE);
        touch::modal(x, y, w, h);
        gfx.setFont(m.title);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_ARCANE);
        gfx.drawString(fitText(String("Cast ") + s.name, w - 40), x + w / 2, y + 16);
        const auto opts = castOptions(s);
        const int at = std::find(opts.begin(), opts.end(), castSlot_) - opts.begin();
        const int by = y + 70, bs = 96;
        auto button = [&](int bx, int bY, int bw, int bh, const char* t, uint16_t edge, input::Key k, bool on) {
            gfx.fillRoundRect(bx, bY, bw, bh, 14, on ? C_PANEL : C_BG);
            gfx.drawRoundRect(bx, bY, bw, bh, 14, on ? edge : C_EDGE);
            gfx.setFont(m.title);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(!on ? C_DIM : edge == C_EDGE ? C_TEXT : edge);
            gfx.drawString(t, bx + bw / 2, bY + bh / 2);
            touch::key(bx, bY, bw, bh, k);
        };
        button(x + 24, by, bs, bs, "<", C_EDGE, input::K_LEFT, at > 0);
        button(x + w - 24 - bs, by, bs, bs, ">", C_EDGE, input::K_RIGHT, at + 1 < (int)opts.size());
        gfx.setFont(m.small);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(castSlot_ ? "slot level" : "no slot needed", x + w / 2, by + 4);
        gfx.setFont(ui::font(&fonts::FreeSerifBold18pt7b));
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(castSlot_ > s.level ? C_GOLD : C_TEXT);
        gfx.drawString(castSlot_ ? String(castSlot_) : String("Free cast"), x + w / 2, by + bs / 2 + 4);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        gfx.setTextDatum(bottom_center);
        gfx.drawString(castSlot_ ? String(c_.maxSlots(castSlot_) - c_.slotsUsed[castSlot_]) + " left" : String(freeLeft(s)) + " left, back on a Long Rest",
                       x + w / 2, by + bs - 2);

        // what it does with this slot
        const int ab = spellAbilFor(s), md = ab >= 0 ? c_.mod(ab) : 0;
        String fx;
        if (s.attack.length()) fx = "Attack " + fmtMod(md + c_.prof());
        const String base = s.damage.length() ? s.damage : s.heal;
        if (base.length()) {
            const String e = spellmath::castExpr(base, s.higher, s.level, std::max(s.level, castSlot_), c_.level, md);
            fx += String(fx.length() ? "   " : "") + (s.damage.length() ? "Damage " + e + " " + s.dtype : "Heals " + e);
        }
        if (s.save.length()) fx += String(fx.length() ? "   " : "") + "DC " + (8 + md + c_.prof()) + " " + s.save;
        int ty = by + bs + 18;
        gfx.setFont(m.body);
        gfx.setTextDatum(top_left);
        if (fx.length()) {
            gfx.setTextColor(castSlot_ > s.level ? C_GOLD : C_TEXT);
            gfx.drawString(fitText(fx, w - 48), x + 24, ty);
            ty += 40;
        }
        const String up = higherText(s);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        if (up.length()) {
            std::vector<String> lines;
            wrapLines((castSlot_ > s.level ? String("Upcast +") + (castSlot_ - s.level) + ": " : String("Higher slots: ")) + up, w - 48, lines);
            for (size_t i = 0; i < lines.size() && i < 3; i++) { gfx.drawString(lines[i], x + 24, ty); ty += 28; }
        } else gfx.drawString("A higher slot doesn't change this spell.", x + 24, ty);
        button(x + 24, y + h - 24 - 84, w - 48, 84, "Cast", C_ARCANE, input::K_ENTER, true);
    }

    void drawItemMenu() {
        drawSheet();
        const int rh = touchRow(11);
        const int w = u(150) + (wide() ? 120 : 0), h = u(14) + itemMenu_.size() * rh, x = (W - w) / 2, y = std::max(u(18), (H - h) / 2);
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_GOLD);
        touch::modal(x, y, w, h);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        String nm = c_.items[itemRef_].name;
        if (!wide() && nm.length() > 24) nm = nm.substring(0, 23) + ".";
        gfx.drawString(fitText(nm, w - u(12)), x + u(6), y + u(3));
        for (size_t i = 0; i < itemMenu_.size(); i++) {
            int ry = y + u(13) + i * rh;
            bool s = (int)i == itemMenuSel_;
            if (s) gfx.fillRect(x + u(3), ry - u(1), w - u(6), rh - 2, C_PANEL);
            touch::row(x, ry - u(1), w, rh, itemMenuSel_, i);
            gfx.setTextColor(itemMenu_[i].startsWith("Remove") ? rgb(230, 140, 130) : s ? C_TEXT : rgb(200, 192, 180));
            gfx.drawString(itemMenu_[i], x + u(9), ry + touchPad(11));
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
                board::present(gfx);
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
                featuresLoaded_ = false;
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
#ifdef BOARD_TAB5
            if (prompt_.active()) {
                prompt_.handle(e);
                if (prompt_.done() && prompt_.tag() == 40 && prompt_.value().length()) runImport(prompt_.value());
                return true;
            }
#endif
            int n = files_.size() + head();
            if (!(e.key == input::K_CHAR && e.fn && e.base == 'x')) confirmDel_ = false;
            switch (e.key) {
                case input::K_BACK: return false;
                case input::K_UP: listSel_ = (listSel_ + n - 1) % n; sfx::click(); break;
                case input::K_DOWN: listSel_ = (listSel_ + 1) % n; sfx::click(); break;
                case input::K_ENTER:
                    if (listSel_ == 0) startCreate();
#ifdef BOARD_TAB5
                    else if (listSel_ < head()) {
                        prompt_.restoreTextModeTo(false);
                        prompt_.open("D&D Beyond character link", "", 40, 90);
                    }
#endif
                    else openCharacter(files_[listSel_ - head()]);
                    break;
                case input::K_CHAR:
                    if (e.fn && e.base == 'x' && listSel_ >= head()) {
                        if (!confirmDel_) { confirmDel_ = true; toast("Fn+X again to delete", C_BLOOD); break; }
                        confirmDel_ = false;
                        deleteCharacter(listSel_ - head());
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
            else if (prompt_.tag() == 31) { String why; if (!addCounter(prompt_.value(), why) && why.length()) { sfx::error(); toast(why.c_str(), C_BLOOD); } }
        }
        return true;
    }
    if (usesRef_ >= 0) { handleUses(e); return true; }
    if (abilPick_ >= 0) {
        const int a = abilPick_;
        if (e.key == input::K_BACK) { abilPick_ = -1; sfx::back(); }
        else if (e.key == input::K_ENTER || (e.key == input::K_CHAR && e.base == 'c')) { abilPick_ = -1; rollCheck(String(ABIL_NAMES[a]) + " check", c_.mod(a)); }
        else if (e.key == input::K_CHAR && e.base == 's') { abilPick_ = -1; rollCheck(String(ABIL_NAMES[a]) + " save", c_.saveMod(a)); }
        return true;
    }
    if (castRef_ >= 0) {
        auto& s = spellInfo_[castRef_];
        if (e.key == input::K_BACK) castRef_ = -1;
        else if (e.key == input::K_LEFT || e.key == input::K_RIGHT) {
            const auto opts = castOptions(s);
            const int at = std::find(opts.begin(), opts.end(), castSlot_) - opts.begin();
            const int to = at + (e.key == input::K_RIGHT ? 1 : -1);
            if (to >= 0 && to < (int)opts.size()) { castSlot_ = opts[to]; sfx::click(); }
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
        case input::K_LEFT: tab_ = nextTab(-1); sel_ = top_ = 0; sfx::click(); return true;
        case input::K_RIGHT: case input::K_TAB: tab_ = nextTab(1); sel_ = top_ = 0; sfx::click(); return true;
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
        toggleSkill(r->ref, e.base == 'e');
        return true;
    }
    if (r->kind == R_SPELL && e.base == 'x') {
        if (!confirmRemove_) { confirmRemove_ = true; toast("X again to forget this spell", C_BLOOD); return true; }
        confirmRemove_ = false;
        forgetSpell(r->ref);
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
            tv_.draw(u(6), HEADER_H + u(3), H - FOOTER_H - HEADER_H - u(5), ACCENT);
            drawFooter("Up/Dn scroll  Space page  Esc back");
            break;
    }
}

}  // namespace

App* makeCharactersApp() { return new CharactersApp(); }

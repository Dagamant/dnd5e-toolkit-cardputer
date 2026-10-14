#include "db_browser.h"
#include "../core/spellmath.h"

using namespace ui;

// Spell Library: SRD spells + custom spells (/gmtools/custom/spells.txt).

namespace {

const char* const CASTERS[] = {"All", "Bard", "Cleric", "Druid", "Paladin", "Ranger", "Sorcerer", "Warlock", "Wizard"};
const char* const SCHOOLS[] = {"Abjuration", "Conjuration", "Divination", "Enchantment", "Evocation",
                               "Illusion", "Necromancy", "Transmutation"};
const char* const ABILS[] = {"Strength", "Dexterity", "Constitution", "Intelligence", "Wisdom", "Charisma"};
const char* const DTYPES[] = {"Acid", "Bludgeoning", "Cold", "Fire", "Force", "Lightning", "Necrotic",
                              "Piercing", "Poison", "Psychic", "Radiant", "Slashing", "Thunder"};

uint16_t schoolColor(const char* s) {
    switch (s[0]) {
        case 'A': return rgb(120, 170, 255);  // abjuration
        case 'C': return rgb(230, 190, 90);   // conjuration
        case 'D': return rgb(160, 230, 230);  // divination
        case 'E': return s[1] == 'n' ? rgb(240, 140, 200) : rgb(255, 120, 80);  // enchantment / evocation
        case 'I': return rgb(190, 140, 255);  // illusion
        case 'N': return rgb(140, 220, 120);  // necromancy
        case 'T': return rgb(230, 230, 140);  // transmutation
        default: return C_TEXT;
    }
}

String levelText(int lvl, const String& school) {
    if (lvl == 0) return school + " cantrip";
    return String("Level ") + lvl + " " + school;
}

std::vector<String> list(const char* const* a, int n) {
    std::vector<String> v;
    for (int i = 0; i < n; i++) v.push_back(a[i]);
    return v;
}

class SpellsApp : public DbBrowser {
public:
    const char* name() const override { return "Spells"; }
    const char* blurb() const override { return "SRD spell library + your own."; }
    uint16_t accent() const override { return rgb(170, 140, 255); }

    void drawIcon(int cx, int cy, float t) override {
        // spellbook with a glowing rune
        gfx.fillRoundRect(cx - 16, cy - 18, 32, 36, 3, rgb(70, 40, 110));
        gfx.drawRoundRect(cx - 16, cy - 18, 32, 36, 3, C_GOLD);
        gfx.fillRect(cx - 16, cy - 18, 4, 36, rgb(50, 28, 80));
        float p = 0.5f + 0.5f * sinf(t * 3);
        uint16_t glow = rgb(140 + (int)(100 * p), 120 + (int)(80 * p), 255);
        gfx.drawCircle(cx + 2, cy, 9, glow);
        gfx.drawTriangle(cx + 2, cy - 8, cx - 5, cy + 5, cx + 9, cy + 5, glow);
    }

    void presetFilter(const String& f) override {
        for (int i = 0; i < 9; i++) if (f == CASTERS[i]) classF_ = i;
    }

protected:
    int levelF_ = -1, classF_ = 0;
    int slot_ = 0;   // chosen cast level in detail view

    const char* title() const override { return "Spells"; }
    const char* newLabel() const override { return "New spell"; }

    void openTable() override {
        table_.open({{"/gmtools/srd/spells", {"level", "school", "classes", "ritual", "conc"}}},
                    "/gmtools/custom/spells.txt", {"level", "school", "classes", "ritual", "conc"});
    }

    bool passFilter(int i) const override {
        if (levelF_ >= 0 && atoi(table_.col(i, "level")) != levelF_) return false;
        if (classF_ > 0 && !strstr(table_.col(i, "classes"), CASTERS[classF_])) return false;
        return true;
    }
    String filterLabel() const override {
        String s = CASTERS[classF_];
        s += levelF_ < 0 ? " / any lvl" : levelF_ == 0 ? " / cantrip" : String(" / lvl ") + levelF_;
        return s;
    }
    void cycleFilter(int d) override { classF_ = (classF_ + d + 9) % 9; }
    bool quickFilter(char c) override {
        int l = c - '0';
        levelF_ = levelF_ == l ? -1 : l;
        return true;
    }

    void drawRow(int i, int y, bool s) override {
        const char* school = table_.col(i, "school");
        int lvl = atoi(table_.col(i, "level"));
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.fillCircle(8, y + 4, 2, schoolColor(school));
        gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 210));
        String n = table_.at(i).name;
        if (n.length() > 27) n = n.substring(0, 26) + ".";
        gfx.drawString(n, 14, y + 1);
        gfx.setTextDatum(top_right);
        String tags;
        if (table_.isCustom(i)) tags += "*";
        if (*table_.col(i, "conc")) tags += "C";
        if (*table_.col(i, "ritual")) tags += "R";
        gfx.setTextColor(C_ICE);
        gfx.drawString(tags, W - 28, y + 1);
        gfx.setTextColor(lvl ? C_GOLD : C_DIM);
        gfx.drawString(lvl ? String(lvl) : String("C"), W - 8, y + 1);
    }

    void buildDetail() override {
        int lvl = cur_.num("level");
        slot_ = lvl;
        tv_.add(levelText(lvl, cur_.get("school")) + (cur_.has("ritual") ? " (ritual)" : ""), TextView::ACCENT);
        tv_.add(cur_.get("classes"), TextView::DIM);
        tv_.add(String("Time: ") + cur_.get("time") + "   Range: " + cur_.get("range"), TextView::BODY);
        tv_.add(String("Components: ") + cur_.get("components"), TextView::BODY);
        tv_.add(String("Duration: ") + cur_.get("duration"), cur_.has("conc") ? TextView::ACCENT : TextView::BODY);
        String roll;
        if (cur_.has("attack")) roll += String(cur_.get("attack")) + " spell attack  ";
        if (cur_.has("save")) roll += cur_.get("save") + " save  ";
        if (cur_.has("damage")) roll += cur_.get("damage") + " " + cur_.get("dtype");
        if (cur_.has("heal")) roll += "heals " + cur_.get("heal");
        roll.replace("+MOD", "+mod");
        if (roll.length()) tv_.add(roll, TextView::GOOD);
        tv_.gap();
        tv_.add(db::unescape(cur_.get("text")));
        if (cur_.has("higher")) { tv_.gap(); tv_.add(db::unescape(cur_.get("higher")), TextView::DIM); }
        // a summoned creature's stat block (Animate Objects, Find Steed, ...)
        if (cur_.has("creature")) { tv_.gap(); tv_.add(db::unescape(cur_.get("creature"))); }
    }

    bool canUpcast() const {
        return cur_.num("level") > 0 && spellmath::upcastDice(cur_.get("higher")).length() &&
               (cur_.has("damage") || cur_.has("heal"));
    }

    bool detailKey(const input::Event& e) override {
        int lvl = cur_.num("level");
        if (e.key == input::K_LEFT || e.key == input::K_RIGHT) {
            if (lvl == 0 && cur_.get("higher").startsWith("Cantrip")) {
                // cycle character-level tiers 1/5/11/17
                static const int tiers[] = {1, 5, 11, 17};
                int k = 0;
                for (int i = 0; i < 4; i++) if (tiers[i] == slot_) k = i;
                k = (k + (e.key == input::K_RIGHT ? 1 : 3)) % 4;
                slot_ = tiers[k];
            } else if (canUpcast()) {
                slot_ = std::max(lvl, std::min(9, slot_ + (e.key == input::K_RIGHT ? 1 : -1)));
            }
            sfx::click();
            return true;
        }
        return false;
    }

    void drawDetailBar(int y) override {
        int lvl = cur_.num("level");
        bool cantrip = lvl == 0 && cur_.get("higher").startsWith("Cantrip");
        if (!cantrip && !canUpcast()) return;
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        String s = cantrip ? String("< caster level ") + std::max(1, slot_) + " >" : String("< cast at level ") + slot_ + " >";
        String expr = expression();
        gfx.drawString(s, 6, y + 1);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_EMERALD);
        gfx.drawString(expr, W - 6, y + 1);
    }

    String expression() const {
        String base = cur_.has("damage") ? cur_.get("damage") : cur_.get("heal");
        int lvl = cur_.num("level");
        return spellmath::castExpr(base, cur_.get("higher"), lvl, std::max(lvl, slot_), std::max(1, slot_), 0);
    }

    void rollCurrent() override {
        if (!cur_.has("damage") && !cur_.has("heal")) {
            if (cur_.has("attack")) { rollui::show(cur_.name(), {{"Spell attack", "d20", "+ your bonus"}}); return; }
            toast("Nothing to roll", C_DIM);
            return;
        }
        std::vector<rollui::Line> lines;
        if (cur_.has("attack")) lines.push_back({"Spell attack", "d20", "+bonus"});
        lines.push_back({cur_.has("damage") ? "Damage" : "Healing", expression(),
                         cur_.has("damage") ? cur_.get("dtype") : String("HP")});
        String t = cur_.name();
        if (cur_.num("level") && slot_ > cur_.num("level")) t += String(" @") + slot_;
        rollui::show(t, lines);
    }

    // ---- creation wizard ----
    std::vector<Wizard::Step> wizardSteps() override {
        using W_ = Wizard;
        std::vector<String> levels = {"Cantrip", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        std::vector<String> classes;
        for (int i = 1; i < 9; i++) classes.push_back(CASTERS[i]);
        auto has = [](const char* k, const char* v) { return [=](const Wizard& w) { return w.is(k, v); }; };
        return {
            {"name", "Spell name", W_::TEXT, {}, "", false, 30},
            {"level", "Level", W_::CHOICE, levels},
            {"school", "School", W_::CHOICE, list(SCHOOLS, 8)},
            {"classes", "Spell lists", W_::MULTI, classes, "Which classes can learn it?"},
            {"time", "Casting time", W_::CHOICE, {"Action", "Bonus Action", "Reaction", "1 minute", "10 minutes", "1 hour", "Other..."}},
            {"ritual", "Ritual?", W_::YESNO},
            {"range", "Range", W_::CHOICE, {"Self", "Touch", "30 feet", "60 feet", "90 feet", "120 feet", "150 feet", "300 feet", "1 mile", "Sight", "Other..."}},
            {"comp", "Components", W_::MULTI, {"V", "S", "M"}},
            {"material", "Material component", W_::TEXT, {}, "e.g. a pinch of sulfur", false, 40,
             [](const Wizard& w) { return w.contains("comp", "M"); }},
            {"duration", "Duration", W_::CHOICE, {"Instantaneous", "1 round", "1 minute", "10 minutes", "1 hour", "8 hours", "24 hours", "Until dispelled", "Other..."}},
            {"conc", "Concentration?", W_::YESNO, {}, "", false, 32,
             [](const Wizard& w) { return !w.is("duration", "Instantaneous"); }},
            {"effect", "How does it resolve?", W_::CHOICE, {"Spell attack (ranged)", "Spell attack (melee)", "Saving throw", "Healing", "No roll (utility)"}},
            {"save", "Saving throw", W_::CHOICE, list(ABILS, 6), "", false, 32, has("effect", "Saving throw")},
            {"damage", "Damage dice", W_::DICE, {}, "e.g. 3d8. Blank if it deals none.", true, 16,
             [](const Wizard& w) { return w.get("effect").startsWith("Spell attack") || w.is("effect", "Saving throw"); }},
            {"dtype", "Damage type", W_::CHOICE, list(DTYPES, 13), "", false, 32,
             [](const Wizard& w) { return w.has("damage"); }},
            {"heal", "Healing dice", W_::DICE, {}, "e.g. 2d8. Add +MOD for the caster's modifier: 2d8+MOD", false, 16, has("effect", "Healing")},
            {"upcast", "Extra dice per slot level", W_::DICE, {}, "Cast with a higher slot adds this per level, e.g. 1d6. Blank = none.", true, 10,
             [](const Wizard& w) { return !w.is("level", "Cantrip") && (w.has("damage") || w.has("heal")); }},
            {"text", "Description", W_::LONGTEXT, {}, "What the spell does.", false},
        };
    }

    void presetWizard(const db::Rec& r) override {
        wiz_.preset("name", r.name());
        wiz_.preset("level", r.num("level") ? String(r.num("level")) : String("Cantrip"));
        wiz_.preset("school", r.get("school"));
        wiz_.preset("classes", r.get("classes"));
        wiz_.preset("time", r.get("time"));
        wiz_.preset("ritual", r.has("ritual") ? "Yes" : "No");
        wiz_.preset("range", r.get("range"));
        String comp = r.get("components");
        int paren = comp.indexOf('(');
        if (paren > 0) {
            wiz_.preset("material", comp.substring(paren + 1, comp.lastIndexOf(')')));
            comp = comp.substring(0, paren);
        }
        comp.trim();
        wiz_.preset("comp", comp);
        String dur = r.get("duration");
        if (dur.startsWith("Concentration, up to ")) dur = dur.substring(21);
        wiz_.preset("duration", dur);
        wiz_.preset("conc", r.has("conc") ? "Yes" : "No");
        String eff = r.has("heal") ? "Healing" : r.has("save") ? "Saving throw" :
                     r.get("attack") == "melee" ? "Spell attack (melee)" : r.has("attack") ? "Spell attack (ranged)" : "No roll (utility)";
        wiz_.preset("effect", eff);
        wiz_.preset("save", r.get("save"));
        wiz_.preset("damage", r.get("damage"));
        wiz_.preset("dtype", r.get("dtype"));
        wiz_.preset("heal", r.get("heal"));
        wiz_.preset("upcast", spellmath::upcastDice(r.get("higher")));
        wiz_.preset("text", db::unescape(r.get("text")));
    }

    db::Rec fromWizard() override {
        db::Rec r;
        const Wizard& w = wiz_;
        r.set("name", w.get("name"));
        int lvl = w.is("level", "Cantrip") ? 0 : w.get("level").toInt();
        r.set("level", String(lvl));
        r.set("school", w.get("school"));
        r.set("classes", w.get("classes"));
        r.set("time", w.get("time"));
        if (w.yes("ritual")) r.set("ritual", "1");
        r.set("range", w.get("range"));
        String comp = w.get("comp");
        if (w.contains("comp", "M") && w.has("material")) comp += " (" + w.get("material") + ")";
        r.set("components", comp);
        bool conc = w.yes("conc") && !w.is("duration", "Instantaneous");
        r.set("duration", conc ? "Concentration, up to " + w.get("duration") : w.get("duration"));
        if (conc) r.set("conc", "1");
        String eff = w.get("effect");
        if (eff == "Spell attack (ranged)") r.set("attack", "ranged");
        if (eff == "Spell attack (melee)") r.set("attack", "melee");
        if (eff == "Saving throw") r.set("save", w.get("save"));
        if (w.has("damage") && eff != "Healing" && eff != "No roll (utility)") { r.set("damage", w.get("damage")); r.set("dtype", w.get("dtype")); }
        if (eff == "Healing") r.set("heal", w.get("heal"));
        r.set("text", w.get("text"));
        if (w.has("upcast") && lvl > 0)
            r.set("higher", "Using a Higher-Level Spell Slot. The " + String(eff == "Healing" ? "healing" : "damage") +
                                " increases by " + w.get("upcast") + " for each spell slot level above " + lvl + ".");
        return r;
    }
};

}  // namespace

App* makeSpellsApp() { return new SpellsApp(); }
DbBrowser* newSpellsBrowser() { return new SpellsApp(); }

#include "char_builder.h"
#include "../core/dice_expr.h"
#include "../core/rng.h"
#include "../core/storage.h"
#include "../core/ui.h"

using namespace dnd;

namespace {

const char* const ALIGNMENTS[] = {"Lawful Good", "Neutral Good", "Chaotic Good", "Lawful Neutral", "Neutral",
                                  "Chaotic Neutral", "Lawful Evil", "Neutral Evil", "Chaotic Evil", "Unaligned"};
const char* const ABIL_KEYS[6] = {"s_str", "s_dex", "s_con", "s_int", "s_wis", "s_cha"};
const int ARRAY[6] = {15, 14, 13, 12, 10, 8};

int pointCost(int v) { return v <= 13 ? v - 8 : v == 14 ? 7 : 9; }

// Species choices that the SRD presents as tables/lists.
struct SpeciesOpt { const char* species; const char* label; std::vector<String> opts; };
const std::vector<SpeciesOpt>& speciesOpts() {
    static const std::vector<SpeciesOpt> v = {
        {"Dragonborn", "Draconic ancestry", {"Black (Acid)", "Blue (Lightning)", "Brass (Fire)", "Bronze (Lightning)",
                                            "Copper (Acid)", "Gold (Fire)", "Green (Poison)", "Red (Fire)", "Silver (Cold)", "White (Cold)"}},
        {"Elf", "Elven lineage", {"Drow", "High Elf", "Wood Elf"}},
        {"Gnome", "Gnomish lineage", {"Forest Gnome", "Rock Gnome"}},
        {"Goliath", "Giant ancestry", {"Cloud's Jaunt", "Fire's Burn", "Frost's Chill", "Hill's Tumble", "Stone's Endurance", "Storm's Thunder"}},
        {"Tiefling", "Fiendish legacy", {"Abyssal", "Chthonic", "Infernal"}},
    };
    return v;
}
const SpeciesOpt* speciesOpt(const String& s) {
    for (auto& o : speciesOpts()) if (s == o.species) return &o;
    return nullptr;
}

std::vector<String> allSkills() {
    std::vector<String> v;
    for (auto& s : SKILLS) v.push_back(s.name);
    return v;
}

std::vector<String> commaList(const String& s) { return splitList(s, ','); }

String stripParen(const String& s) {
    int p = s.indexOf(" (");
    return p > 0 ? s.substring(0, p) : s;
}

// "Magic Initiate (Cleric)" -> "Cleric"
String parenOf(const String& s) {
    int a = s.indexOf('('), b = s.indexOf(')');
    return a >= 0 && b > a ? s.substring(a + 1, b) : String("");
}

}  // namespace

// ---------------------------------------------------------------- data
static bool loadAll(const char* base, std::vector<db::Rec>& out) {
    out.clear();
    String idx;
    if (!storage::readText((String(base) + ".idx").c_str(), idx, 8192)) return false;
    String dataPath = String(base) + ".txt";
    for (auto& line : splitList(idx, '\n')) {
        db::Rec r;
        if (db::readRecordAt(dataPath.c_str(), line.toInt(), r)) out.push_back(r);
    }
    return !out.empty();
}

bool CharBuilder::loadSrd() {
    bool ok = loadAll("/gmtools/srd/classes", classes_) && loadAll("/gmtools/srd/backgrounds", backgrounds_) &&
              loadAll("/gmtools/srd/species", species_) && loadAll("/gmtools/srd/feats", feats_);
    // strip bulky fields we don't need for building
    for (auto& r : feats_) r.set("text", "");
    subclassNames_.clear();
    subclassOf_.clear();
    String idx;
    if (storage::readText("/gmtools/srd/subclasses.idx", idx, 2048)) {
        for (auto& line : splitList(idx, '\n')) {
            auto f = splitList(line, '\t');
            if (f.size() >= 3) { subclassNames_.push_back(f[1]); subclassOf_.push_back(f[2]); }
        }
    }
    spells_.open({{"/gmtools/srd/spells", {"level", "school", "classes", "ritual", "conc"}}},
                 "/gmtools/custom/spells.txt", {"level", "classes"});
    return ok;
}

void CharBuilder::unload() {
    classes_.clear(); backgrounds_.clear(); species_.clear(); feats_.clear();
    classes_.shrink_to_fit(); backgrounds_.shrink_to_fit(); species_.shrink_to_fit(); feats_.shrink_to_fit();
    spells_.close();
}

const db::Rec* CharBuilder::cls(const String& n) const { for (auto& r : classes_) if (r.name() == n) return &r; return nullptr; }
const db::Rec* CharBuilder::bg(const String& n) const { for (auto& r : backgrounds_) if (r.name() == n) return &r; return nullptr; }
const db::Rec* CharBuilder::sp(const String& n) const { for (auto& r : species_) if (r.name() == n) return &r; return nullptr; }
const db::Rec* CharBuilder::feat(const String& n) const { for (auto& r : feats_) if (r.name() == n) return &r; return nullptr; }

std::vector<String> CharBuilder::spellNames(const String& c, int lo, int hi) const {
    std::vector<String> v;
    for (int i = 0; i < spells_.size(); i++) {
        int l = atoi(spells_.col(i, "level"));
        if (l < lo || l > hi) continue;
        if (!strstr(spells_.col(i, "classes"), c.c_str())) continue;
        v.push_back(spells_.at(i).name);
    }
    return v;
}

// ---------------------------------------------------------------- creation
std::vector<Wizard::Step> CharBuilder::creationSteps() {
    using W_ = Wizard;
    std::vector<String> clsNames, bgNames, spNames, align;
    for (auto& r : classes_) clsNames.push_back(r.name());
    for (auto& r : backgrounds_) bgNames.push_back(r.name());
    for (auto& r : species_) spNames.push_back(r.name());
    for (auto a : ALIGNMENTS) align.push_back(a);

    auto clsPreview = [this](const String& n) -> String {
        const db::Rec* r = cls(n);
        if (!r) return String("");
        return String("Hit die ") + r->get("hitdie") + "\nPrimary: " + r->get("primary") + "\nSaves: " + r->get("saves") +
               "\nSkills: " + r->get("skills_n") + " of " + r->get("skills") + "\nArmor: " + r->get("armor") +
               "\nWeapons: " + r->get("weapons") + (r->has("spell_ability") ? "\nCasts with " + r->get("spell_ability") : String(""));
    };
    auto bgPreview = [this](const String& n) -> String {
        const db::Rec* r = bg(n);
        if (!r) return String("");
        return String("Abilities: ") + r->get("abilities") + "\nFeat: " + r->get("feat") + "\nSkills: " + r->get("skills") +
               "\nTool: " + r->get("tool");
    };
    auto spPreview = [this](const String& n) -> String {
        const db::Rec* r = sp(n);
        if (!r) return String("");
        String s = String("Speed ") + r->get("speed") + " ft\n" + stripParen(r->get("size")) + "\n";
        for (int i = 1; i < 10; i++) {
            String t = r->get((String("trait") + i).c_str());
            if (!t.length()) break;
            s += "- " + t.substring(0, t.indexOf(':')) + "\n";
        }
        return s;
    };

    // ability score assignment helpers
    auto poolFor = [](const Wizard& w) {
        std::vector<int> pool;
        if (w.get("method").startsWith("Standard")) for (int v : ARRAY) pool.push_back(v);
        else for (auto& s : splitList(w.get("pool"), ',')) pool.push_back(s.toInt());
        return pool;
    };
    std::vector<W_::Step> abilitySteps;
    for (int a = 0; a < 6; a++) {
        W_::Step s;
        s.key = ABIL_KEYS[a];
        s.label = ABIL_NAMES[a];
        s.type = W_::CHOICE;
        s.when = [](const Wizard& w) { return !w.get("method").startsWith("Enter"); };
        s.dynOptions = [a, poolFor](const Wizard& w) {
            std::vector<String> o;
            if (w.get("method").startsWith("Point")) {
                int spent = 0;
                for (int b = 0; b < a; b++) spent += pointCost(w.get(ABIL_KEYS[b]).toInt());
                for (int v = 15; v >= 8; v--) if (spent + pointCost(v) <= 27) o.push_back(String(v));
                return o;
            }
            std::vector<int> pool = poolFor(w);
            for (int b = 0; b < a; b++) {   // remove values used by earlier abilities
                int used = w.get(ABIL_KEYS[b]).toInt();
                for (size_t k = 0; k < pool.size(); k++) if (pool[k] == used) { pool.erase(pool.begin() + k); break; }
            }
            for (int v : pool) o.push_back(String(v));
            return o;
        };
        abilitySteps.push_back(s);
        W_::Step m;
        m.key = ABIL_KEYS[a];
        m.label = ABIL_NAMES[a];
        m.type = W_::NUMBER;
        m.maxLen = 2;
        m.hint = "3 to 20";
        m.when = [](const Wizard& w) { return w.get("method").startsWith("Enter"); };
        m.validate = [a](const Wizard& w) {
            int v = w.get(ABIL_KEYS[a]).toInt();
            return v < 3 || v > 20 ? String("Must be 3-20") : String("");
        };
        abilitySteps.push_back(m);
    }

    std::vector<W_::Step> steps;
    steps.push_back({"name", "Character name", W_::TEXT, {}, "", false, 24});
    {
        W_::Step s{"class", "Class", W_::CHOICE, clsNames};
        s.preview = clsPreview;
        steps.push_back(s);
    }
    {
        W_::Step s{"background", "Background", W_::CHOICE, bgNames, "Gives ability boosts, an origin feat, 2 skills, a tool."};
        s.preview = bgPreview;
        steps.push_back(s);
    }
    {
        W_::Step s{"species", "Species", W_::CHOICE, spNames};
        s.preview = spPreview;
        steps.push_back(s);
    }
    {
        W_::Step s{"species_opt", "Species option", W_::CHOICE};
        s.when = [](const Wizard& w) { return speciesOpt(w.get("species")) != nullptr; };
        s.dynOptions = [](const Wizard& w) { auto o = speciesOpt(w.get("species")); return o ? o->opts : std::vector<String>{}; };
        steps.push_back(s);
    }
    {
        // Elf, Gnome and Tiefling cast their lineage's spells with one of these
        W_::Step s{"species_ability", "Species spellcasting ability", W_::CHOICE, {"Intelligence", "Wisdom", "Charisma"}};
        s.when = [](const Wizard& w) { return w.is("species", "Elf") || w.is("species", "Gnome") || w.is("species", "Tiefling"); };
        steps.push_back(s);
    }
    {
        W_::Step s{"keen", "Keen Senses skill", W_::CHOICE, {"Insight", "Perception", "Survival"}};
        s.when = [](const Wizard& w) { return w.is("species", "Elf"); };
        steps.push_back(s);
    }
    {
        W_::Step s{"human_skill", "Skillful: any skill", W_::CHOICE, allSkills(), "Humans gain one extra skill."};
        s.when = [](const Wizard& w) { return w.is("species", "Human"); };
        steps.push_back(s);
    }
    {
        W_::Step s{"versatile", "Versatile: origin feat", W_::CHOICE, {}, "Humans gain an extra origin feat."};
        s.when = [](const Wizard& w) { return w.is("species", "Human"); };
        s.dynOptions = [this](const Wizard&) {
            std::vector<String> o;
            for (auto& f : feats_) if (f.get("category") == "Origin") o.push_back(f.name());
            return o;
        };
        steps.push_back(s);
    }
    {
        W_::Step s{"method", "Ability scores", W_::CHOICE,
                   {"Standard array (15 14 13 12 10 8)", "Point buy (27 points)", "Roll 4d6, drop lowest", "Enter manually"}};
        s.hint = "Next you'll assign a score to each ability.";
        s.onCommit = [](Wizard& w) {
            if (w.get("method").startsWith("Roll") && !w.has("pool")) {
                String p;
                for (int i = 0; i < 6; i++) p += String(i ? "," : "") + dice::roll("4d6kh3").total;
                w.set("pool", p);
                ui::toast((String("Rolled ") + p).c_str(), ui::C_GOLD);
            }
        };
        steps.push_back(s);
    }
    for (auto& s : abilitySteps) {
        // remind the player what their class wants
        steps.push_back(s);
    }
    {
        W_::Step s{"bg_asi", "Background boosts", W_::CHOICE};
        s.dynOptions = [this](const Wizard& w) {
            std::vector<String> o;
            const db::Rec* r = bg(w.get("background"));
            if (!r) return o;
            auto a = commaList(r->get("abilities"));
            if (a.size() != 3) return o;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    if (i != j) o.push_back("+2 " + a[i] + ", +1 " + a[j]);
            o.push_back("+1 " + a[0] + ", " + a[1] + ", " + a[2]);
            return o;
        };
        steps.push_back(s);
    }
    {
        W_::Step s{"skills", "Class skills", W_::MULTI};
        s.hint = "Your background skills are already included.";
        s.dynOptions = [this](const Wizard& w) {
            std::vector<String> o;
            const db::Rec* c = cls(w.get("class"));
            const db::Rec* b = bg(w.get("background"));
            if (!c) return o;
            auto bgs = b ? commaList(b->get("skills")) : std::vector<String>{};
            for (auto& sk : commaList(c->get("skills")))
                if (!listHas(bgs, sk) && sk != w.get("human_skill") && sk != w.get("keen")) o.push_back(sk);
            return o;
        };
        s.dynPick = [this](const Wizard& w) { const db::Rec* c = cls(w.get("class")); return c ? c->num("skills_n") : 0; };
        steps.push_back(s);
    }
    {
        W_::Step s{"skilled", "Skilled feat: 3 skills", W_::MULTI, allSkills()};
        s.pick = 3;
        s.when = [](const Wizard& w) { return w.is("versatile", "Skilled"); };
        steps.push_back(s);
    }
    {
        W_::Step s{"expertise", "Expertise (2 skills)", W_::MULTI};
        s.pick = 2;
        s.hint = "Double proficiency bonus on these.";
        s.when = [](const Wizard& w) { return w.is("class", "Rogue"); };
        s.dynOptions = [this](const Wizard& w) {
            std::vector<String> o = commaList(w.get("skills"));
            const db::Rec* b = bg(w.get("background"));
            if (b) for (auto& x : commaList(b->get("skills"))) o.push_back(x);
            return o;
        };
        steps.push_back(s);
    }
    {
        W_::Step s{"fstyle", "Fighting style", W_::CHOICE};
        s.when = [](const Wizard& w) { return w.is("class", "Fighter"); };
        s.dynOptions = [this](const Wizard&) {
            std::vector<String> o;
            for (auto& f : feats_) if (f.get("category") == "Fighting Style") o.push_back(f.name());
            return o;
        };
        steps.push_back(s);
    }
    {
        W_::Step s{"order", "Divine Order", W_::CHOICE, {"Protector (Martial weapons, Heavy armor)", "Thaumaturge (extra cantrip)"}};
        s.when = [](const Wizard& w) { return w.is("class", "Cleric"); };
        steps.push_back(s);
        W_::Step d{"order", "Primal Order", W_::CHOICE, {"Warden (Martial weapons, Medium armor)", "Magician (extra cantrip)"}};
        d.when = [](const Wizard& w) { return w.is("class", "Druid"); };
        steps.push_back(d);
    }
    // ---- spells
    auto extraCantrip = [](const Wizard& w) { return w.get("order").startsWith("Thaumaturge") || w.get("order").startsWith("Magician") ? 1 : 0; };
    {
        W_::Step s{"cantrips", "Cantrips", W_::MULTI};
        s.when = [this](const Wizard& w) { const db::Rec* c = cls(w.get("class")); return c && c->has("spell_ability") && splitList(c->get("L1"), '|').size() > 0 && w.has("class") &&
                                           [&]() { ClassInfo ci; ci.rec = *c; ci.cols = splitList(c->get("cols"), '|'); return ci.num(1, "Cantrips") > 0; }(); };
        s.dynOptions = [this](const Wizard& w) { return spellNames(w.get("class"), 0, 0); };
        s.dynPick = [this, extraCantrip](const Wizard& w) {
            const db::Rec* c = cls(w.get("class"));
            if (!c) return 0;
            ClassInfo ci; ci.rec = *c; ci.cols = splitList(c->get("cols"), '|');
            return ci.num(1, "Cantrips") + extraCantrip(w);
        };
        steps.push_back(s);
    }
    {
        W_::Step s{"spells1", "Level 1 spells", W_::MULTI};
        s.hint = "Spells you have prepared.";
        s.when = [this](const Wizard& w) {
            const db::Rec* c = cls(w.get("class"));
            if (!c || !c->has("spell_ability")) return false;
            ClassInfo ci; ci.rec = *c; ci.cols = splitList(c->get("cols"), '|');
            return ci.num(1, "Prepared Spells") > 0;
        };
        s.dynOptions = [this](const Wizard& w) { return spellNames(w.get("class"), 1, 1); };
        s.dynPick = [this](const Wizard& w) {
            const db::Rec* c = cls(w.get("class"));
            if (!c) return 0;
            ClassInfo ci; ci.rec = *c; ci.cols = splitList(c->get("cols"), '|');
            return ci.num(1, "Prepared Spells");
        };
        steps.push_back(s);
    }
    // Magic Initiate (from background, or Human Versatile)
    auto hasMI = [this](const Wizard& w) {
        const db::Rec* b = bg(w.get("background"));
        return (b && b->get("feat").startsWith("Magic Initiate")) || w.is("versatile", "Magic Initiate");
    };
    auto miList = [this](const Wizard& w) {
        const db::Rec* b = bg(w.get("background"));
        if (b && b->get("feat").startsWith("Magic Initiate")) return parenOf(b->get("feat"));
        return w.get("mi_list");
    };
    {
        W_::Step s{"mi_list", "Magic Initiate list", W_::CHOICE, {"Cleric", "Druid", "Wizard"}};
        s.when = [this](const Wizard& w) {
            const db::Rec* b = bg(w.get("background"));
            return w.is("versatile", "Magic Initiate") && !(b && b->get("feat").startsWith("Magic Initiate"));
        };
        steps.push_back(s);
    }
    {
        W_::Step s{"mi_cantrips", "Magic Initiate cantrips", W_::MULTI};
        s.pick = 2;
        s.when = hasMI;
        s.dynOptions = [this, miList](const Wizard& w) {
            std::vector<String> o;
            auto known = commaList(w.get("cantrips"));
            for (auto& n : spellNames(miList(w), 0, 0)) if (!listHas(known, n)) o.push_back(n);
            return o;
        };
        steps.push_back(s);
        W_::Step l{"mi_spell", "Magic Initiate spell", W_::CHOICE};
        l.hint = "A level 1 spell, castable once per long rest for free.";
        l.when = hasMI;
        l.dynOptions = [this, miList](const Wizard& w) {
            std::vector<String> o;
            auto known = commaList(w.get("spells1"));
            for (auto& n : spellNames(miList(w), 1, 1)) if (!listHas(known, n)) o.push_back(n);
            return o;
        };
        steps.push_back(l);
        W_::Step a{"mi_ability", "Magic Initiate ability", W_::CHOICE, {"Intelligence", "Wisdom", "Charisma"}};
        a.when = hasMI;
        steps.push_back(a);
    }
    // ---- equipment
    {
        W_::Step s{"cls_equip", "Class equipment", W_::CHOICE};
        s.dynOptions = [this](const Wizard& w) {
            std::vector<String> o;
            const db::Rec* c = cls(w.get("class"));
            if (!c) return o;
            for (const char* k : {"equip_a", "equip_b", "equip_c"}) {
                if (!c->has(k)) continue;
                String v = c->get(k);
                v.replace("|", ", ");
                o.push_back(String((char)toupper(k[6])) + ": " + v);
            }
            return o;
        };
        s.preview = [](const String& o) { return o; };
        steps.push_back(s);
    }
    {
        W_::Step s{"bg_equip", "Background equipment", W_::CHOICE};
        s.dynOptions = [this](const Wizard& w) {
            std::vector<String> o;
            const db::Rec* b = bg(w.get("background"));
            if (!b) return o;
            for (const char* k : {"equip_a", "equip_b"}) {
                if (!b->has(k)) continue;
                String v = b->get(k);
                v.replace("|", ", ");
                o.push_back(String((char)toupper(k[6])) + ": " + v);
            }
            return o;
        };
        s.preview = [](const String& o) { return o; };
        steps.push_back(s);
    }
    steps.push_back({"alignment", "Alignment", W_::CHOICE, align});
    return steps;
}

void CharBuilder::addStartingItems(Character& c, const String& choice) {
    // choice is "A: Greataxe, 4 Handaxes, Explorer's Pack, 15 GP"
    String list = choice.substring(choice.indexOf(':') + 1);
    db::Table items;
    items.open({{"/gmtools/srd/items", {"kind", "category", "cost", "weight"}},
                {"/gmtools/srd/magic", {"kind", "type", "rarity", "attune"}}},
               "/gmtools/custom/items.txt", {"kind"});
    for (auto entry : splitList(list, ',')) {
        entry.trim();
        int qty = 1;
        int sp = entry.indexOf(' ');
        if (sp > 0 && entry.substring(0, sp).toInt() > 0) { qty = entry.substring(0, sp).toInt(); entry = entry.substring(sp + 1); }
        if (entry == "GP" || entry.endsWith(" GP") || entry == "GP") {
            c.coins[3] += qty;
            continue;
        }
        String name = entry;
        int idx = items.find(name);
        String base = stripParen(name);
        if (idx < 0) idx = items.find(base);
        if (idx < 0 && name.endsWith("es")) idx = items.find(name.substring(0, name.length() - 2));
        if (idx < 0 && name.endsWith("s")) idx = items.find(name.substring(0, name.length() - 1));
        Item it;
        db::Rec r;
        if (idx >= 0 && items.read(idx, r)) {
            it = Item::fromRec(r, qty);
            if (name != r.name() && name.indexOf('(') > 0) it.name = name;  // keep "Arcane Focus (crystal)"
        } else {
            it.name = name;
            it.qty = qty;
            it.kind = "gear";
        }
        // merge duplicates
        bool merged = false;
        for (auto& e : c.items) if (e.name == it.name) { e.qty += it.qty; merged = true; break; }
        if (!merged) c.items.push_back(it);
    }
    items.close();
}

bool CharBuilder::applyCreation(const Wizard& w, Character& c) {
    const db::Rec* cr = cls(w.get("class"));
    const db::Rec* br = bg(w.get("background"));
    const db::Rec* sr = sp(w.get("species"));
    if (!cr || !br || !sr) return false;
    c = Character();
    c.name = w.get("name");
    c.cls = cr->name();
    c.background = br->name();
    c.species = sr->name();
    c.speciesOpt = w.get("species_opt");
    c.spAbility = w.get("species_ability");
    c.alignment = w.get("alignment");
    c.level = 1;
    for (int a = 0; a < 6; a++) c.score[a] = w.get(ABIL_KEYS[a]).toInt();
    // background boosts: "+2 Wisdom, +1 Charisma" or "+1 A, B, C"
    String boost = w.get("bg_asi");
    if (boost.startsWith("+2")) {
        auto parts = splitList(boost, ',');
        c.score[abilIndex(parts[0].substring(3))] += 2;
        if (parts.size() > 1) { String p = parts[1]; p.trim(); c.score[abilIndex(p.substring(3))] += 1; }
    } else {
        for (auto& a : commaList(br->get("abilities"))) { int i = abilIndex(a); if (i >= 0) c.score[i] += 1; }
    }
    for (int a = 0; a < 6; a++) c.score[a] = std::min(20, c.score[a]);

    c.saveProf = commaList(cr->get("saves"));
    c.weapons = cr->get("weapons");
    c.armor = cr->get("armor");
    String order = w.get("order");
    if (order.startsWith("Protector")) { c.weapons = "Simple and Martial weapons"; c.armor = "All armor and Shields"; }
    if (order.startsWith("Warden")) { c.weapons = "Simple and Martial weapons"; c.armor = "Light and Medium armor and Shields"; }

    c.skillProf = commaList(br->get("skills"));
    for (auto& s : commaList(w.get("skills"))) if (!listHas(c.skillProf, s)) c.skillProf.push_back(s);
    for (const char* k : {"human_skill", "keen"}) if (w.has(k) && !listHas(c.skillProf, w.get(k))) c.skillProf.push_back(w.get(k));
    for (auto& s : commaList(w.get("skilled"))) if (!listHas(c.skillProf, s)) c.skillProf.push_back(s);
    c.expertise = commaList(w.get("expertise"));
    c.toolProf.clear();
    if (br->has("tool")) c.toolProf.push_back(br->get("tool"));
    if (cr->has("tools")) c.toolProf.push_back(cr->get("tools"));

    c.feats.clear();
    c.feats.push_back(stripParen(br->get("feat")));
    if (w.has("versatile") && !listHas(c.feats, w.get("versatile"))) c.feats.push_back(w.get("versatile"));
    if (w.has("fstyle")) c.feats.push_back(w.get("fstyle"));
    if (order.length()) c.feats.push_back((c.cls == "Cleric" ? "Divine Order: " : "Primal Order: ") + stripParen(order));

    c.spells = commaList(w.get("cantrips"));
    for (auto& s : commaList(w.get("spells1"))) c.spells.push_back(s);
    if (w.has("mi_spell")) {
        c.miSpells = commaList(w.get("mi_cantrips"));
        c.miSpells.push_back(w.get("mi_spell"));
        c.miAbility = w.get("mi_ability");
    }

    c.speed = sr->num("speed", 30);
    c.size = stripParen(sr->get("size"));
    if (c.size.indexOf(" or ") > 0) c.size = "Medium";
    c.darkvision = sr->num("darkvision");
    if (c.species == "Elf" && c.speciesOpt == "Drow") c.darkvision = 120;
    if (c.species == "Elf" && c.speciesOpt == "Wood Elf") c.speed = 35;

    c.ci.load(c.cls);
    c.hpMax = c.hp = std::max(1, c.ci.hitDie() + c.mod(CON) + (c.species == "Dwarf" ? 1 : 0));

    addStartingItems(c, w.get("cls_equip"));
    addStartingItems(c, w.get("bg_equip"));
    // equip the first armor, shield and weapons
    bool armorOn = false, shieldOn = false;
    for (auto& it : c.items) {
        if (it.isArmor() && !armorOn) { it.equipped = true; armorOn = true; }
        else if (it.isShield() && !shieldOn) { it.equipped = true; shieldOn = true; }
        else if (it.isWeapon()) it.equipped = true;
    }
    c.syncResources();
    c.path = Character::pathFor(c.name);
    return c.save();
}

// ---------------------------------------------------------------- level up
std::vector<String> CharBuilder::featuresAt(const Character& c, int level, const String& sub) {
    std::vector<String> out;
    String idx;
    if (!storage::readText("/gmtools/srd/features.idx", idx, 16384)) return out;
    for (auto& line : splitList(idx, '\n')) {
        auto f = splitList(line, '\t');
        // offset, name, class, [subclass], level  (subclass may be empty -> 4 fields)
        if (f.size() < 4) continue;
        String fcls = f[2];
        String fsub = f.size() >= 5 ? f[3] : String("");
        int flvl = f.back().toInt();
        if (fcls != c.cls || flvl != level) continue;
        if (fsub.length() && fsub != sub) continue;
        out.push_back(f[1]);
    }
    return out;
}

std::vector<Wizard::Step> CharBuilder::levelUpSteps(const Character& c) {
    using W_ = Wizard;
    lvl_ = &c;
    lvlCi_ = c.ci;
    int nl = c.level + 1;
    std::vector<W_::Step> steps;
    // what's new at this level (shown on the first step)
    String sub = c.subclass;
    auto feats = featuresAt(c, nl, sub);
    String gained;
    for (auto& f : feats) if (f != "Ability Score Improvement" && f != "Epic Boon" && !f.endsWith("Subclass"))
        gained += String(gained.length() ? ", " : "") + f;
    int die = c.hitDie(), avg = die / 2 + 1, con = c.mod(CON);
    {
        W_::Step s{"hp", String("Level ") + nl + ": hit points", W_::CHOICE,
                   {String("Take average: ") + avg + fmtMod(con), String("Roll 1d") + die + fmtMod(con)}};
        s.hint = gained.length() ? "New: " + gained : String("Hit points for the new level.");
        steps.push_back(s);
    }
    if (nl == 3 && !c.subclass.length()) {
        std::vector<String> subs;
        for (size_t i = 0; i < subclassNames_.size(); i++) if (subclassOf_[i] == c.cls) subs.push_back(subclassNames_[i]);
        subs.push_back("Other...");
        W_::Step s{"subclass", String(c.cls) + " subclass", W_::CHOICE, subs};
        steps.push_back(s);
    }
    bool asi = false, boon = false;
    for (auto& f : feats) { if (f == "Ability Score Improvement") asi = true; if (f == "Epic Boon") boon = true; }
    if (asi) {
        std::vector<String> abil;
        for (int a = 0; a < 6; a++) abil.push_back(String(ABIL_NAMES[a]) + " (" + c.score[a] + ")");
        steps.push_back({"asi_type", "Ability Score Improvement", W_::CHOICE, {"+2 to one ability", "+1 to two abilities", "Take a feat instead"}});
        W_::Step a1{"asi1", "Increase which ability?", W_::CHOICE, abil};
        a1.when = [](const Wizard& w) { return !w.get("asi_type").startsWith("Take"); };
        a1.validate = [&c](const Wizard& w) {
            int i = abilIndex(w.get("asi1").substring(0, w.get("asi1").indexOf(' ')));
            int add = w.get("asi_type").startsWith("+2") ? 2 : 1;
            return i >= 0 && c.score[i] + add > 20 ? String("Can't go above 20") : String("");
        };
        steps.push_back(a1);
        W_::Step a2{"asi2", "And which second ability?", W_::CHOICE, abil};
        a2.when = [](const Wizard& w) { return w.get("asi_type").startsWith("+1"); };
        a2.validate = [&c](const Wizard& w) {
            if (w.get("asi1") == w.get("asi2")) return String("Pick a different ability");
            int i = abilIndex(w.get("asi2").substring(0, w.get("asi2").indexOf(' ')));
            return i >= 0 && c.score[i] + 1 > 20 ? String("Can't go above 20") : String("");
        };
        steps.push_back(a2);
        W_::Step f{"feat", "Choose a feat", W_::CHOICE};
        f.when = [](const Wizard& w) { return w.get("asi_type").startsWith("Take"); };
        f.dynOptions = [this, &c](const Wizard&) {
            std::vector<String> o;
            for (auto& r : feats_) {
                String cat = r.get("category");
                if (cat != "General" && cat != "Origin") continue;
                if (listHas(c.feats, r.name()) && !r.has("repeatable")) continue;
                if (r.name() == "Ability Score Improvement") continue;
                String pre = r.get("prereq");
                // simple ability prerequisites: "Strength or Dexterity 13+"
                bool ok = true;
                int k = pre.indexOf("13+");
                if (k > 0) {
                    ok = false;
                    for (int a = 0; a < 6; a++) if (pre.indexOf(ABIL_NAMES[a]) >= 0 && c.score[a] >= 13) ok = true;
                }
                if (ok) o.push_back(r.name());
            }
            return o;
        };
        steps.push_back(f);
        W_::Step fa{"feat_asi", "Feat ability increase", W_::CHOICE};
        fa.when = [this](const Wizard& w) { const db::Rec* r = feat(w.get("feat")); return w.get("asi_type").startsWith("Take") && r && r->has("asi"); };
        fa.dynOptions = [this](const Wizard& w) { const db::Rec* r = feat(w.get("feat")); return r ? commaList(r->get("asi")) : std::vector<String>{}; };
        steps.push_back(fa);
    }
    if (boon) {
        W_::Step b{"boon", "Epic Boon", W_::CHOICE};
        b.dynOptions = [this](const Wizard&) {
            std::vector<String> o;
            for (auto& r : feats_) if (r.get("category") == "Epic Boon") o.push_back(r.name());
            return o;
        };
        steps.push_back(b);
        W_::Step ba{"boon_asi", "Boon: +1 to which ability?", W_::CHOICE, {"Strength", "Dexterity", "Constitution", "Intelligence", "Wisdom", "Charisma"}};
        steps.push_back(ba);
    }
    if (c.ci.caster() || c.ci.num(nl, "Cantrips") > 0) {
        int newC = c.ci.num(nl, "Cantrips") - c.ci.num(c.level, "Cantrips");
        int newP = c.ci.num(nl, "Prepared Spells") - c.ci.num(c.level, "Prepared Spells");
        int maxLvl = 0;
        {
            Character tmp = c;   // max slot level at the new level
            tmp.level = nl;
            maxLvl = tmp.highestSlot();
        }
        if (newC > 0) {
            W_::Step s{"new_cantrips", String("Learn ") + newC + " cantrip" + (newC > 1 ? "s" : ""), W_::MULTI};
            s.pick = newC;
            String cl = c.cls;
            std::vector<String> known = c.spells;
            s.dynOptions = [this, cl, known](const Wizard&) {
                std::vector<String> o;
                for (auto& n : spellNames(cl, 0, 0)) if (!listHas(known, n)) o.push_back(n);
                return o;
            };
            steps.push_back(s);
        }
        if (newP > 0) {
            W_::Step s{"new_spells", String("Prepare ") + newP + " more spell" + (newP > 1 ? "s" : ""), W_::MULTI};
            s.pick = newP;
            s.hint = String("Up to level ") + maxLvl + ".";
            String cl = c.cls;
            std::vector<String> known = c.spells;
            s.dynOptions = [this, cl, known, maxLvl](const Wizard&) {
                std::vector<String> o;
                for (auto& n : spellNames(cl, 1, std::max(1, maxLvl))) if (!listHas(known, n)) o.push_back(n);
                return o;
            };
            steps.push_back(s);
        }
    }
    return steps;
}

String CharBuilder::applyLevelUp(const Wizard& w, Character& c) {
    int nl = c.level + 1;
    int die = c.hitDie(), con = c.mod(CON);
    int gain = w.get("hp").startsWith("Roll") ? rng::roll(die) : die / 2 + 1;
    String summary = String("Level ") + nl;
    if (w.get("hp").startsWith("Roll")) summary += String(": rolled ") + gain;
    gain = std::max(1, gain + con + (c.species == "Dwarf" ? 1 : 0));
    c.level = nl;
    c.hpMax += gain;
    c.hp += gain;
    summary += String(", +") + gain + " HP";
    if (w.has("subclass")) c.subclass = w.get("subclass");
    String t = w.get("asi_type");
    auto abilOf = [](const String& s) { return abilIndex(s.substring(0, s.indexOf(' '))); };
    if (t.startsWith("+2")) { int a = abilOf(w.get("asi1")); if (a >= 0) c.score[a] = std::min(20, c.score[a] + 2); }
    else if (t.startsWith("+1")) {
        int a = abilOf(w.get("asi1")), b = abilOf(w.get("asi2"));
        if (a >= 0) c.score[a] = std::min(20, c.score[a] + 1);
        if (b >= 0) c.score[b] = std::min(20, c.score[b] + 1);
    } else if (t.startsWith("Take") && w.has("feat")) {
        c.feats.push_back(w.get("feat"));
        int a = abilIndex(w.get("feat_asi"));
        if (a >= 0) c.score[a] = std::min(20, c.score[a] + 1);
    }
    if (w.has("boon")) {
        c.feats.push_back(w.get("boon"));
        int a = abilIndex(w.get("boon_asi"));
        if (a >= 0) c.score[a] = std::min(30, c.score[a] + 1);
    }
    for (auto& s : commaList(w.get("new_cantrips"))) c.spells.push_back(s);
    for (auto& s : commaList(w.get("new_spells"))) c.spells.push_back(s);
    c.syncResources();
    c.save();
    return summary;
}

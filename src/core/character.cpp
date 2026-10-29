#include "character.h"
#include "storage.h"
#include <SD.h>

namespace dnd {

const char* const ABIL_NAMES[6] = {"Strength", "Dexterity", "Constitution", "Intelligence", "Wisdom", "Charisma"};
const char* const ABIL_SHORT[6] = {"STR", "DEX", "CON", "INT", "WIS", "CHA"};
const SkillDef SKILLS[18] = {
    {"Acrobatics", DEX}, {"Animal Handling", WIS}, {"Arcana", INT}, {"Athletics", STR}, {"Deception", CHA},
    {"History", INT}, {"Insight", WIS}, {"Intimidation", CHA}, {"Investigation", INT}, {"Medicine", WIS},
    {"Nature", INT}, {"Perception", WIS}, {"Performance", CHA}, {"Persuasion", CHA}, {"Religion", INT},
    {"Sleight of Hand", DEX}, {"Stealth", DEX}, {"Survival", WIS}};

int modOf(int s) { return (s >= 10 ? (s - 10) / 2 : -((11 - s) / 2)); }
String fmtMod(int m) { return m >= 0 ? String("+") + m : String(m); }
int abilIndex(const String& n) {
    for (int i = 0; i < 6; i++) if (n.equalsIgnoreCase(ABIL_NAMES[i]) || n.equalsIgnoreCase(ABIL_SHORT[i])) return i;
    return -1;
}
std::vector<String> splitList(const String& s, char sep) {
    std::vector<String> v;
    int start = 0;
    while (start <= (int)s.length()) {
        int k = s.indexOf(sep, start);
        String t = k < 0 ? s.substring(start) : s.substring(start, k);
        t.trim();
        if (t.length()) v.push_back(t);
        if (k < 0) break;
        start = k + 1;
    }
    return v;
}
String joinList(const std::vector<String>& v, char sep) {
    String s;
    for (size_t i = 0; i < v.size(); i++) { if (i) s += sep; s += v[i]; }
    return s;
}
bool listHas(const std::vector<String>& v, const String& x) {
    for (auto& s : v) if (s.equalsIgnoreCase(x)) return true;
    return false;
}

// ---------------------------------------------------------------- items
// encoded as  name;q=2;e=1;k=weapon;d=1d8;...   (';' and '=' are not used in names)
String Item::encode() const {
    String s = name;
    auto put = [&](const char* k, const String& v) { if (v.length()) { s += ';'; s += k; s += '='; s += v; } };
    auto puti = [&](const char* k, int v) { if (v) put(k, String(v)); };
    puti("q", qty == 1 ? 0 : qty);
    if (qty == 0) put("q", "0");
    puti("e", equipped);
    puti("a", attuned);
    put("k", kind); put("c", category); put("d", damage); put("t", dtype); put("p", props);
    put("v", versatile); put("r", range); put("m", mastery); put("h", heal); put("x", roll);
    put("xl", rolllabel); put("ra", rarity); put("at", attune); put("dx", dex);
    puti("b", bonus); puti("ab", acbonus); puti("ac", acBase); puti("ch", charges); puti("cm", chargesMax);
    return s;
}

Item Item::decode(const String& s) {
    Item it;
    auto parts = splitList(s, ';');
    if (parts.empty()) return it;
    it.name = parts[0];
    for (size_t i = 1; i < parts.size(); i++) {
        int eq = parts[i].indexOf('=');
        if (eq < 0) continue;
        String k = parts[i].substring(0, eq), v = parts[i].substring(eq + 1);
        if (k == "q") it.qty = v.toInt();
        else if (k == "e") it.equipped = v.toInt();
        else if (k == "a") it.attuned = v.toInt();
        else if (k == "k") it.kind = v;
        else if (k == "c") it.category = v;
        else if (k == "d") it.damage = v;
        else if (k == "t") it.dtype = v;
        else if (k == "p") it.props = v;
        else if (k == "v") it.versatile = v;
        else if (k == "r") it.range = v;
        else if (k == "m") it.mastery = v;
        else if (k == "h") it.heal = v;
        else if (k == "x") it.roll = v;
        else if (k == "xl") it.rolllabel = v;
        else if (k == "ra") it.rarity = v;
        else if (k == "at") it.attune = v;
        else if (k == "dx") it.dex = v;
        else if (k == "b") it.bonus = v.toInt();
        else if (k == "ab") it.acbonus = v.toInt();
        else if (k == "ac") it.acBase = v.toInt();
        else if (k == "ch") it.charges = v.toInt();
        else if (k == "cm") it.chargesMax = v.toInt();
    }
    return it;
}

Item Item::fromRec(const db::Rec& r, int qty) {
    Item it;
    it.name = r.name();
    it.qty = qty;
    it.kind = r.get("kind");
    it.category = r.get("category");
    it.damage = r.get("damage");
    it.dtype = r.get("dtype");
    it.props = r.get("props");
    it.versatile = r.get("versatile");
    it.range = r.get("range");
    it.mastery = r.get("mastery");
    it.heal = r.get("heal");
    it.roll = r.get("roll");
    it.rolllabel = r.get("rolllabel");
    it.rarity = r.get("rarity");
    it.attune = r.get("attune");
    it.dex = r.get("dex");
    it.bonus = r.num("bonus");
    it.acbonus = r.num("acbonus");
    it.acBase = r.num("ac_base");
    if (it.kind == "shield" && !it.acBase) it.acBase = 2;
    it.chargesMax = it.charges = r.num("charges");
    // a magic weapon/armor entry ("Weapon +1") is generic; name it so the user can rename later
    if (it.kind == "magic" && r.get("type") == "Weapon") it.kind = "weapon", it.damage = it.damage.length() ? it.damage : "1d8", it.dtype = it.dtype.length() ? it.dtype : "Slashing", it.category = "Martial Melee";
    if (it.kind == "magic" && r.get("type") == "Armor" && it.acbonus) it.kind = "armor", it.acBase = 14, it.dex = "max2";
    if (it.kind == "magic" && r.name().startsWith("Shield")) it.kind = "shield", it.acBase = 2;
    return it;
}

// ---------------------------------------------------------------- class info
bool ClassInfo::load(const String& className) {
    loaded = false;
    rec.kv.clear();
    cols.clear();
    String idx;
    if (!storage::readText("/gmtools/srd/classes.idx", idx, 4096)) return false;
    for (auto& line : splitList(idx, '\n')) {
        auto f = splitList(line, '\t');
        if (f.size() >= 2 && f[1].equalsIgnoreCase(className)) {
            if (!db::readRecordAt("/gmtools/srd/classes.txt", f[0].toInt(), rec)) return false;
            cols = splitList(rec.get("cols"), '|');
            loaded = true;
            return true;
        }
    }
    return false;
}

String ClassInfo::cell(int level, const char* column) const {
    level = std::max(1, std::min(20, level));
    // split keeping empty cells so columns stay aligned
    String row = rec.get((String("L") + level).c_str());
    int col = -1;
    for (size_t i = 0; i < cols.size(); i++) if (cols[i] == column) col = i;
    if (col < 0) return "";
    int start = 0;
    for (int i = 0; i < col; i++) {
        start = row.indexOf('|', start);
        if (start < 0) return "";
        start++;
    }
    int end = row.indexOf('|', start);
    String c = end < 0 ? row.substring(start) : row.substring(start, end);
    c.trim();
    return c == "-" ? String("") : c;
}

int ClassInfo::num(int level, const char* column) const {
    String c = cell(level, column);
    c.replace("+", "");
    return c.toInt();
}

int ClassInfo::hitDie() const {
    String h = rec.get("hitdie", "d8");
    return h.substring(1).toInt();
}

// ---------------------------------------------------------------- storage
String Character::pathFor(const String& name) {
    String n;
    for (size_t i = 0; i < name.length() && n.length() < 32; i++) n += isalnum((unsigned char)name[i]) ? name[i] : '_';
    if (!n.length()) n = "hero";
    // never reuse another character's file (two heroes can share a name)
    String base = String("/gmtools/characters/") + n, p = base + ".txt";
    for (int i = 2; storage::exists(p.c_str()) && i < 1000; i++) p = base + "_" + i + ".txt";
    return p;
}

bool Character::load(const String& p) {
    String text;
    if (!storage::readText(p.c_str(), text, 32768)) return false;
    *this = Character();   // fields missing from the file must not carry over
    path = p;
    for (auto& line : splitList(text, '\n')) {
        int c = line.indexOf(": ");
        if (c <= 0) continue;
        String k = line.substring(0, c), v = line.substring(c + 2);
        if (k == "name") name = v;
        else if (k == "class") cls = v;
        else if (k == "subclass") subclass = v;
        else if (k == "species") species = v;
        else if (k == "species_opt") speciesOpt = v;
        else if (k == "background") background = v;
        else if (k == "alignment") alignment = v;
        else if (k == "level") level = v.toInt();
        else if (k == "scores") { auto s = splitList(v, ','); for (int i = 0; i < 6 && i < (int)s.size(); i++) score[i] = s[i].toInt(); }
        else if (k == "hp") { auto s = splitList(v, '/'); hp = s.size() > 0 ? s[0].toInt() : 1; hpMax = s.size() > 1 ? s[1].toInt() : hp; }
        else if (k == "hp_temp") hpTemp = v.toInt();
        else if (k == "hd_used") hitDiceUsed = v.toInt();
        else if (k == "death") { auto s = splitList(v, '/'); deathOk = s.size() ? s[0].toInt() : 0; deathFail = s.size() > 1 ? s[1].toInt() : 0; }
        else if (k == "inspiration") inspiration = v.toInt();
        else if (k == "skills") skillProf = splitList(v);
        else if (k == "expertise") expertise = splitList(v);
        else if (k == "saves") saveProf = splitList(v);
        else if (k == "tools") toolProf = splitList(v);
        else if (k == "feats") feats = splitList(v);
        else if (k == "spells") spells = splitList(v);
        else if (k == "mi_spells") miSpells = splitList(v);
        else if (k == "mi_ability") miAbility = v;
        else if (k == "species_ability") spAbility = v;
        else if (k == "free_used") freeUsed = splitList(v);
        else if (k == "weapons") weapons = v;
        else if (k == "armor") armor = v;
        else if (k == "slots_used") { auto s = splitList(v, ','); for (int i = 0; i < 10 && i < (int)s.size(); i++) slotsUsed[i] = s[i].toInt(); }
        else if (k == "res") {
            auto s = splitList(v);
            if (s.size() < 3) continue;
            Resource r;
            r.name = s[0]; r.cur = s[1].toInt(); r.max = s[2].toInt();
            if (s.size() >= 5 && s[4] == "c") { r.custom = true; r.rest = s[3].length() ? s[3][0] : 'L'; }
            res.push_back(r);
        }
        else if (k == "item") items.push_back(Item::decode(v));
        else if (k == "coins") { auto s = splitList(v, ','); for (int i = 0; i < 5 && i < (int)s.size(); i++) coins[i] = s[i].toInt(); }
        else if (k == "speed") speed = v.toInt();
        else if (k == "darkvision") darkvision = v.toInt();
        else if (k == "size") size = v;
        else if (k == "notes") notes = db::unescape(v);
        else if (k == "ddb") ddb = v;
    }
    ci.load(cls);
    syncResources();
    return name.length() > 0;
}

bool Character::save() {
    if (!path.length()) path = pathFor(name);
    if (!SD.exists("/gmtools/characters")) SD.mkdir("/gmtools/characters");
    String s;
    auto put = [&](const char* k, const String& v) { if (v.length()) { s += k; s += ": "; s += v; s += "\n"; } };
    put("name", name);
    put("ddb", ddb);   // near the top: the list reads only the start of each file
    put("class", cls);
    put("subclass", subclass);
    put("level", String(level));
    put("species", species);
    put("species_opt", speciesOpt);
    put("background", background);
    put("alignment", alignment);
    String sc;
    for (int i = 0; i < 6; i++) sc += String(i ? "," : "") + score[i];
    put("scores", sc);
    put("hp", String(hp) + "/" + hpMax);
    if (hpTemp) put("hp_temp", String(hpTemp));
    if (hitDiceUsed) put("hd_used", String(hitDiceUsed));
    if (deathOk || deathFail) put("death", String(deathOk) + "/" + deathFail);
    if (inspiration) put("inspiration", "1");
    put("skills", joinList(skillProf));
    put("expertise", joinList(expertise));
    put("saves", joinList(saveProf));
    put("tools", joinList(toolProf));
    put("feats", joinList(feats));
    put("spells", joinList(spells));
    put("mi_spells", joinList(miSpells));
    put("mi_ability", miAbility);
    put("species_ability", spAbility);
    put("free_used", joinList(freeUsed));
    put("weapons", weapons);
    put("armor", armor);
    String su;
    bool any = false;
    for (int i = 0; i < 10; i++) { su += String(i ? "," : "") + slotsUsed[i]; any |= slotsUsed[i] != 0; }
    if (any) put("slots_used", su);
    for (auto& r : res) put("res", r.name + "|" + r.cur + "|" + r.max + (r.custom ? String("|") + r.rest + "|c" : String("")));
    String co;
    for (int i = 0; i < 5; i++) co += String(i ? "," : "") + coins[i];
    put("coins", co);
    put("speed", String(speed));
    if (darkvision) put("darkvision", String(darkvision));
    put("size", size);
    for (auto& it : items) put("item", it.encode());
    put("notes", db::escape(notes));
    return storage::writeText(path.c_str(), s);
}

// ---------------------------------------------------------------- derived
bool Character::saveProficient(int a) const { return listHas(saveProf, ABIL_NAMES[a]); }
int Character::saveMod(int a) const { return mod(a) + (saveProficient(a) ? prof() : 0); }

int Character::skillLevel(int i) const {
    if (listHas(expertise, SKILLS[i].name)) return 2;
    return listHas(skillProf, SKILLS[i].name) ? 1 : 0;
}
int Character::skillMod(int i) const { return mod(SKILLS[i].ab) + skillLevel(i) * prof(); }

int Character::initiative() const {
    return mod(DEX) + (listHas(feats, "Alert") ? prof() : 0);
}

int Character::ac(String* how) const {
    const Item* armorIt = nullptr;
    const Item* shieldIt = nullptr;
    int extra = 0;
    for (auto& it : items) {
        if (!it.equipped) continue;
        if (it.isArmor() && !armorIt) armorIt = &it;
        else if (it.isShield() && !shieldIt) shieldIt = &it;
        else if (!it.isArmor() && !it.isShield() && it.acbonus && (it.attuned || it.attune.length() == 0)) extra += it.acbonus;
    }
    int dex = mod(DEX), v;
    String h;
    if (armorIt) {
        int d = armorIt->dex == "yes" ? dex : armorIt->dex == "max2" ? std::min(dex, 2) : 0;
        v = armorIt->acBase + d + armorIt->acbonus;
        h = armorIt->name;
        if (listHas(feats, "Defense")) { v += 1; h += " +Defense"; }
    } else if (cls == "Barbarian") {
        v = 10 + dex + mod(CON); h = "Unarmored Defense";
    } else if (cls == "Monk" && !shieldIt) {
        v = 10 + dex + mod(WIS); h = "Unarmored Defense";
    } else {
        v = 10 + dex; h = "No armor";
    }
    if (shieldIt) { v += shieldIt->acBase + shieldIt->acbonus; h += " + shield"; }
    if (extra) { v += extra; h += " + magic"; }
    if (how) *how = h;
    return v;
}

int Character::spellAbil() const {
    String a = ci.rec.get("spell_ability");
    if (a.length()) return abilIndex(a);
    if (miAbility.length()) return abilIndex(miAbility);
    if (spAbility.length() && !speciesSpells().empty()) return abilIndex(spAbility);
    return -1;
}

// The SRD's species spells: a cantrip at level 1, spells at character levels 3 and 5.
namespace {
struct Lineage { const char* species; const char* opt; const char* spells[3]; };
const Lineage LINEAGES[] = {
    {"Elf", "Drow", {"Dancing Lights", "Faerie Fire", "Darkness"}},
    {"Elf", "High Elf", {"Prestidigitation", "Detect Magic", "Misty Step"}},
    {"Elf", "Wood Elf", {"Druidcraft", "Longstrider", "Pass without Trace"}},
    {"Tiefling", "Abyssal", {"Poison Spray", "Ray of Sickness", "Hold Person"}},
    {"Tiefling", "Chthonic", {"Chill Touch", "False Life", "Ray of Enfeeblement"}},
    {"Tiefling", "Infernal", {"Fire Bolt", "Hellish Rebuke", "Darkness"}},
    {"Gnome", "Forest Gnome", {"Minor Illusion", "Speak with Animals", nullptr}},
    {"Gnome", "Rock Gnome", {"Mending", "Prestidigitation", nullptr}},
};
}  // namespace

std::vector<Character::Granted> Character::speciesSpells() const {
    std::vector<Granted> v;
    for (auto& l : LINEAGES) {
        if (species != l.species || !speciesOpt.equalsIgnoreCase(l.opt)) continue;
        if (species == "Gnome") {
            // Forest: Speak with Animals free PB times; Rock: two cantrips
            v.push_back({l.spells[0], l.opt, 0});
            v.push_back({l.spells[1], l.opt, String(l.opt) == "Forest Gnome" ? prof() : 0});
        } else {
            v.push_back({l.spells[0], l.opt, 0});
            if (level >= 3) v.push_back({l.spells[1], l.opt, 1});
            if (level >= 5) v.push_back({l.spells[2], l.opt, 1});
        }
    }
    if (species == "Tiefling") v.push_back({"Thaumaturgy", "Tiefling", 0});
    return v;
}

int Character::freeLeft(const String& spell, int freeMax) const {
    int used = 0;
    for (auto& n : freeUsed) if (n == spell) used++;
    return std::max(0, freeMax - used);
}

// The lineage whose level 1 cantrip is among these spells (D&D Beyond lists the
// species' spells but not always the lineage).
String Character::guessLineage(const String& species, const std::vector<String>& spells) {
    for (auto& l : LINEAGES)
        if (species == l.species)
            for (auto& s : spells) if (s.equalsIgnoreCase(l.spells[0])) return l.opt;
    return "";
}
int Character::spellAttack() const { int a = spellAbil(); return a < 0 ? 0 : mod(a) + prof(); }
int Character::spellDC() const { int a = spellAbil(); return a < 0 ? 0 : 8 + mod(a) + prof(); }

int Character::maxSlots(int sl) const {
    if (sl < 1 || sl > 9) return 0;
    String pact = ci.cell(level, "Slot Level");
    if (pact.length()) return pact.toInt() == sl ? ci.num(level, "Spell Slots") : 0;
    return ci.num(level, String(sl).c_str());
}
int Character::pactSlotLevel() const {
    String pact = ci.cell(level, "Slot Level");
    return pact.length() ? pact.toInt() : 0;
}
int Character::highestSlot() const {
    for (int s = 9; s >= 1; s--) if (maxSlots(s) > 0) return s;
    return 0;
}

bool Character::proficientWith(const Item& it) const {
    String w = weapons;
    bool simple = it.category.startsWith("Simple");
    bool martial = it.category.startsWith("Martial");
    if (simple && w.indexOf("Simple") >= 0) return true;
    if (martial && w.indexOf("Martial") >= 0) {
        int that = w.indexOf("that have");
        if (that < 0) return true;
        String cond = w.substring(that);
        return (cond.indexOf("Finesse") >= 0 && it.props.indexOf("Finesse") >= 0) ||
               (cond.indexOf("Light") >= 0 && it.props.indexOf("Light") >= 0);
    }
    return !simple && !martial;  // custom/improvised: assume yes
}

static String plusMod(const String& dice, int m) {
    if (!m) return dice;
    return dice + (m > 0 ? "+" : "") + String(m);
}

std::vector<Character::Attack> Character::attacks() const {
    std::vector<Attack> out;
    String martialDie = cls == "Monk" ? ci.cell(level, "Martial Arts") : String("");
    for (auto& it : items) {
        if (!it.isWeapon()) continue;
        bool melee = it.category.indexOf("Ranged") < 0;
        bool finesse = it.props.indexOf("Finesse") >= 0;
        int a = melee ? mod(STR) : mod(DEX);
        bool monkWeapon = martialDie.length() && melee && (it.category.startsWith("Simple") || it.props.indexOf("Light") >= 0);
        if (finesse || monkWeapon) a = std::max(mod(STR), mod(DEX));
        int hit = a + (proficientWith(it) ? prof() : 0) + it.bonus;
        Attack at;
        at.name = it.name;
        at.equipped = it.equipped;
        at.hit = "d20" + fmtMod(hit);
        String dice = it.damage.length() ? it.damage : String("1d4");
        if (monkWeapon) {
            // use the martial arts die if it's bigger
            int md = martialDie.substring(martialDie.indexOf('d') + 1).toInt();
            int wd = dice.substring(dice.indexOf('d') + 1).toInt();
            if (md > wd && dice.startsWith("1d")) dice = "1" + martialDie.substring(martialDie.indexOf('d'));
        }
        at.damage = plusMod(dice, a + it.bonus);
        at.dtype = it.dtype;
        if (it.versatile.length()) at.alt = plusMod(it.versatile, a + it.bonus);
        if (it.range.length()) at.note = it.range;
        if (!proficientWith(it)) at.note += String(at.note.length() ? " " : "") + "not proficient";
        out.push_back(at);
    }
    // unarmed strike
    Attack u;
    u.name = "Unarmed Strike";
    if (martialDie.length()) {
        int a = std::max(mod(STR), mod(DEX));
        u.hit = "d20" + fmtMod(a + prof());
        u.damage = plusMod("1" + martialDie.substring(martialDie.indexOf('d')), a);
    } else {
        u.hit = "d20" + fmtMod(mod(STR) + prof());
        u.damage = String(std::max(1, 1 + mod(STR)));
    }
    u.dtype = "Bludgeoning";
    out.push_back(u);
    // dragonborn breath weapon
    if (species == "Dragonborn") {
        Attack b;
        b.name = "Breath Weapon";
        int n = level >= 17 ? 4 : level >= 11 ? 3 : level >= 5 ? 2 : 1;
        b.damage = String(n) + "d10";
        b.note = String("DC ") + (8 + mod(CON) + prof()) + " Dex save";
        int paren = speciesOpt.indexOf('(');
        b.dtype = paren > 0 ? speciesOpt.substring(paren + 1, speciesOpt.indexOf(')')) : String("");
        out.push_back(b);
    }
    return out;
}

// ---------------------------------------------------------------- rests
// (resources and what spends them are in resources.cpp)

void Character::shortRest() {
    for (auto& r : res) {
        if (r.rest == 'S') r.cur = r.max;
        else if (r.rest == '1') r.cur = std::min(r.max, r.cur + 1);
    }
    if (pactSlotLevel()) for (int i = 0; i < 10; i++) slotsUsed[i] = 0;
}

void Character::longRest() {
    hp = hpMax;
    hpTemp = 0;
    hitDiceUsed = std::max(0, hitDiceUsed - std::max(1, level / 2));
    deathOk = deathFail = 0;
    for (int i = 0; i < 10; i++) slotsUsed[i] = 0;
    freeUsed.clear();
    for (auto& r : res) r.cur = r.max;
    for (auto& it : items) if (it.chargesMax) it.charges = it.chargesMax;
}

String Character::levelLine() const {
    String s = String("Level ") + level + " " + cls;
    if (subclass.length()) s += " (" + subclass + ")";
    return s;
}

}  // namespace dnd

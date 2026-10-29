#include "character.h"
#include <algorithm>

// Limited-use resources (Rages, Focus Points, Luck Points...) and the things
// that spend them (Flurry of Blows, Metamagic, Second Wind...), from the
// SRD 5.2.1 class features, feats and species traits.
namespace dnd {

namespace {

// Who has it: "C:Class" (sub = subclass), "F:Feat", "S:Species" (sub = species option).
struct PoolDef {
    const char* who; const char* sub; int lvl;
    const char* name;
    const char* max;    // "col" (the class table's column of that name), "PB", "WIS"/"CHA" (min 1),
                        // "5L" (5 x level), or "1,2@13,3@17" (2 from level 13, 3 from 17)
    char rest;
    const char* from;
};

const PoolDef POOLS[] = {
    {"C:Barbarian", "", 1, "Rages", "col", '1', "Rage"},
    {"C:Bard", "", 1, "Bardic Inspiration", "CHA", 'L', "Bardic Inspiration"},   // 'S' from level 5 (Font of Inspiration)
    {"C:Cleric", "", 2, "Channel Divinity", "col", '1', "Channel Divinity"},
    {"C:Cleric", "", 10, "Divine Intervention", "1", 'L', "Divine Intervention"},
    {"C:Druid", "", 2, "Wild Shape", "col", '1', "Wild Shape"},
    {"C:Druid", "", 5, "Wild Resurgence", "1", 'L', "Wild Resurgence"},
    {"C:Fighter", "", 1, "Second Wind", "col", '1', "Second Wind"},
    {"C:Fighter", "", 2, "Action Surge", "1,2@17", 'S', "Action Surge"},
    {"C:Fighter", "", 9, "Indomitable", "1,2@13,3@17", 'L', "Indomitable"},
    {"C:Monk", "", 2, "Focus Points", "col", 'S', "Monk's Focus"},
    {"C:Monk", "", 2, "Uncanny Metabolism", "1", 'L', "Uncanny Metabolism"},
    {"C:Paladin", "", 1, "Lay On Hands", "5L", 'L', "Lay On Hands"},
    {"C:Paladin", "", 3, "Channel Divinity", "col", '1', "Channel Divinity"},
    {"C:Ranger", "", 1, "Favored Enemy", "col", 'L', "Favored Enemy"},
    {"C:Ranger", "", 10, "Tireless", "WIS", 'L', "Tireless"},
    {"C:Ranger", "", 14, "Nature's Veil", "WIS", 'L', "Nature's Veil"},
    {"C:Rogue", "", 20, "Stroke of Luck", "1", 'S', "Stroke of Luck"},
    {"C:Sorcerer", "", 1, "Innate Sorcery", "2", 'L', "Innate Sorcery"},
    {"C:Sorcerer", "", 2, "Sorcery Points", "col", 'L', "Font of Magic"},
    {"C:Sorcerer", "", 5, "Sorcerous Restoration", "1", 'L', "Sorcerous Restoration"},
    {"C:Warlock", "", 2, "Magical Cunning", "1", 'L', "Magical Cunning"},
    {"C:Wizard", "", 1, "Arcane Recovery", "1", 'L', "Arcane Recovery"},
    {"C:Barbarian", "Path of the Berserker", 14, "Intimidating Presence", "1", 'L', "Intimidating Presence"},
    {"C:Druid", "Circle of the Land", 6, "Natural Recovery", "1", 'L', "Natural Recovery"},
    {"C:Monk", "Warrior of the Open Hand", 6, "Wholeness of Body", "WIS", 'L', "Wholeness of Body"},
    {"C:Paladin", "Oath of Devotion", 20, "Holy Nimbus", "1", 'L', "Holy Nimbus"},
    {"C:Sorcerer", "Draconic Sorcery", 14, "Dragon Wings", "1", 'L', "Dragon Wings"},
    {"C:Warlock", "Fiend Patron", 6, "Dark One's Own Luck", "CHA", 'L', "Dark One's Own Luck"},
    {"C:Warlock", "Fiend Patron", 14, "Hurl Through Hell", "1", 'L', "Hurl Through Hell"},
    // feats (the SRD's, and common ones a D&D Beyond import brings)
    {"F:Boon of Fate", "", 1, "Improve Fate", "1", 'S', "Boon of Fate"},
    {"F:Lucky", "", 1, "Luck Points", "PB", 'L', "Lucky"},
    {"F:Martial Adept", "", 1, "Superiority Die", "1", 'S', "Martial Adept"},
    {"F:Mage Slayer", "", 1, "Guarded Mind", "1", 'S', "Mage Slayer"},
    {"F:Gift of the Chromatic Dragon", "", 1, "Chromatic Infusion", "1", 'L', "Gift of the Chromatic Dragon"},
    {"F:Gift of the Chromatic Dragon", "", 1, "Reactive Resistance", "PB", 'L', "Gift of the Chromatic Dragon"},
    {"F:Gift of the Metallic Dragon", "", 1, "Protective Wings", "PB", 'L', "Gift of the Metallic Dragon"},
    // species traits
    {"S:Dragonborn", "", 1, "Breath Weapon", "PB", 'L', "Breath Weapon"},
    {"S:Dragonborn", "", 5, "Draconic Flight", "1", 'L', "Draconic Flight"},
    {"S:Dwarf", "", 1, "Stonecunning", "PB", 'L', "Stonecunning"},
    {"S:Goliath", "", 1, "Giant Ancestry", "PB", 'L', "Giant Ancestry"},
    {"S:Goliath", "", 5, "Large Form", "1", 'L', "Large Form"},
    {"S:Orc", "", 1, "Adrenaline Rush", "PB", 'S', "Adrenaline Rush"},
    {"S:Orc", "", 1, "Relentless Endurance", "1", 'L', "Relentless Endurance"},
};

struct UseDef {
    const char* who; const char* sub; int lvl;
    const char* pool;
    const char* name; int cost;
    const char* roll; const char* label; const char* note;
    UseEffect effect;
    const char* target; int amount;
};

// Tokens in roll/note: see Character::expand().
const UseDef USES[] = {
    {"C:Barbarian", "", 1, "Rages", "Rage", 1, "", "", "+{RD} Str damage, resist B/P/S, Adv. on Str", U_NONE, "", 0},
    {"C:Barbarian", "Path of the Berserker", 14, "Rages", "Restore Presence", 1, "", "", "", U_NONE, "Intimidating Presence", 0},
    {"C:Barbarian", "Path of the Berserker", 14, "Intimidating Presence", "Intimidating Presence", 1, "", "", "Wis save DC {8+STR}: Frightened 1 min", U_NONE, "", 0},

    {"C:Bard", "", 1, "Bardic Inspiration", "Bardic Inspiration", 1, "1{BD}", "Bardic die", "", U_NONE, "", 0},
    {"C:Bard", "College of Lore", 3, "Bardic Inspiration", "Cutting Words", 1, "1{BD}", "Subtract", "", U_NONE, "", 0},
    {"C:Bard", "College of Lore", 14, "Bardic Inspiration", "Peerless Skill", 1, "1{BD}", "Add to the check", "", U_NONE, "", 0},

    {"C:Cleric", "", 2, "Channel Divinity", "Divine Spark: heal", 1, "{n1:7:13:18}d8{+WIS}", "Healing", "", U_NONE, "", 0},
    {"C:Cleric", "", 2, "Channel Divinity", "Divine Spark: harm", 1, "{n1:7:13:18}d8{+WIS}", "Necrotic/Radiant", "Con save DC {DC}, half on success", U_NONE, "", 0},
    {"C:Cleric", "", 2, "Channel Divinity", "Turn Undead", 1, "", "", "Wis save DC {DC}: Frightened, Incapacitated 1 min", U_NONE, "", 0},
    {"C:Cleric", "Life Domain", 3, "Channel Divinity", "Preserve Life", 1, "{5L}", "HP to share out", "", U_NONE, "", 0},
    {"C:Cleric", "", 10, "Divine Intervention", "Divine Intervention", 1, "", "", "Cast a Cleric spell of level 5 or lower, no slot", U_NONE, "", 0},

    {"C:Druid", "", 2, "Wild Shape", "Wild Shape", 1, "", "", "Bonus Action, lasts {HL} hours", U_NONE, "", 0},
    {"C:Druid", "", 2, "Wild Shape", "Wild Companion", 1, "", "", "Cast Find Familiar (a Fey spirit)", U_NONE, "", 0},
    {"C:Druid", "Circle of the Land", 3, "Wild Shape", "Land's Aid", 1, "{n2:10:14}d6", "Necrotic", "Con save DC {DC}; one creature heals the same", U_NONE, "", 0},
    {"C:Druid", "Circle of the Land", 14, "Wild Shape", "Nature's Sanctuary", 1, "", "", "15-ft cube of trees: Half Cover for allies, 1 min", U_NONE, "", 0},
    {"C:Druid", "", 5, "Wild Shape", "Slot -> Wild Shape", 0, "", "", "", U_SLOT_USE, "", 0},
    {"C:Druid", "", 5, "Wild Resurgence", "Wild Shape -> slot", 1, "", "", "", U_WS_SLOT, "", 0},
    {"C:Druid", "Circle of the Land", 6, "Natural Recovery", "Recover spell slots", 1, "", "", "", U_RECOVER, "", 0},

    {"C:Fighter", "", 1, "Second Wind", "Second Wind", 1, "1d10+{L}", "Healing", "", U_HEAL, "", 0},
    {"C:Fighter", "", 2, "Second Wind", "Tactical Mind", 1, "1d10", "Add to the check", "Still a failure? The use isn't spent", U_NONE, "", 0},
    {"C:Fighter", "", 2, "Action Surge", "Action Surge", 1, "", "", "One more action this turn", U_NONE, "", 0},
    {"C:Fighter", "", 9, "Indomitable", "Indomitable", 1, "d20+{L}", "Reroll + level", "Add your save modifier too", U_NONE, "", 0},

    {"C:Monk", "", 2, "Focus Points", "Flurry of Blows", 1, "", "", "Bonus Action: two Unarmed Strikes", U_NONE, "", 0},
    {"C:Monk", "", 2, "Focus Points", "Patient Defense", 1, "", "", "Bonus Action: Disengage and Dodge", U_NONE, "", 0},
    {"C:Monk", "", 2, "Focus Points", "Step of the Wind", 1, "", "", "Bonus Action: Disengage and Dash, jump x2", U_NONE, "", 0},
    {"C:Monk", "", 3, "Focus Points", "Deflect: redirect", 1, "2{MA}{+DEX}", "Damage", "Dex save DC {KDC}", U_NONE, "", 0},
    {"C:Monk", "", 5, "Focus Points", "Stunning Strike", 1, "", "", "Con save DC {KDC}: Stunned to your next turn", U_NONE, "", 0},
    {"C:Monk", "", 14, "Focus Points", "Survivor: reroll", 1, "", "", "Reroll a failed save", U_NONE, "", 0},
    {"C:Monk", "", 18, "Focus Points", "Superior Defense", 3, "", "", "Resistance to all but Force, 1 min", U_NONE, "", 0},
    {"C:Monk", "Warrior of the Open Hand", 17, "Focus Points", "Quivering Palm", 4, "10d12", "Force", "Con save DC {KDC}, half on success", U_NONE, "", 0},
    {"C:Monk", "", 2, "Uncanny Metabolism", "Uncanny Metabolism", 1, "1{MA}+{L}", "Healing", "Focus Points restored", U_HEAL, "Focus Points", 0},
    {"C:Monk", "Warrior of the Open Hand", 6, "Wholeness of Body", "Wholeness of Body", 1, "1{MA}{+WIS}", "Healing", "", U_HEAL, "", 0},

    {"C:Paladin", "", 1, "Lay On Hands", "Heal 1 HP", 1, "", "", "", U_NONE, "", 0},
    {"C:Paladin", "", 1, "Lay On Hands", "Heal 5 HP", 5, "", "", "", U_NONE, "", 0},
    {"C:Paladin", "", 1, "Lay On Hands", "Heal 10 HP", 10, "", "", "", U_NONE, "", 0},
    {"C:Paladin", "", 1, "Lay On Hands", "Cure Poisoned", 5, "", "", "", U_NONE, "", 0},
    {"C:Paladin", "", 3, "Channel Divinity", "Divine Sense", 1, "", "", "Sense celestials, fiends, undead in 60 ft, 10 min", U_NONE, "", 0},
    {"C:Paladin", "Oath of Devotion", 3, "Channel Divinity", "Sacred Weapon", 1, "", "", "+{CHA} to attack rolls, bright light, 10 min", U_NONE, "", 0},
    {"C:Paladin", "", 9, "Channel Divinity", "Abjure Foes", 1, "", "", "Wis save DC {DC}: Frightened 1 min", U_NONE, "", 0},
    {"C:Paladin", "Oath of Devotion", 20, "Holy Nimbus", "Holy Nimbus", 1, "", "", "10 min: aura of light, Adv. vs fiend/undead saves", U_NONE, "", 0},

    {"C:Ranger", "", 1, "Favored Enemy", "Free Hunter's Mark", 1, "", "", "Cast Hunter's Mark without a slot", U_NONE, "", 0},
    {"C:Ranger", "", 10, "Tireless", "Tireless", 1, "1d8{+WIS}", "Temp HP", "", U_TEMP, "", 0},
    {"C:Ranger", "", 14, "Nature's Veil", "Nature's Veil", 1, "", "", "Invisible until the end of your next turn", U_NONE, "", 0},
    {"C:Rogue", "", 20, "Stroke of Luck", "Stroke of Luck", 1, "", "", "Treat a failed D20 Test as a 20", U_NONE, "", 0},

    {"C:Sorcerer", "", 1, "Innate Sorcery", "Innate Sorcery", 1, "", "", "1 min: +1 spell DC, Adv. on spell attacks", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Create a slot", 0, "", "", "", U_MAKE_SLOT, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Slot -> points", 0, "", "", "", U_SLOT_POINTS, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Careful Spell", 1, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Distant Spell", 1, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Empowered Spell", 1, "", "", "Reroll up to {CHA} damage dice", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Extended Spell", 1, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Heightened Spell", 2, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Quickened Spell", 2, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Seeking Spell", 1, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Subtle Spell", 1, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Transmuted Spell", 1, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 2, "Sorcery Points", "Twinned Spell", 1, "", "", "", U_NONE, "", 0},
    {"C:Sorcerer", "", 7, "Sorcery Points", "Innate Sorcery", 2, "", "", "1 min: +1 spell DC, Adv. on spell attacks", U_NONE, "", 0},
    {"C:Sorcerer", "Draconic Sorcery", 14, "Sorcery Points", "Restore Wings", 3, "", "", "", U_NONE, "Dragon Wings", 0},
    {"C:Sorcerer", "", 5, "Sorcerous Restoration", "Regain points", 1, "", "", "After a Short Rest", U_NONE, "Sorcery Points", -1},
    {"C:Sorcerer", "Draconic Sorcery", 14, "Dragon Wings", "Dragon Wings", 1, "", "", "Fly speed 60 ft, 1 hour", U_NONE, "", 0},

    {"C:Warlock", "", 2, "Magical Cunning", "Magical Cunning", 1, "", "", "", U_PACT_HALF, "", 0},
    {"C:Warlock", "Fiend Patron", 6, "Dark One's Own Luck", "Dark One's Own Luck", 1, "1d10", "Add to the roll", "", U_NONE, "", 0},
    {"C:Warlock", "Fiend Patron", 14, "Hurl Through Hell", "Hurl Through Hell", 1, "8d10", "Psychic", "Cha save DC {DC}", U_NONE, "", 0},
    {"C:Wizard", "", 1, "Arcane Recovery", "Recover spell slots", 1, "", "", "", U_RECOVER, "", 0},

    {"F:Boon of Fate", "", 1, "Improve Fate", "Improve Fate", 1, "2d4", "Bonus or penalty", "", U_NONE, "", 0},
    {"F:Lucky", "", 1, "Luck Points", "Spend a Luck Point", 1, "", "", "Advantage on a D20 Test (or Disadv. on an attack vs you)", U_NONE, "", 0},
    {"F:Martial Adept", "", 1, "Superiority Die", "Superiority Die", 1, "1d6", "Maneuver", "", U_NONE, "", 0},
    {"F:Mage Slayer", "", 1, "Guarded Mind", "Guarded Mind", 1, "", "", "Succeed on a failed Int, Wis or Cha save", U_NONE, "", 0},
    {"F:Gift of the Chromatic Dragon", "", 1, "Chromatic Infusion", "Chromatic Infusion", 1, "", "", "A weapon deals +1d4 (chosen type), 1 min", U_NONE, "", 0},
    {"F:Gift of the Chromatic Dragon", "", 1, "Reactive Resistance", "Reactive Resistance", 1, "", "", "Reaction: Resistance to that damage", U_NONE, "", 0},
    {"F:Gift of the Metallic Dragon", "", 1, "Protective Wings", "Protective Wings", 1, "", "", "Reaction: +{PB} AC against one attack", U_NONE, "", 0},

    {"S:Dragonborn", "", 1, "Breath Weapon", "Breath Weapon", 1, "{n1:5:11:17}d10", "Damage", "Dex save DC {CDC}, half on success", U_NONE, "", 0},
    {"S:Dragonborn", "", 5, "Draconic Flight", "Draconic Flight", 1, "", "", "Fly speed = your Speed, 10 min", U_NONE, "", 0},
    {"S:Dwarf", "", 1, "Stonecunning", "Stonecunning", 1, "", "", "Tremorsense 60 ft, 10 min", U_NONE, "", 0},
    {"S:Goliath", "Cloud's Jaunt", 1, "Giant Ancestry", "Cloud's Jaunt", 1, "", "", "Bonus Action: teleport 30 ft", U_NONE, "", 0},
    {"S:Goliath", "Fire's Burn", 1, "Giant Ancestry", "Fire's Burn", 1, "1d10", "Fire", "", U_NONE, "", 0},
    {"S:Goliath", "Frost's Chill", 1, "Giant Ancestry", "Frost's Chill", 1, "1d6", "Cold", "Target's Speed -10 ft", U_NONE, "", 0},
    {"S:Goliath", "Hill's Tumble", 1, "Giant Ancestry", "Hill's Tumble", 1, "", "", "Knock a Large or smaller target Prone", U_NONE, "", 0},
    {"S:Goliath", "Stone's Endurance", 1, "Giant Ancestry", "Stone's Endurance", 1, "1d12{+CON}", "Damage reduced", "", U_NONE, "", 0},
    {"S:Goliath", "Storm's Thunder", 1, "Giant Ancestry", "Storm's Thunder", 1, "1d8", "Thunder", "", U_NONE, "", 0},
    {"S:Goliath", "", 5, "Large Form", "Large Form", 1, "", "", "Large, Adv. on Str checks, +10 ft Speed, 10 min", U_NONE, "", 0},
    {"S:Orc", "", 1, "Adrenaline Rush", "Adrenaline Rush", 1, "{PB}", "Temp HP", "Bonus Action: Dash", U_TEMP, "", 0},
    {"S:Orc", "", 1, "Relentless Endurance", "Relentless Endurance", 1, "", "", "Dropped to 1 HP instead of 0", U_STAND, "", 0},
};

// Sorcery Points to create a slot of each level, and the Sorcerer level needed.
const int SLOT_COST[6] = {0, 2, 3, 5, 6, 7};
const int SLOT_MIN_LEVEL[6] = {0, 2, 3, 5, 7, 9};

}  // namespace

static bool hasSource(const Character& c, const char* who, const char* sub, int lvl) {
    const String w = who + 2;
    if (who[0] == 'F') return listHas(c.feats, w);
    if (c.level < lvl) return false;
    if (who[0] == 'C') return c.cls == w && (!*sub || c.subclass == sub);
    return c.species == w && (!*sub || c.speciesOpt.startsWith(sub));
}

// "1,2@13,3@17" -> the value for this level
static int stepped(const char* spec, int level) {
    int v = 0;
    for (auto& part : splitList(spec, ',')) {
        const int at = part.indexOf('@');
        if (at < 0 || level >= part.substring(at + 1).toInt()) v = part.toInt();
    }
    return v;
}

void Character::syncResources() {
    std::vector<Resource> want;
    for (auto& p : POOLS) {
        if (!hasSource(*this, p.who, p.sub, p.lvl)) continue;
        int mx;
        const String m = p.max;
        if (m == "col") mx = listHas(ci.cols, p.name) ? ci.num(level, p.name) : 0;
        else if (m == "PB") mx = prof();
        else if (m == "WIS") mx = std::max(1, mod(WIS));
        else if (m == "CHA") mx = std::max(1, mod(CHA));
        else if (m == "5L") mx = 5 * level;
        else mx = stepped(p.max, level);
        if (mx <= 0) continue;
        Resource r;
        r.name = p.name;
        r.cur = r.max = mx;
        r.rest = p.rest;
        if (r.name == "Bardic Inspiration" && level >= 5) r.rest = 'S';
        r.from = p.from;
        want.push_back(r);
    }
    // keep current values for resources that already exist (and gain what a new level adds)
    for (auto& w : want)
        for (auto& r : res)
            if (!r.custom && r.name == w.name) w.cur = std::min(r.cur + std::max(0, w.max - r.max), w.max);
    for (auto& r : res) if (r.custom) want.push_back(r);
    res = want;
}

int Character::resIndex(const String& name) const {
    for (size_t i = 0; i < res.size(); i++) if (res[i].name == name) return i;
    return -1;
}

std::vector<Use> Character::uses(const Resource& r) const {
    std::vector<Use> out;
    if (r.custom) return out;
    for (auto& d : USES) {
        if (r.name != d.pool || !hasSource(*this, d.who, d.sub, d.lvl)) continue;
        Use u;
        u.name = d.name; u.cost = d.cost;
        u.roll = d.roll; u.label = d.label; u.note = d.note;
        u.effect = d.effect; u.target = d.target; u.amount = d.amount;
        out.push_back(u);
    }
    return out;
}

String Character::expand(const String& t) const {
    String out;
    int i = 0;
    while (i < (int)t.length()) {
        const int a = t.indexOf('{', i);
        if (a < 0) { out += t.substring(i); break; }
        const int b = t.indexOf('}', a);
        if (b < 0) { out += t.substring(i); break; }
        out += t.substring(i, a);
        String k = t.substring(a + 1, b);
        i = b + 1;
        bool sign = k.startsWith("+");
        if (sign) k = k.substring(1);
        const int ab = abilIndex(k);
        if (ab >= 0) {
            const int m = mod(ab);
            out += sign ? (m ? fmtMod(m) : String("")) : String(m);
        } else if (k == "L") out += level;
        else if (k == "HL") out += level / 2;
        else if (k == "5L") out += 5 * level;
        else if (k == "PB") out += prof();
        else if (k == "DC") out += spellDC();
        else if (k == "KDC") out += 8 + mod(WIS) + prof();
        else if (k == "CDC") out += 8 + mod(CON) + prof();
        else if (k == "8+STR") out += 8 + mod(STR) + prof();
        else if (k == "MA") { String d = ci.cell(level, "Martial Arts"); out += d.substring(std::max(0, d.indexOf('d'))); }
        else if (k == "BD") { String d = ci.cell(level, "Bardic Die"); d.toLowerCase(); out += d.length() ? d : String("d6"); }
        else if (k == "RD") { String d = ci.cell(level, "Rage Damage"); d.replace("+", ""); out += d; }
        else if (k.startsWith("n")) {
            // "n1:7:13:18": 1, plus one for each level reached
            auto parts = splitList(k.substring(1), ':');
            int n = parts.size() ? parts[0].toInt() : 1;
            for (size_t p = 1; p < parts.size(); p++) if (level >= parts[p].toInt()) n++;
            out += n;
        }
    }
    return out;
}

int Character::slotPointCost(int l) { return l >= 1 && l <= 5 ? SLOT_COST[l] : 0; }

std::vector<int> Character::useLevels(const Use& u) const {
    std::vector<int> v;
    if (u.effect == U_MAKE_SLOT) {
        for (int l = 1; l <= 5; l++) if (level >= SLOT_MIN_LEVEL[l] && maxSlots(l)) v.push_back(l);
    } else if (u.effect == U_SLOT_POINTS) {
        for (int l = 1; l <= 9; l++) if (maxSlots(l) - slotsUsed[l] > 0) v.push_back(l);
    }
    return v;
}

static int lowestSlot(const Character& c) {
    for (int l = 1; l <= 9; l++) if (c.maxSlots(l) - c.slotsUsed[l] > 0) return l;
    return 0;
}

bool Character::spendUse(int ri, const Use& u, int sl, String& msg) {
    if (ri < 0 || ri >= (int)res.size()) { msg = "No such resource"; return false; }
    Resource& r = res[ri];
    int cost = u.cost;
    if (u.effect == U_MAKE_SLOT) {
        if (sl < 1 || sl > 5 || level < SLOT_MIN_LEVEL[sl] || !maxSlots(sl)) { msg = "Can't create that slot"; return false; }
        cost = SLOT_COST[sl];
    }
    if (r.cur < cost) {
        msg = String("Not enough ") + r.name + " (" + r.cur + "/" + cost + ")";
        return false;
    }
    // checks that need something else first
    switch (u.effect) {
        case U_SLOT_POINTS:
            if (sl < 1 || sl > 9 || maxSlots(sl) - slotsUsed[sl] <= 0) { msg = "No slot of that level left"; return false; }
            if (r.cur >= r.max) { msg = String(r.name) + " already full"; return false; }
            break;
        case U_SLOT_USE:
            if (r.cur > 0) { msg = String("Only when you have no ") + r.name + " left"; return false; }
            if (!lowestSlot(*this)) { msg = "No spell slots left"; return false; }
            break;
        case U_WS_SLOT: {
            const int ws = resIndex("Wild Shape");
            if (ws < 0 || res[ws].cur <= 0) { msg = "No Wild Shape uses left"; return false; }
            break;
        }
        case U_STAND:
            if (hp > 0) { msg = "Only when you drop to 0 HP"; return false; }
            break;
        case U_PACT_HALF: case U_RECOVER: {
            bool any = false;
            for (int l = 1; l <= (u.effect == U_RECOVER ? 5 : 9); l++) any |= slotsUsed[l] > 0;
            if (!any) { msg = "No spent slots to recover"; return false; }
            break;
        }
        default: break;
    }
    r.cur -= cost;
    msg = cost ? String("Used ") + u.name + " (" + r.name + " " + r.cur + "/" + r.max + ")" : String(u.name);
    switch (u.effect) {
        case U_MAKE_SLOT:
            slotsUsed[sl]--;   // may go below 0: an extra slot, gone on a Long Rest
            msg = String("Created a level ") + sl + " slot for " + cost + " points";
            break;
        case U_SLOT_POINTS:
            slotsUsed[sl]++;
            r.cur = std::min(r.max, r.cur + sl);
            msg = String("Level ") + sl + " slot -> " + r.name + " " + r.cur + "/" + r.max;
            break;
        case U_SLOT_USE: {
            const int l = lowestSlot(*this);
            slotsUsed[l]++;
            r.cur = 1;
            msg = String("Spent a level ") + l + " slot for a " + r.name + " use";
            break;
        }
        case U_WS_SLOT: {
            res[resIndex("Wild Shape")].cur--;
            slotsUsed[1]--;
            msg = "Spent a Wild Shape use for a level 1 slot";
            break;
        }
        case U_RECOVER: {
            // slots worth up to half your level (rounded up), none above level 5: the highest first
            int budget = (level + 1) / 2;
            String got;
            for (int l = 5; l >= 1; l--)
                while (slotsUsed[l] > 0 && l <= budget) { slotsUsed[l]--; budget -= l; got += String(got.length() ? ", " : "") + l; }
            msg = got.length() ? "Recovered slots of level " + got : String("Nothing to recover");
            break;
        }
        case U_PACT_HALF: {
            const int pl = pactSlotLevel();
            const int back = std::min(slotsUsed[pl], (maxSlots(pl) + 1) / 2);
            slotsUsed[pl] -= back;
            msg = String("Recovered ") + back + " Pact Magic slot" + (back == 1 ? "" : "s");
            break;
        }
        case U_STAND:
            hp = 1;
            deathOk = deathFail = 0;
            msg = "Relentless Endurance: you're at 1 HP";
            break;
        default: break;
    }
    if (u.target.length()) {
        const int t = resIndex(u.target);
        if (t >= 0) {
            Resource& x = res[t];
            const int before = x.cur;
            x.cur = u.amount > 0 ? std::min(x.max, x.cur + u.amount) : u.amount < 0 ? std::min(x.max, x.cur + level / 2) : x.max;
            msg += String(", ") + x.name + " +" + (x.cur - before);
        }
    }
    return true;
}

void Character::applyUseRoll(const Use& u, int total) {
    if (u.effect == U_HEAL) {
        hp = std::min(hpMax, hp + std::max(0, total));
        if (hp > 0) deathOk = deathFail = 0;
    } else if (u.effect == U_TEMP) hpTemp = std::max(hpTemp, total);
}

}  // namespace dnd

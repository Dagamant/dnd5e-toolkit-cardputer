#pragma once
#include "db.h"
#include <Arduino.h>
#include <vector>

// D&D 5e (SRD 5.2.1) player character: storage + derived numbers.
// Files live in /gmtools/characters/<Name>.txt as "key: value" lines;
// lists use '|' separators and inventory uses one "item:" line per entry.
namespace dnd {

enum Abil { STR, DEX, CON, INT, WIS, CHA };
extern const char* const ABIL_NAMES[6];
extern const char* const ABIL_SHORT[6];
struct SkillDef { const char* name; Abil ab; };
extern const SkillDef SKILLS[18];

int modOf(int score);
String fmtMod(int m);                  // +3 / -1 / +0
int abilIndex(const String& name);     // "Strength" or "STR" -> 0..5, -1
std::vector<String> splitList(const String& s, char sep = '|');
String joinList(const std::vector<String>& v, char sep = '|');
bool listHas(const std::vector<String>& v, const String& x);

struct Item {
    String name;
    int qty = 1;
    bool equipped = false, attuned = false;
    String kind, category, damage, dtype, props, versatile, range, mastery;
    String heal, roll, rolllabel, rarity, attune, dex;
    int bonus = 0, acbonus = 0, acBase = 0, charges = 0, chargesMax = 0;
    String encode() const;
    static Item decode(const String& s);
    static Item fromRec(const db::Rec& r, int qty = 1);
    bool isWeapon() const { return kind == "weapon"; }
    bool isArmor() const { return kind == "armor"; }
    bool isShield() const { return kind == "shield"; }
};

// A counter with limited uses: Rages, Focus Points, Luck Points...
struct Resource {
    String name;
    int cur = 0, max = 0;
    char rest = 'L';        // 'L' back on a Long Rest, 'S' all back on a Short Rest, '1' one back on a Short Rest
    bool custom = false;    // added by hand (or from D&D Beyond): kept as saved, not from the rules
    String from;            // the feature, feat or trait it belongs to ("Monk's Focus", "Lucky")
};

// Something that spends a resource: "Flurry of Blows" (1 Focus Point).
// roll/note may hold {tokens} that Character::expand() fills in.
enum UseEffect : uint8_t {
    U_NONE,
    U_HEAL,         // the roll heals you
    U_TEMP,         // the roll is Temporary Hit Points
    U_MAKE_SLOT,    // Sorcery Points -> a spell slot (pick the level)
    U_SLOT_POINTS,  // a spell slot -> Sorcery Points (pick the level)
    U_RECOVER,      // Arcane Recovery: get back slots worth half your level
    U_PACT_HALF,    // Magical Cunning: get back half your Pact Magic slots
    U_WS_SLOT,      // Wild Resurgence: a Wild Shape use -> a level 1 slot
    U_SLOT_USE,     // a spell slot -> one use of this resource (when it's empty)
    U_STAND,        // Relentless Endurance: drop to 1 HP instead of 0
};
struct Use {
    String name;
    int cost = 1;
    String roll, label, note;
    UseEffect effect = U_NONE;
    String target;          // another resource it refills
    int amount = 0;         // how much of it: 0 all, -1 half your level
};

// The class's SRD record plus helpers for its level table.
struct ClassInfo {
    db::Rec rec;
    std::vector<String> cols;
    bool loaded = false;
    bool load(const String& className);
    String cell(int level, const char* column) const;  // "" if no such column
    int num(int level, const char* column) const;      // "-" -> 0
    int hitDie() const;                                // 12 for d12
    bool caster() const { return rec.has("spell_ability"); }
};

class Character {
public:
    String path;
    String name, cls, subclass, species, speciesOpt, background, alignment;
    int level = 1;
    int score[6] = {10, 10, 10, 10, 10, 10};
    int hpMax = 1, hp = 1, hpTemp = 0, hitDiceUsed = 0;
    int deathOk = 0, deathFail = 0;
    bool inspiration = false;
    std::vector<String> skillProf, expertise, saveProf, feats, spells, toolProf;
    std::vector<String> miSpells;       // spells from feats (Magic Initiate): always prepared
    String miAbility;                   // their casting ability
    String spAbility;                   // casting ability of the species' spells (Elf, Gnome, Tiefling)
    std::vector<String> freeUsed;       // one entry per free cast spent; back on a Long Rest
    String weapons, armor;              // training text from the class
    int slotsUsed[10] = {0};
    std::vector<Resource> res;
    std::vector<Item> items;
    int coins[5] = {0, 0, 0, 0, 0};     // cp sp ep gp pp
    int speed = 30, darkvision = 0;
    String size, notes;
    String ddb;                         // D&D Beyond share link it was imported from

    ClassInfo ci;

    bool load(const String& filePath);
    bool save();
    static String pathFor(const String& name);   // a new, unused file for this name

    int prof() const { return 2 + (level - 1) / 4; }
    int mod(int a) const { return modOf(score[a]); }
    bool saveProficient(int a) const;
    int saveMod(int a) const;
    int skillLevel(int i) const;        // 0 none, 1 proficient, 2 expertise
    int skillMod(int i) const;
    int initiative() const;
    int ac(String* how = nullptr) const;
    int passivePerception() const { return 10 + skillMod(11); }
    int spellAbil() const;              // -1 if not a caster
    int spellAttack() const;
    int spellDC() const;
    int hitDie() const { return ci.hitDie(); }

    // Spells a feat or the species grants. Cantrips are at will; a levelled one
    // has free casts (freeMax, back on a Long Rest), then needs a spell slot.
    struct Granted { String name, source; int freeMax; };
    std::vector<Granted> speciesSpells() const;         // from species, lineage and level
    int freeLeft(const String& spell, int freeMax) const;
    static String guessLineage(const String& species, const std::vector<String>& spells);

    int maxSlots(int slotLevel) const;
    int pactSlotLevel() const;          // warlock: level of all slots, else 0
    int highestSlot() const;

    struct Attack {
        String name, hit, damage, dtype, alt, note;
        bool equipped = false;
    };
    std::vector<Attack> attacks() const;
    bool proficientWith(const Item& it) const;

    void syncResources();               // set resource maxima for the current level
    std::vector<Use> uses(const Resource& r) const;
    String expand(const String& tmpl) const;          // "{n1:7:13:18}d8{+WIS}" -> "2d8+3"
    // Pays for a use (and does what it does to slots and other resources).
    // slotLevel is the level picked for U_MAKE_SLOT / U_SLOT_POINTS.
    bool spendUse(int resIndex, const Use& u, int slotLevel, String& msg);
    void applyUseRoll(const Use& u, int total);       // healing / temp HP from the roll
    std::vector<int> useLevels(const Use& u) const;   // levels to pick from, for the two slot uses
    int resIndex(const String& name) const;           // -1 if none
    static int slotPointCost(int slotLevel);          // Sorcery Points to create a slot of that level
    void shortRest();
    void longRest();
    String levelLine() const;           // "Level 3 Fighter (Champion)"
};

}  // namespace dnd

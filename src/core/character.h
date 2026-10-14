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

struct Resource { String name; int cur = 0, max = 0; };

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
    std::vector<String> miSpells;       // Magic Initiate spells (always prepared)
    String miAbility;                   // their casting ability
    String weapons, armor;              // training text from the class
    int slotsUsed[10] = {0};
    std::vector<Resource> res;
    std::vector<Item> items;
    int coins[5] = {0, 0, 0, 0, 0};     // cp sp ep gp pp
    int speed = 30, darkvision = 0;
    String size, notes;

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
    void shortRest();
    void longRest();
    String levelLine() const;           // "Level 3 Fighter (Champion)"
};

}  // namespace dnd

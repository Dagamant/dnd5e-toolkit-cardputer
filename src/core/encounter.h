#pragma once
#include <Arduino.h>
#include <vector>

// Encounter plan (built in the Monsters app) and the hand-off queue into the
// Initiative tracker.
namespace encounter {

struct Entry {
    String name;
    int count = 1;
    int xp = 0, hp = 1, ac = 10, init = 0;
    String hpdice, cr;
};

struct Plan {
    std::vector<Entry> monsters;
    int partySize = 4, partyLevel = 1;
    // Encounter generator choices (indexes into its option lists).
    int genHabitat = 0, genType = 0, genDiff = 1, genGroup = 0;
    int totalXp() const;
    bool load();
    bool save() const;
};

// XP budget per character from the SRD (difficulty 0 Low, 1 Moderate, 2 High).
int budget(int level, int difficulty);

// Combatants waiting to be added by the Initiative tracker next time it opens.
struct Pending { String name, monster; int init = 0, hp = 0, maxHp = 0, ac = 0; };
extern std::vector<Pending> queue;
// Roll initiative (and optionally HP) for every monster in the list and queue
// them for the tracker. Returns how many combatants were queued.
int enqueue(const std::vector<Entry>& monsters, bool rolledHp);
// Look up a monster (SRD or custom) by name. Plural names ("Goblin Warriors") work.
bool lookup(const String& name, Entry& out);
// Parse "2x Grick, 4 Swarms of Insects and a Goblin Boss (Forest, 850 XP)" into
// entries. Names that don't match a monster are returned in `unknown`.
std::vector<Entry> parseList(const String& text, String& unknown);
// The reverse: "2x Grick, 4x Swarm of Insects".
String describe(const std::vector<Entry>& monsters);

}  // namespace encounter

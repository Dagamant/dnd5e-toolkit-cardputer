#pragma once
#include "db.h"
#include "encounter.h"

// The encounter generator (moved out of the Encounters app so the Dungeon23
// app can stock rooms with it): picks SRD and custom monsters to fit the
// party's XP budget for a habitat, creature type and group style.
namespace encounter {

extern const char* const HABITATS[];
constexpr int NHAB = 14;
extern const char* const TYPES[];
constexpr int NTYPES = 15;
extern const char* const DIFF[];      // Low, Moderate, High
extern const char* const GROUPS[];    // Random, Solo, Leader + minions, Horde, Mixed
constexpr int NGROUPS = 5;
extern const char* const GROUP_SHORT[];

struct GenOptions {
    int partySize = 4, partyLevel = 1;
    int habitat = 0, type = 0, diff = 1, group = 0;   // indexes into the lists above (group 0: random)
};

struct GenResult {
    std::vector<Entry> monsters;   // biggest XP first
    int budget = 0, group = 0;     // the budget used and the group style picked
    String note;                   // what had to give ("None fit Arctic - any habitat used")
};

class Generator {
public:
    bool open();    // loads the monster index (SRD + custom)
    void close();
    bool isOpen() const { return table_.isOpen(); }
    // False when nothing fits (res.monsters is then empty).
    bool generate(const GenOptions& o, GenResult& res);

private:
    struct Cand { int idx, xp; float cr; const char* type; };
    db::Table table_;
    std::vector<Cand> cands_;
    std::vector<Entry>* out_ = nullptr;
    GenOptions o_;
    int budget_ = 0;
    String note_;

    void gather(int hab, int type);
    const Cand* pick(int lo, int hi, float maxCr, const char* sameType = nullptr);
    void add(const Cand* c, int count);
    int totalXp() const;
    int bodies() const;
    void compose(int group);
};

}  // namespace encounter

#pragma once
#include "../core/character.h"
#include "../core/db.h"
#include "../core/wizard.h"
#include <vector>

// Character creation and level-up wizards built from the SRD data on the SD card.
class CharBuilder {
public:
    // Loads class/background/species/feat records and the spell index.
    bool loadSrd();
    void unload();

    std::vector<Wizard::Step> creationSteps();
    // Fills `c` from a finished creation wizard (also adds starting equipment).
    bool applyCreation(const Wizard& w, dnd::Character& c);

    std::vector<Wizard::Step> levelUpSteps(const dnd::Character& c);
    // Returns a short summary of what changed (e.g. "+9 HP").
    String applyLevelUp(const Wizard& w, dnd::Character& c);

    // Feature names gained at `level` (class + subclass).
    std::vector<String> featuresAt(const dnd::Character& c, int level, const String& subclass);

private:
    std::vector<db::Rec> classes_, backgrounds_, species_, feats_;
    std::vector<String> subclassNames_, subclassOf_;
    db::Table spells_;
    const dnd::Character* lvl_ = nullptr;   // character being levelled
    dnd::ClassInfo lvlCi_;

    const db::Rec* cls(const String& n) const;
    const db::Rec* bg(const String& n) const;
    const db::Rec* sp(const String& n) const;
    const db::Rec* feat(const String& n) const;
    std::vector<String> spellNames(const String& cls, int minLevel, int maxLevel) const;
    void addStartingItems(dnd::Character& c, const String& list);
};

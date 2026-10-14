#pragma once
#include "../core/app.h"
#include "../core/db.h"
#include "../core/roll_overlay.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/textview.h"
#include "../core/ui.h"
#include "../core/wizard.h"
#include <functional>
#include <vector>

// Shared list/search/detail/wizard behaviour for the Spell Library and Item
// Database. Subclasses supply the table, row drawing, filters, detail text,
// roll actions and the creation wizard.
//
// List keys: type to search, Del erases, Tab cycles the filter, digits are
// subclass-specific quick filters, Enter opens, Fn+N creates a new entry.
// Detail keys: Up/Down scroll, Enter rolls, Fn+E edits / Fn+D copies to a
// custom entry, Fn+X X deletes a custom entry.
class DbBrowser : public App {
public:
    // Pick mode: Enter on the detail screen calls this instead of rolling
    // (used by the character sheet to add items / spells).
    std::function<void(const db::Rec&)> onPick;
    const char* pickVerb = "add";
    // Optional starting filter (e.g. a class name for the spell picker).
    virtual void presetFilter(const String& f) {}
    // Jump straight to an entry's detail view (used from other apps).
    bool openByName(const String& name);
    // When set, Esc in the detail view leaves the browser instead of going to the list.
    bool closeOnBack = false;

    void onEnter() override;
    void onExit() override;
    bool handle(const input::Event& e) override;
    void tick(uint32_t dt) override {}
    void draw() override;

protected:
    enum class Mode { List, Detail, Wizard };
    Mode mode_ = Mode::List;
    db::Table table_;
    std::vector<int> vis_;
    String search_;
    int sel_ = 0, top_ = 0;
    db::Rec cur_;
    int curIdx_ = -1;
    TextView tv_;
    Wizard wiz_;
    int editIdx_ = -1;           // custom entry being edited, -1 for new
    bool confirmDelete_ = false;

    virtual const char* title() const = 0;
    virtual void openTable() = 0;
    virtual bool passFilter(int i) const = 0;
    virtual String filterLabel() const = 0;
    virtual void cycleFilter(int dir) = 0;
    virtual bool quickFilter(char c) { return false; }  // digit keys
    virtual void drawRow(int i, int y, bool sel) = 0;
    virtual void buildDetail() = 0;
    virtual bool detailKey(const input::Event& e) { return false; }
    virtual void drawDetailBar(int y) {}
    virtual void rollCurrent() = 0;
    virtual std::vector<Wizard::Step> wizardSteps() { return {}; }
    virtual void presetWizard(const db::Rec& r) {}
    virtual db::Rec fromWizard() { return db::Rec(); }
    virtual const char* newLabel() const { return "New"; }  // "New spell"
    virtual bool canCreate() const { return true; }          // has a creation wizard
    virtual const char* detailFooter() const { return nullptr; }
    virtual const char* listFooter() const { return nullptr; }

    void refilter();
    void openDetail(int visIndex);
    void startWizard(int editIndex, const db::Rec* from);
    void finishWizard();
    static String jstr(const char* s) { return String(s ? s : ""); }
};

// Fresh browser instances for embedding (e.g. pickers in the character sheet).
DbBrowser* newSpellsBrowser();
DbBrowser* newItemsBrowser();
DbBrowser* newMonstersBrowser();
DbBrowser* newRulesBrowser();

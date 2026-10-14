#pragma once
#include "db_browser.h"

// Item Database browser. Also embedded by the character sheet (with onPick set)
// to add items to a character's inventory.
class ItemsBrowser : public DbBrowser {
public:
    const char* name() const override { return "Items"; }
    const char* blurb() const override { return "Equipment, magic items & yours."; }
    uint16_t accent() const override { return ui::C_GOLD; }
    void drawIcon(int cx, int cy, float t) override;

protected:
    int filter_ = 0;
    const char* title() const override;
    const char* newLabel() const override { return "New item"; }
    void openTable() override;
    bool passFilter(int i) const override;
    String filterLabel() const override;
    void cycleFilter(int d) override;
    void drawRow(int i, int y, bool s) override;
    void buildDetail() override;
    void rollCurrent() override;
    std::vector<Wizard::Step> wizardSteps() override;
    void presetWizard(const db::Rec& r) override;
    db::Rec fromWizard() override;
};

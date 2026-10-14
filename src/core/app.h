#pragma once
#include "input.h"
#include <stdint.h>

// Every tool is an App. The launcher owns the list and switches between them.
class App {
public:
    virtual ~App() {}
    virtual const char* name() const = 0;
    virtual const char* blurb() const { return ""; }
    virtual uint16_t accent() const;
    // Draw a ~40px icon centred at cx,cy (used by the launcher carousel).
    virtual void drawIcon(int cx, int cy, float t) = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    // Return false to ask the launcher to close this app.
    virtual bool handle(const input::Event& e) = 0;
    virtual void tick(uint32_t dtMs) {}
    virtual void draw() = 0;
};

// Suspend the hang watchdog around long blocking work (e.g. SD format).
void watchdogPause(bool paused);
// Frame timing for the debug console's "perf" command (resets the counters).
void perfReport(char* out, size_t n);
// Ask the launcher to switch to the app with this name after the current event.
void requestApp(const char* name);

App* makeDiceApp();
App* makeInitiativeApp();
App* makeDungeonApp();
App* makeTablesApp();
App* makeNotesApp();
App* makeCampaignApp();
App* makeSpellsApp();
App* makeCharactersApp();
App* makeMonstersApp();
App* makeEncountersApp();
App* makeRulesApp();
App* makeHazardsApp();
App* makeTravelApp();
App* makeShopApp();
App* makeItemsApp();
App* makeArchiveApp();
App* makeWebApp();
App* makeSettingsApp();

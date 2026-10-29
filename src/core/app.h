#pragma once
#include "input.h"
#include <Arduino.h>
#include <functional>
#include <stdint.h>

// A request from the web page to one app: /api/app?app=<name>&op=<op>&...
// (see web.h). Apps answer from their own state, so the device and the page
// always agree.
struct WebCall {
    String op, body;                                // body: the POST text, if any
    std::function<String(const char*)> arg;         // a query argument, "" if missing
    int num(const char* k, int def = 0) const { const String v = arg(k); return v.length() ? v.toInt() : def; }
};

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
    // True while something moves on its own (a roll): the big screen then
    // keeps drawing at full speed instead of a few times a second.
    virtual bool animating() const { return false; }
    virtual void draw() = 0;
    // Draws itself for the big screen (ui::WIDE) when the device has one.
    virtual bool bigLayout() const { return false; }
    // The web page's side of the app: fill `out` (JSON, or plain text) and
    // return the HTTP status; 0 means the app has no web side. Op "hello"
    // asks whether it has one.
    virtual int web(const WebCall& c, String& out) { return 0; }
};

// The app with this name (as name() returns it), or nullptr.
App* findApp(const char* name);
// True when this app is the one on screen.
bool appOpen(const App* a);
// Every app, in launcher order.
void forEachApp(const std::function<void(App*)>& fn);

// Suspend the hang watchdog around long blocking work (e.g. SD format).
void watchdogPause(bool paused);
// Frame timing for the debug console's "perf" command (resets the counters).
void perfReport(char* out, size_t n);
// Ask the launcher to switch to the app with this name after the current event.
void requestApp(const char* name);

App* makeDiceApp();
App* makeInitiativeApp();
App* makeDungeonApp();
App* makeDungeon23App();
App* makeHexMapApp();
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
App* makeFoundryApp();
App* makeSettingsApp();

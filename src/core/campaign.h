#pragma once
#include <Arduino.h>
#include <vector>

// Campaigns and session plans, stored as readable text on the SD card:
//
//   /gmtools/campaigns/<id>/campaign.txt      campaign notes
//   /gmtools/campaigns/<id>/session_003.txt   one file per session
//   /gmtools/campaigns/current.txt            "<id>\n<session path>" (the plan
//                                             other apps add to)
// File format:
//   title: The Sunken Prison
//   number: 3
//   status: planned            (or played)
//   ## Secrets & clues
//   [ ] The warden is a doppelganger
//   [x] The key is hidden in the chapel
namespace campaign {

enum Kind { CAMPAIGN, SESSION };

struct Item { String text; bool done = false; };
struct Section { String name; std::vector<Item> items; };

struct Doc {
    Kind kind = SESSION;
    String title, path;
    int number = 0;
    bool played = false;
    std::vector<Section> sections;

    void init(Kind k);                 // empty doc with the standard sections
    Section* find(const String& name);
    bool load(const String& path, Kind k);
    bool save() const;
};

// Hint shown for an empty section, and what a ticked item means there.
const char* sectionHint(const String& section);
const char* checkLabel(const String& section);

struct Info { String id, title; int sessions = 0; };
std::vector<Info> campaigns();
String dirOf(const String& id);
String create(const String& title);                 // new campaign id, "" on failure
bool removeCampaign(const String& id);

struct SessionInfo { String path, title; int number = 0; bool played = false; };
std::vector<SessionInfo> sessions(const String& id);   // ascending by number
// Creates the next session. Unrevealed secrets carry over and the previous
// session's notes become the recap; `report` describes what was carried.
String newSession(const String& id, const String& title, String& report);

void setCurrent(const String& id, const String& sessionPath);
bool current(String& id, String& sessionPath);
// Appends an item to a section of the current session plan.
bool addToCurrentSession(const char* section, const String& text, String& sessionTitle);

}  // namespace campaign

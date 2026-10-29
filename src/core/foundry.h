#pragma once
#include <Arduino.h>

// Foundry VTT link: the latest data pushed by the "Cardputer Link" Foundry
// module (foundry/cardputer-link), held in RAM for the Foundry app.
//
// The module runs in the GM's browser and talks to web.cpp's /api/foundry/*
// routes, authenticated by a pairing key kept in NVS (not the web PIN):
//   POST /api/foundry/push?s=<section>   body = the section's text
//   GET  /api/foundry/poll               -> requests, one per line: "full",
//                                           "page\t<entry>\t<page>", "sheet\t<actor>"
// Section formats (fields separated by tabs, records by a line holding only \x1e):
//   actors   one line per actor: id, name, subtitle, hp, max, temp, ac
//   sheet    actor id, name + a TextView body (the one asked for)
//   combat   round, turn index, name; then per combatant:
//            name, init, hp, max, ac, flags (c current, h hidden, d defeated, p player), conditions
//   chat     id, time, speaker, flavor, formula, total, dice, flags (c crit, f fumble, w whisper, b blind)
//            + the message text, oldest first
//   journals entry id, page id, folder, entry name, page name (one line per text page)
//   page     entry id, page id, title + a TextView body
namespace foundry {

enum Section : uint8_t { ACTORS, COMBAT, CHAT, JOURNALS, PAGE, SHEET, NSECTIONS };

const String& text(Section s);
uint32_t version(Section s);   // changes with every push of that section
bool connected();              // the module polled in the last few seconds
bool everConnected();

String key();                  // pairing key (12 characters), made on first use
String keyGrouped();           // the same as "abcd-efgh-jkmn", for showing
void newKey();

// Asks the module for a journal page / a character sheet / everything on its
// next poll. The module keeps sending updates of the last sheet asked for.
void requestPage(const String& entry, const String& page);
void requestSheet(const String& actor);
void requestFull();

// For web.cpp
bool keyOk(const String& k);
bool store(const String& section, const String& body);   // false: unknown section or too big
String takeRequest();                                     // also records the contact

// Splits a line into its tab-separated fields.
void fields(const String& line, String* out, int n);

}  // namespace foundry

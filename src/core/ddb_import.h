#pragma once
#ifdef BOARD_TAB5
#include <Arduino.h>

// Import a character from D&D Beyond by its share link
// (https://www.dndbeyond.com/characters/<id>/<code>). The character must be
// public; the device needs the internet (web access joined to a home WiFi).
namespace ddb {

// The character id from a share link (or a bare id); false if there's none.
bool parseLink(const String& link, String& id);

// Download and save the character. A character imported before from the same
// id is updated in place (keeping its notes); otherwise a new file is made.
// path: the file written. msg: what happened, or why it failed.
bool import(const String& link, String& path, String& msg);

}  // namespace ddb
#endif

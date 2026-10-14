#pragma once
#include <Arduino.h>

// Helpers for turning spell text into rolls.
namespace spellmath {

// "8d6" + 2 x "1d6" -> "10d6" (falls back to "8d6+2d6" for mixed dice).
String addDice(const String& base, const String& per, int times);

// Per-slot-level increase parsed from a spell's "higher" text, e.g. "1d6"; "" if none.
String upcastDice(const String& higher);

// Damage for a cantrip at a character level, using "levels 5 (2d10), 11 (3d10), and 17 (4d10)".
String cantripDice(const String& base, const String& higher, int charLevel);

// Damage/heal expression for a spell cast with a slot of `slot` level (spell level `level`).
// `mod` replaces the "+MOD" marker (spellcasting modifier); pass 0 if unknown.
String castExpr(const String& base, const String& higher, int level, int slot, int charLevel, int mod);

}  // namespace spellmath

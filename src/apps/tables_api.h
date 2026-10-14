#pragma once
#include <Arduino.h>

// Roll on a Random Tables table by name (built-in, SRD or yours), expanding
// {references} and [dice]. Returns "" if there's no such table. The tables are
// loaded on first use; release them when done to free the memory.
String rollRandomTable(const String& name);
void releaseRandomTables();

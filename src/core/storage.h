#pragma once
#include <Arduino.h>
#include <vector>

// microSD helpers. All app data lives under /gmtools on the card:
//   /gmtools/tables/*.txt     random tables (one entry per line)
//   /gmtools/ref/<sys>/*.txt  quick-reference pages
//   /gmtools/dungeons/*.txt   saved 5-room dungeons
//   /gmtools/logs/            session roll logs
//   /gmtools/campaigns/<id>/  campaign notes and session plans
namespace storage {
bool begin();
bool ok();
void end();
// Erases the card and writes a fresh FAT32 filesystem (MBR partition table),
// then remounts. Works even if the card currently can't be mounted (e.g. exFAT).
// Blocks for several seconds to a minute depending on card size.
bool format(String& err);
uint64_t cardSizeMB();
// Reads non-empty lines that don't start with '#'.
bool readLines(const char* path, std::vector<String>& out, size_t maxLines = 400);
bool readText(const char* path, String& out, size_t maxBytes = 16384);
bool writeText(const char* path, const String& text);
bool appendLine(const char* path, const String& line);
// Lists file names (not dirs) in a directory, sorted.
bool listFiles(const char* dir, std::vector<String>& out, const char* ext = ".txt");
// Lists sub-directory names in a directory, sorted.
bool listDirs(const char* dir, std::vector<String>& out);
// Deletes a directory and the files directly inside it.
bool removeDir(const char* dir);
bool mkdir(const char* dir);
bool exists(const char* path);
bool remove(const char* path);
String sessionLogPath();
}  // namespace storage

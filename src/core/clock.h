#pragma once
#include <Arduino.h>
#include <time.h>

// Wall-clock time. The Tab5 keeps it in its RTC (stored as UTC); it is set
// from the internet (NTP) whenever web access joins a home network, or by
// hand in Settings. The time zone is one of a few presets (POSIX TZ rules,
// so daylight saving switches by itself). The Cardputer has no RTC: there
// the clock is only valid after an NTP sync, until power-off.
namespace wallclock {

struct Zone { const char* label; const char* tz; };
extern const Zone ZONES[];
extern const int NZONES;

void begin();            // time zone from settings, system time from the RTC
bool valid();            // the time has been set (from the RTC, NTP or by hand)
bool now(tm& out);       // local time; false if not valid
// "14:05", "Tue 27 Oct 2026", "2026-10-27" (empty when not valid)
String timeText();
String dateText();
String isoDate();

int zone();              // index into ZONES
void setZone(int i);
// Sets the local time by hand (also writes the RTC).
void setLocal(const tm& local);
// Web access joined a network with internet: ask NTP. update() writes the
// answer to the RTC once it arrives.
void syncFromInternet();
void update();
bool syncing();
uint32_t lastSyncMs();   // millis() of the last NTP answer, 0 if none yet

}  // namespace wallclock

#include "clock.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace wallclock {

const Zone ZONES[] = {
    {"UTC", "UTC0"},
    {"US Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"Arizona", "MST7"},
    {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Hawaii", "HST10"},
    {"Atlantic", "AST4ADT,M3.2.0,M11.1.0"},
    {"Brazil (Brasilia)", "<-03>3"},
    {"UK & Ireland", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "MSK-3"},
    {"India", "IST-5:30"},
    {"China", "CST-8"},
    {"Japan", "JST-9"},
    {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"New Zealand", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
const int NZONES = sizeof(ZONES) / sizeof(ZONES[0]);

static int s_zone = 0;
static bool s_syncing = false;
static uint32_t s_syncAt = 0, s_lastSync = 0;
static constexpr time_t EARLIEST = 1704067200;   // 2024-01-01: anything before is an unset clock

static void applyZone() {
    setenv("TZ", ZONES[s_zone].tz, 1);
    tzset();
}

// The RTC holds UTC.
static void writeRtc(time_t t) {
    if (!M5.Rtc.isEnabled()) return;
    tm u;
    gmtime_r(&t, &u);
    M5.Rtc.setDateTime(&u);
}

static void setSystem(time_t t) {
    timeval tv{t, 0};
    settimeofday(&tv, nullptr);
}

void begin() {
    Preferences p;
    p.begin("gmtools", true);
    s_zone = p.getUChar("tz", 0);
    p.end();
    if (s_zone >= NZONES) s_zone = 0;
    applyZone();
    if (!M5.Rtc.isEnabled()) return;
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.getDateTime(&dt) || dt.date.year < 2024) return;
    tm u{};
    u.tm_year = dt.date.year - 1900;
    u.tm_mon = dt.date.month - 1;
    u.tm_mday = dt.date.date;
    u.tm_hour = dt.time.hours;
    u.tm_min = dt.time.minutes;
    u.tm_sec = dt.time.seconds;
    // UTC to time_t without touching TZ: mktime treats tm as local, so undo the offset
    setenv("TZ", "UTC0", 1);
    tzset();
    const time_t t = mktime(&u);
    applyZone();
    setSystem(t);
}

bool valid() { return time(nullptr) >= EARLIEST; }

bool now(tm& out) {
    if (!valid()) return false;
    const time_t t = time(nullptr);
    localtime_r(&t, &out);
    return true;
}

static String fmt(const char* f) {
    tm t;
    if (!now(t)) return "";
    char b[40];
    strftime(b, sizeof(b), f, &t);
    return b;
}
String timeText() { return fmt("%H:%M"); }
String dateText() { return fmt("%a %d %b %Y"); }
String isoDate() { return fmt("%Y-%m-%d"); }

int zone() { return s_zone; }

void setZone(int i) {
    s_zone = (i + NZONES) % NZONES;
    applyZone();
    Preferences p;
    p.begin("gmtools", false);
    p.putUChar("tz", s_zone);
    p.end();
}

void setLocal(const tm& local) {
    tm t = local;
    t.tm_isdst = -1;   // let the zone's rules decide
    const time_t v = mktime(&t);
    if (v < EARLIEST) return;
    setSystem(v);
    writeRtc(v);
}

void syncFromInternet() {
    if (esp_sntp_enabled()) esp_sntp_stop();
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTzTime(ZONES[s_zone].tz, "pool.ntp.org", "time.google.com");
    s_syncing = true;
    s_syncAt = millis();
}

void update() {
    if (!s_syncing) return;
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && valid()) {
        s_syncing = false;
        s_lastSync = millis();
        writeRtc(time(nullptr));
        applyZone();
    } else if (millis() - s_syncAt > 30000) {
        s_syncing = false;   // no answer: keep what the RTC had
    }
}

bool syncing() { return s_syncing; }
uint32_t lastSyncMs() { return s_lastSync; }

}  // namespace wallclock

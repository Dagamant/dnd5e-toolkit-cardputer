#include "foundry.h"
#include <Preferences.h>

namespace foundry {

static const char* NAMES[NSECTIONS] = {"actors", "combat", "chat", "journals", "page", "sheet"};
// RAM budget per section; the module trims to fit
static const size_t LIMITS[NSECTIONS] = {6144, 3072, 6144, 6144, 10240, 10240};
static constexpr size_t SPARE = 16 * 1024;   // heap kept free for the app's views and WiFi

static String s_text[NSECTIONS];
static uint32_t s_version[NSECTIONS] = {0};
static String s_key, s_pageReq, s_sheetReq;
static bool s_full = true;   // nothing yet: ask for everything
static uint32_t s_lastPoll = 0;
static bool s_ever = false;

const String& text(Section s) { return s_text[s]; }
uint32_t version(Section s) { return s_version[s]; }
bool connected() { return s_ever && millis() - s_lastPoll < 6000; }
bool everConnected() { return s_ever; }

String key() {
    if (s_key.length()) return s_key;
    Preferences p;
    p.begin("gmtools", false);
    s_key = p.getString("fkey", "");
    p.end();
    if (s_key.length() != 12) newKey();   // also replaces the longer keys of the first version
    return s_key;
}

void newKey() {
    static const char* A = "abcdefghjkmnpqrstuvwxyz23456789";
    s_key = "";
    for (int i = 0; i < 12; i++) s_key += A[esp_random() % 31];
    Preferences p;
    p.begin("gmtools", false);
    p.putString("fkey", s_key);
    p.end();
}

void requestPage(const String& entry, const String& page) { s_pageReq = "page\t" + entry + "\t" + page; }
void requestSheet(const String& actor) { s_sheetReq = "sheet\t" + actor; }
void requestFull() { s_full = true; }

String keyGrouped() {
    String k = key();
    return k.substring(0, 4) + "-" + k.substring(4, 8) + "-" + k.substring(8);
}

bool keyOk(const String& typed) {
    String k;   // as typed in Foundry: dashes, spaces and case don't matter
    for (char c : typed) if (c != '-' && c != ' ') k += (char)tolower(c);
    String want = key();
    if (k.length() != want.length()) return false;
    uint8_t diff = 0;   // constant time
    for (size_t i = 0; i < k.length(); i++) diff |= k[i] ^ want[i];
    return !diff;
}

bool store(const String& section, const String& body) {
    for (int i = 0; i < NSECTIONS; i++) {
        if (section != NAMES[i]) continue;
        if (body.length() > LIMITS[i]) return false;
        // the old text is freed first, so it counts as free
        if (ESP.getFreeHeap() + s_text[i].length() < body.length() + SPARE) return false;
        s_text[i] = String();
        if (ESP.getMaxAllocHeap() < body.length() + 256) { s_version[i]++; return false; }   // no block that big
        s_text[i] = body;
        s_version[i]++;
        return true;
    }
    return false;
}

String takeRequest() {
    s_lastPoll = millis();
    s_ever = true;
    String r;
    for (String* q : {&s_pageReq, &s_sheetReq}) {
        if (q->length()) { r += (r.length() ? "\n" : "") + *q; *q = ""; }
    }
    if (s_full) { s_full = false; r = "full" + String(r.length() ? "\n" : "") + r; }
    return r;
}

void fields(const String& line, String* out, int n) {
    int start = 0;
    for (int i = 0; i < n; i++) {
        if (start < 0) { out[i] = ""; continue; }
        int tab = line.indexOf('\t', start);
        out[i] = tab < 0 ? line.substring(start) : line.substring(start, tab);
        start = tab < 0 ? -1 : tab + 1;
    }
}

}  // namespace foundry

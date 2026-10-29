#ifdef BOARD_TAB5
#include "dungeon23_tables.h"
#include "tables_api.h"
#include "../core/app.h"
#include "../core/clock.h"
#include "../core/db.h"
#include "../core/encounter.h"
#include "../core/encounter_gen.h"
#include "../core/hold_menu.h"
#include "../core/prompt.h"
#include "../core/rng.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/touch.h"
#include "../core/ui.h"
#include "../core/web.h"
#include "../core/widgets.h"
#include <algorithm>
#include <vector>

using namespace ui;

// Dungeon23: a megadungeon written one room a day - a level a month, twelve
// levels - after Sean McCoy's challenge and the "Dungeon 23" worksheets. Each
// room has the worksheet's fields (light, features, description, inhabitants,
// three features, three treasures) and can be one of its special pages
// (puzzle, set piece, faction lair). The last room of every level is its boss,
// who guards the way down. Each level also has a d8 wandering-encounter table
// with omens, and a checklist of what's still missing.
//
// The generators fill anything empty: monsters from the encounter generator
// sized to the party, traps and loot from the Random Tables (SRD), the rest
// from dungeon23_tables.h.
//
// "Run" shuffles the written rooms into a map - random connections, the
// entrance first, the boss deepest with the stairs down behind it - that the
// players explore room by room (also as a full-screen map to show them).
//
// Files: /gmtools/dungeon23/<id>/dungeon.txt and level01.txt ... level12.txt,
// "key: value" lines, a level's rooms under "## Room N" headings.
// Big screen only.

namespace {

const uint16_t ACCENT = rgb(226, 120, 80);
const char* const ROOT = "/gmtools/dungeon23";
constexpr int LEVELS = 12;
constexpr int MAX_ROOMS = 31;

// A small seeded generator for the run map, so a layout redraws the same.
struct Lcg {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    int below(int n) { return n > 0 ? (int)(next() % n) : 0; }
};

int daysIn(int year, int mon) {   // mon 0..11
    static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100) || year % 400 == 0;
    return D[mon] + (mon == 1 && leap);
}

String slugOf(const String& name) {
    String s;
    for (char c : name) {
        if (isalnum((unsigned char)c)) s += (char)tolower(c);
        else if (s.length() && s[s.length() - 1] != '_') s += '_';
        if (s.length() >= 24) break;
    }
    while (s.endsWith("_")) s.remove(s.length() - 1);
    return s.length() ? s : String("dungeon");
}

struct Level {
    db::Rec info;                 // name theme desc rooms seed explored check e1..e8 o1..o8
    std::vector<db::Rec> rooms;   // [key - 1]
};

// "key: value" lines; "## Room N" starts a room.
bool readLevel(const String& path, Level& lv) {
    lv = Level();
    lv.rooms.resize(MAX_ROOMS);
    String txt;
    if (!storage::readText(path.c_str(), txt, 65536)) return false;
    db::Rec* cur = &lv.info;
    int start = 0;
    while (start < (int)txt.length()) {
        int nl = txt.indexOf('\n', start);
        String l = nl < 0 ? txt.substring(start) : txt.substring(start, nl);
        start = nl < 0 ? txt.length() : nl + 1;
        l.trim();
        if (!l.length()) continue;
        if (l.startsWith("## Room ")) {
            const int k = l.substring(8).toInt();
            cur = k >= 1 && k <= MAX_ROOMS ? &lv.rooms[k - 1] : nullptr;
            continue;
        }
        const int c = l.indexOf(": ");
        if (cur && c > 0) cur->set(l.substring(0, c).c_str(), l.substring(c + 2));
    }
    return true;
}

String recText(const db::Rec& r) {
    String s;
    for (auto& kv : r.kv) s += kv.first + ": " + kv.second + "\n";
    return s;
}

bool writeLevel(const String& path, const Level& lv) {
    String s = recText(lv.info);
    for (int k = 0; k < MAX_ROOMS; k++)
        if (!lv.rooms[k].kv.empty()) s += String("\n## Room ") + (k + 1) + "\n" + recText(lv.rooms[k]);
    return storage::writeText(path.c_str(), s);
}

bool readRec(const String& path, db::Rec& r) {
    r = db::Rec();
    std::vector<String> lines;
    if (!storage::readLines(path.c_str(), lines, 200)) return false;
    for (auto& l : lines) {
        const int c = l.indexOf(": ");
        if (c > 0) r.set(l.substring(0, c).c_str(), l.substring(c + 2));
    }
    return true;
}

// A room has been written once it has a name.
bool written(const db::Rec& r) { return r.get("name").length() > 0; }
bool hasFeature(const db::Rec& r, const char* f) { return r.get("features").indexOf(f) >= 0; }
int kindOf(const db::Rec& r) {
    const String k = r.get("kind");
    for (int i = 0; i < 5; i++) if (k == d23::KINDS[i]) return i;
    return 0;
}

// What the worksheet calls each field, in order, for each kind of room.
struct Field { const char* key; const char* label; bool roll; };
const Field COMMON[] = {{"desc", "Description", true}, {"monsters", "Inhabitants", true}, {"f1", "Feature 1", true},
                        {"f2", "Feature 2", true}, {"f3", "Feature 3", true}, {"t1", "Treasure 1", true},
                        {"t2", "Treasure 2", true}, {"t3", "Treasure 3", true}};
const Field PUZZLE[] = {{"puzzle", "Puzzle", true}, {"solution", "Solutions", true}, {"failure", "Failure result", true}};
const Field SETPIECE[] = {{"behavior", "Behaviour & personality", true}, {"wants", "Wants", true}, {"fears", "Fears", true}};
const Field FACTION[] = {{"faction", "Faction", true}, {"leader", "Leader", true}, {"desires", "Desires", true},
                         {"defenses", "Lair defences", true}};

enum Tab { T_ROOMS, T_LEVEL, T_ENC, T_CHECK, T_RUN, NTABS };
const char* const TAB_NAMES[] = {"Rooms", "Level", "Encounters", "Checklist", "Run"};

class Dungeon23App : public App {
public:
    const char* name() const override { return "Dungeon23"; }
    const char* blurb() const override { return "A megadungeon, one room a day."; }
    uint16_t accent() const override { return ACCENT; }

    void drawIcon(int cx, int cy, float t) override {
        // steps down into the dark
        gfx.fillRoundRect(cx - 18, cy - 14, 36, 30, 4, rgb(30, 34, 52));
        for (int i = 0; i < 5; i++) gfx.fillRect(cx - 18 + i * 7, cy - 10 + i * 5, 9, 3, i % 2 ? C_DIM : C_TEXT);
        gfx.fillRect(cx + 4, cy - 20, 16, 13, rgb(236, 214, 164));
        gfx.fillRect(cx + 4, cy - 20, 16, 4, C_BLOOD);
    }

    bool bigLayout() const override { return true; }

    void onEnter() override {
        input::setTextMode(false);
        screen_ = LIST;
        loadList();
        if (!storage::ok()) toast("No SD card - see Settings", C_BLOOD);
    }
    void onExit() override {
        gen_.close();
        releaseRandomTables();
        input::setTextMode(false);
    }

    bool handle(const input::Event& e) override {
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done() && onPrompt_) { auto fn = onPrompt_; fn(prompt_.value()); }
            return true;
        }
        if (present_) {   // the players' map: Esc goes back to the DM's
            if (e.key == input::K_BACK) { present_ = false; sfx::back(); }
            return true;
        }
        switch (e.key) {
            case input::K_BACK:
                if (screen_ == LEVEL) { screen_ = HOME; sfx::back(); return true; }
                if (screen_ == HOME) { screen_ = LIST; loadList(); sfx::back(); return true; }
                return false;
            case input::K_LEFT: if (screen_ == LEVEL) { tab_ = (tab_ + NTABS - 1) % NTABS; sfx::click(); } return true;
            case input::K_RIGHT: case input::K_TAB: if (screen_ == LEVEL) { tab_ = (tab_ + 1) % NTABS; sfx::click(); } return true;
            case input::K_UP: case input::K_DOWN:
                if (screen_ == LEVEL && tab_ == T_ROOMS) {
                    const int d = e.key == input::K_UP ? -1 : 1;
                    room_ = std::max(1, std::min(target(), room_ + d));
                    edPos_ = 0;
                    sfx::click();
                }
                return true;
            case input::K_ENTER:
                if (screen_ == LEVEL && tab_ == T_ROOMS) { genRoom(room_, true); return true; }
                return true;
            default: return true;
        }
    }

    void draw() override {
        drawBackground();
        if (present_) drawPresent();
        else if (screen_ == LIST) drawList();
        else if (screen_ == HOME) drawHome();
        else drawLevel();
        prompt_.draw();
    }

private:
    enum Screen { LIST, HOME, LEVEL };
    Screen screen_ = LIST;
    Prompt prompt_;
    std::function<void(const String&)> onPrompt_;
    encounter::Generator gen_;

    void ask(const String& label, const String& cur, int maxLen, std::function<void(const String&)> fn) {
        onPrompt_ = std::move(fn);
        prompt_.open(label, cur, 1, maxLen);
    }

    // ================================================================ dungeons
    struct Info { String id, name, theme; int rooms = 0; };
    std::vector<Info> list_;
    float listPos_ = 0;

    void loadList() {
        list_.clear();
        std::vector<String> dirs;
        storage::listDirs(ROOT, dirs);
        for (auto& d : dirs) {
            db::Rec r;
            if (!readRec(String(ROOT) + "/" + d + "/dungeon.txt", r)) continue;
            list_.push_back({d, r.get("name", d.c_str()), r.get("theme"), r.num("written")});
        }
    }

    void newDungeon(String name) {
        name.trim();
        if (!storage::ok()) { sfx::error(); toast("No SD card", C_BLOOD); return; }
        if (!name.length()) name = String("The ") + rng::pick(d23::DUNGEON_ADJ) + " " + rng::pick(d23::DUNGEON_NOUN);
        String id = slugOf(name);
        for (int i = 2; storage::exists((String(ROOT) + "/" + id).c_str()); i++) id = slugOf(name) + "_" + i;
        storage::mkdir(ROOT);
        storage::mkdir((String(ROOT) + "/" + id).c_str());
        dungeon_ = db::Rec();
        id_ = id;
        dungeon_.set("name", name);
        dungeon_.set("party", "4");
        dungeon_.set("base", "1");
        if (wallclock::valid()) dungeon_.set("start", wallclock::isoDate());
        saveDungeon();
        openDungeon(id);
        sfx::select();
    }

    void drawList() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawHeader("Dungeon23", ACCENT);
        const int top = HEADER_H + 10, bottom = H - FOOTER_H - 10, n = list_.size();
        button(16, top, 380, 56, "+ New dungeon", false, C_GOLD, [this] {
            ask("Dungeon name (empty: roll one)", "", 40, [this](const String& v) { newDungeon(v); });
        });
        gfx.setFont(m.small);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString("A room a day, a level a month: a megadungeon in a year.", 416, top + 28);
        const int gy = top + 70, gh = bottom - gy, cw = (W - 48) / 2, ch = 132, gap = 14;
        const float maxPos = std::max(0, (n + 1) / 2 * (ch + gap) - gap - gh);
        listPos_ = std::max(0.0f, std::min(listPos_, maxPos + 60));
        touch::scroller(16, gy, W - 32, gh, &listPos_, maxPos);
        gfx.setClipRect(16, gy, W - 32, gh);
        for (int i = 0; i < n; i++) {
            const int x = 16 + (i % 2) * (cw + 16), y = gy + (i / 2) * (ch + gap) - (int)listPos_;
            if (y + ch < gy || y > gy + gh) continue;
            gfx.fillRoundRect(x, y, cw, ch, 12, C_BG2);
            gfx.drawRoundRect(x, y, cw, ch, 12, C_EDGE);
            gfx.fillRoundRect(x, y, 8, ch, 4, ACCENT);
            gfx.setTextDatum(top_left);
            gfx.setFont(m.title);
            gfx.setTextColor(C_TEXT);
            gfx.drawString(fitText(list_[i].name, cw - 44), x + 24, y + 14);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(list_[i].theme.length() ? list_[i].theme : String("No theme yet"), x + 24, y + 62);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(String(list_[i].rooms) + " of 365 rooms written", x + 24, y + 92);
            const int vy = std::max(y, gy), vh = std::min(y + ch, gy + gh) - vy;
            if (vh <= 0) continue;
            touch::zone(x, vy, cw, vh, [this, i] { openDungeon(list_[i].id); });
            touch::onHold(x, vy, cw, vh, [this, i] {
                const String id = list_[i].id;
                holdmenu::open(list_[i].name, {{"Open", [this, id] { openDungeon(id); }},
                                               {"Delete with all its levels", [this, id] {
                                                   const String dir = String(ROOT) + "/" + id;
                                                   storage::removeDir(dir.c_str());
                                                   loadList();
                                                   toast("Dungeon deleted", C_DIM);
                                               }, C_BLOOD, true}});
            });
        }
        gfx.clearClipRect();
        if (!n) {
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Write one room every day.", W / 2, gy + gh / 2 - 40);
            gfx.drawString("Every week an area, every month a level,", W / 2, gy + gh / 2);
            gfx.drawString("and a boss guarding the stairs down.", W / 2, gy + gh / 2 + 40);
        }
        drawFooter("Esc back");
    }

    // ================================================================ one dungeon
    String id_;
    db::Rec dungeon_;
    int levelRooms_[LEVELS] = {0}, levelDone_[LEVELS] = {0};   // for the level cards
    String levelName_[LEVELS];
    float introPos_ = 0;

    String dir() const { return String(ROOT) + "/" + id_; }
    String levelPath(int l) const { char b[16]; snprintf(b, sizeof(b), "/level%02d.txt", l); return dir() + b; }
    void saveDungeon() {
        int total = 0;
        for (int i = 0; i < LEVELS; i++) total += levelRooms_[i];
        dungeon_.set("written", String(total));
        storage::writeText((dir() + "/dungeon.txt").c_str(), recText(dungeon_));
    }

    int partySize() const { return std::max(1, std::min(10, dungeon_.num("party", 4))); }
    int partyLevel(int l) const { return std::max(1, std::min(20, dungeon_.num("base", 1) + l - 1)); }

    // The start date's month is level 1; each month after is the next level.
    bool startDate(int& y, int& m, int& d) const {
        const String s = dungeon_.get("start");
        if (s.length() != 10) return false;
        y = s.substring(0, 4).toInt();
        m = s.substring(5, 7).toInt() - 1;
        d = s.substring(8, 10).toInt();
        return y > 2000;
    }
    String monthOf(int l) const {
        static const char* const MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int y, m, d;
        if (!startDate(y, m, d)) return "";
        return MON[(m + l - 1) % 12];
    }
    int defaultRooms(int l) const {
        int y, m, d;
        if (!startDate(y, m, d)) return MAX_ROOMS;
        const int mm = m + l - 1;
        return daysIn(y + mm / 12, mm % 12);
    }
    // Today's level and room (a room a day: the day of the month).
    bool today(int& l, int& key) const {
        tm t;
        int y, m, d;
        if (!wallclock::now(t) || !startDate(y, m, d)) return false;
        l = (t.tm_year + 1900 - y) * 12 + t.tm_mon - m + 1;
        key = t.tm_mday;
        return l >= 1 && l <= LEVELS;
    }

    void openDungeon(const String& id) {
        loadDungeon(id);
        introPos_ = 0;
        screen_ = HOME;
        sfx::select();
    }

    bool loadDungeon(const String& id) {
        id_ = id;
        const bool ok = readRec(dir() + "/dungeon.txt", dungeon_);
        for (int l = 1; l <= LEVELS; l++) {
            Level lv;
            levelRooms_[l - 1] = levelDone_[l - 1] = 0;
            levelName_[l - 1] = "";
            if (!readLevel(levelPath(l), lv)) continue;
            summarize(l, lv);
        }
        return ok;
    }

    void summarize(int l, const Level& lv) {
        int n = 0;
        for (auto& r : lv.rooms) n += written(r);
        levelRooms_[l - 1] = n;
        levelName_[l - 1] = lv.info.get("name");
        int done = 0, total = 0;
        checklist(lv, l, nullptr, &done, &total);
        levelDone_[l - 1] = total ? done * 100 / total : 0;
    }

    void rollIntro(const char* key) {
        String k = key, v;
        if (k == "name") v = String("The ") + rng::pick(d23::DUNGEON_ADJ) + " " + rng::pick(d23::DUNGEON_NOUN);
        else if (k == "theme") v = rng::pick(d23::THEMES);
        else if (k == "history") v = rng::pick(d23::HISTORY);
        else if (k == "haven") v = rollRandomTable("Tavern") + " in the village of " + rollRandomTable("_last") + "'s Rest";
        else if (k.startsWith("h")) {
            char b[160];
            snprintf(b, sizeof(b), rng::pick(d23::HOOKS), 1 + rng::below(LEVELS));
            v = b;
        } else if (k.startsWith("r")) v = rollRandomTable("Rumor");
        if (!v.length()) { sfx::error(); return; }
        dungeon_.set(key, v);
        saveDungeon();
        sfx::diceRattle();
    }

    void drawHome() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawHeader(dungeon_.get("name", "Dungeon").c_str(), ACCENT);
        const int top = HEADER_H + 10, bottom = H - FOOTER_H - 10, LX = 16, LW = 560, RX = LX + LW + 16, RW = W - RX - 16;

        // today's room
        int tl, tk;
        const bool dated = today(tl, tk);
        drawPanel(LX, top, LW, 132, C_PANEL, ACCENT);
        gfx.setTextDatum(top_left);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString(dated ? "TODAY  " + wallclock::dateText() : String("TODAY"), LX + 18, top + 12);
        gfx.setFont(m.title);
        gfx.setTextColor(C_TEXT);
        if (dated) {
            gfx.drawString(String("Level ") + tl + ", room " + std::min(tk, MAX_ROOMS), LX + 18, top + 40);
            button(LX + LW - 216, top + 64, 200, 56, "Write it", true, C_GOLD, [this, tl, tk] { openLevel(tl, std::min(tk, MAX_ROOMS)); });
        } else {
            gfx.drawString(wallclock::valid() ? "Past the twelfth level" : "Set the clock for a room a day", LX + 18, top + 40);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(wallclock::valid() ? "Pick any level on the right." : "Settings > Date & time, or join WiFi.",
                           LX + 18, top + 84);
        }
        // the party (sizes the monsters and treasure)
        int y = top + 144;
        stepper(LX, y, LW / 2 - 6, 52, "Party", String(partySize()), [this](int d) {
            dungeon_.set("party", String(std::max(1, std::min(10, partySize() + d))));
            saveDungeon();
        }, 0, nullptr, 92);
        stepper(LX + LW / 2 + 6, y, LW / 2 - 6, 52, "Level", String(dungeon_.num("base", 1)), [this](int d) {
            dungeon_.set("base", String(std::max(1, std::min(20, dungeon_.num("base", 1) + d))));
            saveDungeon();
        }, 0, nullptr, 92);

        // the intro sheet: theme, history, haven, hooks and rumours
        y += 64;
        static const Field INTRO[] = {{"theme", "Theme", true}, {"history", "Origin & history", true},
                                      {"haven", "Nearest haven", true}, {"h1", "Hook 1", true}, {"h2", "Hook 2", true},
                                      {"h3", "Hook 3", true}, {"r1", "Rumour 1", true}, {"r2", "Rumour 2", true},
                                      {"r3", "Rumour 3", true}, {"name", "Dungeon name", true}};
        fieldList(LX, y, LW, bottom - y, INTRO, sizeof(INTRO) / sizeof(INTRO[0]), &introPos_,
                  [this](const char* k) -> String { return dungeon_.get(k); },
                  [this](const char* k, const String& v) { dungeon_.set(k, v); saveDungeon(); },
                  [this](const char* k) { rollIntro(k); });

        // the twelve levels
        const int cols = 3, gap = 12, cw = (RW - (cols - 1) * gap) / cols, ch = (bottom - top - 3 * gap) / 4;
        for (int l = 1; l <= LEVELS; l++) {
            const int x = RX + ((l - 1) % cols) * (cw + gap), yy = top + ((l - 1) / cols) * (ch + gap);
            const bool isToday = dated && tl == l;
            gfx.fillRoundRect(x, yy, cw, ch, 10, isToday ? C_PANEL : C_BG2);
            gfx.drawRoundRect(x, yy, cw, ch, 10, isToday ? ACCENT : C_EDGE);
            gfx.setTextDatum(top_left);
            gfx.setFont(m.small);
            gfx.setTextColor(ACCENT);
            const String mon = monthOf(l);
            gfx.drawString(String("Level ") + l + (mon.length() ? "  " + mon : String("")), x + 12, yy + 8);
            gfx.setFont(m.bold);
            gfx.setTextColor(levelName_[l - 1].length() ? C_TEXT : C_DIM);
            gfx.drawString(fitText(levelName_[l - 1].length() ? levelName_[l - 1] : String("Unwritten"), cw - 24), x + 12, yy + 36);
            const int rooms = levelRooms_[l - 1];
            drawBar(x + 12, yy + ch - 34, cw - 24, 10, rooms / (float)defaultRooms(l), C_GOLD, C_BG);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(rooms) + " rooms  " + levelDone_[l - 1] + "%", x + 12, yy + ch - 22);
            touch::zone(x, yy, cw, ch, [this, l] { openLevel(l, 1); });
        }
        drawFooter("Esc back");
    }

    // A scrolling list of worksheet fields: tap to write, the die to roll,
    // hold for both and Clear.
    void fieldList(int x, int y, int w, int h, const Field* fields, int n, float* pos,
                   std::function<String(const char*)> get, std::function<void(const char*, const String&)> set,
                   std::function<void(const char*)> roll) {
        const Metrics& m = metrics();
        const int tw = w - 32 - 70;
        gfx.setFont(m.body);
        std::vector<std::vector<String>> lines(n);
        std::vector<int> tops(n + 1, 0);
        for (int i = 0; i < n; i++) {
            const String v = get(fields[i].key);
            wrapLines(v.length() ? v : String("-"), tw, lines[i]);
            if (lines[i].size() > 4) lines[i].resize(4);
            tops[i + 1] = tops[i] + 44 + (int)lines[i].size() * m.line + 10;
        }
        const float maxPos = std::max(0, tops[n] - 10 - h);
        *pos = std::max(0.0f, std::min(*pos, maxPos + 60));
        touch::scroller(x, y, w, h, pos, maxPos);
        gfx.setClipRect(x, y, w, h);
        for (int i = 0; i < n; i++) {
            const int cy = y + tops[i] - (int)*pos, ch = tops[i + 1] - tops[i] - 10;
            if (cy + ch < y || cy > y + h) continue;
            const Field& f = fields[i];
            const String v = get(f.key);
            gfx.fillRoundRect(x, cy, w, ch, 10, C_BG2);
            gfx.drawRoundRect(x, cy, w, ch, 10, C_EDGE);
            gfx.setTextDatum(top_left);
            gfx.setFont(m.small);
            gfx.setTextColor(ACCENT);
            gfx.drawString(f.label, x + 16, cy + 10);
            gfx.setFont(m.body);
            gfx.setTextColor(v.length() ? C_TEXT : C_EDGE);
            for (size_t k = 0; k < lines[i].size(); k++) gfx.drawString(lines[i][k], x + 16, cy + 38 + k * m.line);
            const int vy = std::max(cy, y), vh = std::min(cy + ch, y + h) - vy;
            if (vh <= 0) continue;
            const char* key = f.key;
            const String label = f.label;
            touch::zone(x, vy, w - 72, vh, [this, key, label, get, set] {
                ask(label, get(key), 200, [key, set](const String& nv) { String t = nv; t.trim(); set(key, t); });
            });
            touch::onHold(x, vy, w, vh, [this, key, label, get, set, roll] {
                holdmenu::open(label, {{"Write", [this, key, label, get, set] {
                                            ask(label, get(key), 200, [key, set](const String& nv) { String t = nv; t.trim(); set(key, t); });
                                        }},
                                       {"Roll", [key, roll] { roll(key); }},
                                       {"Clear", [key, set] { set(key, ""); }, C_BLOOD}});
            });
            if (f.roll) dieButton(x + w - 64, cy + (ch - 52) / 2, y, y + h, [key, roll] { roll(key); });
        }
        gfx.clearClipRect();
    }

    // A small d20 button that rolls something; only tappable inside [clipTop, clipBottom).
    void dieButton(int x, int y, int clipTop, int clipBottom, std::function<void()> fn) {
        gfx.fillRoundRect(x, y, 52, 52, 10, C_PANEL);
        gfx.drawRoundRect(x, y, 52, 52, 10, C_EDGE);
        drawDieShape(20, x + 26, y + 26, 17, C_BG, ACCENT);
        const int vy = std::max(y, clipTop), vh = std::min(y + 52, clipBottom) - vy;
        if (vh > 0) touch::zone(x, vy, 52, vh, [fn] { fn(); });
    }

    // ================================================================ a level
    int level_ = 1, room_ = 1, tab_ = T_ROOMS;
    Level lv_;
    float edPos_ = 0, levPos_ = 0, encPos_ = 0, chkPos_ = 0;
    int encHit_ = -1;   // the d8 row rolled last

    int target() const { return std::max(3, std::min(MAX_ROOMS, lv_.info.num("rooms", defaultRooms(level_)))); }
    db::Rec& room(int key) { return lv_.rooms[key - 1]; }
    bool isBoss(int key) const { return key == target(); }

    void loadLevel(int l) {
        level_ = l;
        readLevel(levelPath(l), lv_);
        if (!lv_.info.has("rooms")) lv_.info.set("rooms", String(defaultRooms(l)));
        nodes_.clear();
        layoutSeed_ = 0;
    }

    void openLevel(int l, int key) {
        loadLevel(l);
        room_ = std::max(1, std::min(target(), key));
        edPos_ = levPos_ = encPos_ = chkPos_ = 0;
        encHit_ = -1;
        runSel_ = -1;
        layoutSeed_ = 0;
        screen_ = LEVEL;
        tab_ = T_ROOMS;
        sfx::select();
    }

    bool batch_ = false;   // stocking a whole level: save once at the end
    void saveLevel() {
        if (batch_) return;
        writeLevel(levelPath(level_), lv_);
        summarize(level_, lv_);
        saveDungeon();
    }

    // A room changed: date it (the day it was written) and save.
    void changedRoom(int key) {
        db::Rec& r = room(key);
        if (!r.has("date") && wallclock::valid()) r.set("date", wallclock::isoDate());
        if (isBoss(key)) r.set("kind", "boss");
        saveLevel();
    }

    // ---------------------------------------------------------------- generators
    String themeWord() {
        const String t = lv_.info.get("theme", dungeon_.get("theme").c_str());
        return t.length() ? t : String(rng::pick(d23::THEMES));
    }

    String monstersFor(int diff, int group, String* activity = nullptr) {
        encounter::GenOptions o;
        o.partySize = partySize();
        o.partyLevel = partyLevel(level_);
        o.habitat = level_ >= 4 && rng::below(2) ? 11 : 4;   // Underdark deeper down, else Dungeon
        o.diff = diff;
        o.group = group;
        encounter::GenResult res;
        if (!gen_.generate(o, res)) return "";
        if (activity) *activity = rng::pick(d23::ACTIVITY);
        return encounter::describe(res.monsters);
    }

    String trapFor() {
        const int lv = partyLevel(level_);
        const char* band = lv <= 4 ? "Levels 1-4" : lv <= 10 ? "Levels 5-10" : lv <= 16 ? "Levels 11-16" : "Levels 17-20";
        String t;
        for (int i = 0; i < 12; i++) {   // the SRD traps come in level bands: prefer the party's
            t = rollRandomTable("Trap");
            if (t.indexOf(band) >= 0) break;
        }
        return t;
    }

    String lootFor(bool hoard) {
        const int lv = std::min(20, partyLevel(level_) + (hoard ? 2 : 0));
        return rollRandomTable(lv <= 4 ? "Loot (Tier 1, levels 1-4)" : lv <= 10 ? "Loot (Tier 2, levels 5-10)"
                               : lv <= 16 ? "Loot (Tier 3, levels 11-16)" : "Loot (Tier 4, levels 17-20)");
    }

    String roomName(int kind) {
        if (kind == 4) return String(rng::pick(d23::ROOM_ADJ)) + " Throne Room";
        if (kind == 3) return String("Lair of the ") + rng::pick(d23::FACTION_ADJ) + " " + rng::pick(d23::FACTION_NOUN);
        if (rng::below(3) == 0) return themeWord() + " " + rng::pick(d23::ROOM_NOUN);
        return String(rng::pick(d23::ROOM_ADJ)) + " " + rng::pick(d23::ROOM_NOUN);
    }

    String description() {
        return String(rng::pick(d23::SIGHTS)) + " Smells of " + rng::pick(d23::SMELLS) + "; " + rng::pick(d23::SOUNDS) + ".";
    }

    // Classic stocking (a d6: monster, monster, trap, special, empty, empty),
    // with treasure more likely beside monsters.
    String stock(int kind) {
        std::vector<String> f;
        if (kind == 4 || kind == 2) { f = {"Monster", "Treasure"}; }
        else if (kind == 1) { f = {"Special"}; if (rng::below(2)) f.push_back("Treasure"); }
        else if (kind == 3) { f = {"Monster"}; if (rng::below(10) < 6) f.push_back("Treasure"); }
        else {
            const int d = rng::roll(6);
            if (d <= 2) { f.push_back("Monster"); if (rng::below(2)) f.push_back("Treasure"); }
            else if (d == 3) { f.push_back("Trap"); if (rng::below(3) == 0) f.push_back("Treasure"); }
            else if (d == 4) f.push_back("Special");
            else if (rng::below(6) == 0) f.push_back("Treasure");
        }
        String s;
        for (auto& x : f) s += (s.length() ? "," : "") + x;
        return s.length() ? s : String("Empty");
    }

    // Fills one field of a room (by its key).
    void rollField(int key, const String& k) {
        db::Rec& r = room(key);
        const int kind = isBoss(key) ? 4 : kindOf(r);
        String v;
        if (k == "name") v = roomName(kind);
        else if (k == "desc") v = description();
        else if (k == "light") {
            static const uint8_t W8[] = {45, 30, 15, 5, 5};
            int x = rng::below(100), i = 0;
            while (i < 4 && x >= W8[i]) x -= W8[i++];
            v = d23::LIGHTS[i];
        } else if (k == "features") v = stock(kind);
        else if (k == "monsters") {
            String act;
            v = kind == 4 ? monstersFor(2, rng::below(2) ? 1 : 2, &act) : kind == 2 ? monstersFor(2, 1, &act)
                : kind == 3 ? monstersFor(1, rng::below(2) ? 2 : 3, &act) : monstersFor(rng::below(2), 0, &act);
            if (v.length() && !r.get("f1").length()) r.set("f1", act);
        } else if (k == "f1" || k == "f2" || k == "f3") {
            // the features worth more words: monsters' doings, a trap, the special, a secret
            if (k == "f1" && hasFeature(r, "Monster")) v = rng::pick(d23::ACTIVITY);
            else if (k != "f3" && hasFeature(r, "Trap")) v = "Trap: " + trapFor();
            else if (k != "f3" && hasFeature(r, "Special")) v = rng::pick(d23::SPECIALS);
            else v = String("Secret: ") + rng::pick(d23::SECRETS);
        } else if (k == "t1" || k == "t2" || k == "t3") v = lootFor(kind == 4 || kind == 2);
        else if (k == "puzzle" || k == "solution" || k == "failure") {
            const auto& p = d23::PUZZLES[rng::below(sizeof(d23::PUZZLES) / sizeof(d23::PUZZLES[0]))];
            r.set("puzzle", p.puzzle);
            r.set("solution", p.solution);
            r.set("failure", p.failure);
            changedRoom(key);
            sfx::diceRattle();
            return;
        } else if (k == "behavior") v = rng::pick(d23::BEHAVIOR);
        else if (k == "wants") v = String("Wants ") + rng::pick(d23::WANTS) + ".";
        else if (k == "fears") v = String("Fears ") + rng::pick(d23::FEARS) + ".";
        else if (k == "faction") v = String("The ") + rng::pick(d23::FACTION_ADJ) + " " + rng::pick(d23::FACTION_NOUN);
        else if (k == "leader") v = rollRandomTable("NPC");
        else if (k == "desires") v = String("They want ") + rng::pick(d23::DESIRES) + ".";
        else if (k == "defenses") v = rng::pick(d23::DEFENSES);
        if (!v.length()) { sfx::error(); toast("Nothing to roll (SD card?)", C_BLOOD); return; }
        r.set(k.c_str(), v);
        changedRoom(key);
        sfx::diceRattle();
    }

    // Fills everything still empty in a room (or rerolls it all).
    void genRoom(int key, bool onlyEmpty) {
        db::Rec& r = room(key);
        if (!onlyEmpty) {
            const String date = r.get("date");
            r = db::Rec();
            if (date.length()) r.set("date", date);
        }
        if (isBoss(key)) r.set("kind", "boss");
        else if (!r.has("kind")) {
            const int d = rng::below(100);
            r.set("kind", d < 80 ? "room" : d < 87 ? "puzzle" : d < 94 ? "setpiece" : "faction");
        }
        const int kind = kindOf(r);
        auto need = [&](const char* k) { if (!r.get(k).length()) rollField(key, k); };
        need("name");
        need("light");
        need("features");
        need("desc");
        if (hasFeature(r, "Monster")) need("monsters");
        need("f1");
        if (hasFeature(r, "Trap") || hasFeature(r, "Special") || rng::below(3) == 0) need("f2");
        if (rng::below(4) == 0) need("f3");
        if (hasFeature(r, "Treasure")) {
            need("t1");
            if (kind == 4 || kind == 2) { need("t2"); need("t3"); }
        }
        if (kind == 1) need("puzzle");
        if (kind == 2 || kind == 4) { need("behavior"); need("wants"); need("fears"); }
        if (kind == 3) { need("faction"); need("leader"); need("desires"); need("defenses"); }
        changedRoom(key);
        sfx::crit();
    }

    void rollLevel(const char* key) {
        const String k = key;
        String v;
        if (k == "name") v = String("The ") + themeWord() + " " + rng::pick(d23::LEVEL_NOUN);
        else if (k == "theme") v = rng::pick(d23::THEMES);
        else if (k == "desc") v = String(rng::pick(d23::HISTORY)) + " " + rng::pick(d23::SIGHTS);
        if (!v.length()) return;
        lv_.info.set(key, v);
        saveLevel();
        sfx::diceRattle();
    }

    void rollEncounter(int i) {   // 1..8
        String act;
        const String mon = monstersFor(rng::below(2), 0, &act);
        if (!mon.length()) { sfx::error(); toast("No monsters fit (SD card?)", C_BLOOD); return; }
        lv_.info.set((String("e") + i).c_str(), mon);
        // the omen names the biggest creature
        String what = mon;
        const int comma = what.indexOf(',');
        if (comma > 0) what = what.substring(0, comma);
        const int x = what.indexOf("x ");
        if (x > 0 && x < 4) what = what.substring(x + 2);
        char b[160];
        snprintf(b, sizeof(b), rng::pick(d23::OMENS), what.c_str());
        lv_.info.set((String("o") + i).c_str(), b);
        saveLevel();
        sfx::diceRattle();
    }

    // Stocks every room still empty (and makes sure there's a puzzle and a lair).
    void stockLevel() {
        int puzzles = 0, lairs = 0;
        std::vector<int> empty;
        batch_ = true;
        for (int k = 1; k <= target(); k++) {
            const db::Rec& r = room(k);
            puzzles += kindOf(r) == 1;
            lairs += kindOf(r) == 3;
            if (!written(r) && !isBoss(k)) empty.push_back(k);
        }
        if (!puzzles && !empty.empty()) room(empty[rng::below(empty.size())]).set("kind", "puzzle");
        if (!lairs && empty.size() > 1) {
            int k;
            do k = empty[rng::below(empty.size())]; while (room(k).get("kind") == "puzzle");
            room(k).set("kind", "faction");
        }
        for (int k = 1; k <= target(); k++) if (!written(room(k))) genRoom(k, true);
        for (int i = 1; i <= 8; i++) if (!lv_.info.get((String("e") + i).c_str()).length()) rollEncounter(i);
        for (const char* k : {"theme", "name", "desc"}) if (!lv_.info.get(k).length()) rollLevel(k);
        batch_ = false;
        saveLevel();
        toast("Level stocked", C_EMERALD);
    }

    void fight(const String& text) {
        String unknown;
        auto list = encounter::parseList(text, unknown);
        if (list.empty()) { sfx::error(); toast("No monsters found (e.g. 3x Goblin Warrior)", C_BLOOD); return; }
        encounter::enqueue(list, true);
        if (unknown.length()) toast((String("Unknown: ") + unknown).c_str(), C_GOLD);
        sfx::crit();
        requestApp("Initiative");
    }

    // ---------------------------------------------------------------- checklist
    struct Check { String label, detail; bool done, manual; const char* key; };

    // Fills `out` (when given) and counts what's done.
    void checklist(const Level& lv, int l, std::vector<Check>* out, int* done, int* total) const {
        const int tgt = std::max(3, std::min(MAX_ROOMS, lv.info.num("rooms", defaultRooms(l))));
        int rooms = 0, traps = 0, treasure = 0, puzzles = 0, lairs = 0, enc = 0, omens = 0;
        for (int k = 0; k < tgt; k++) {
            const db::Rec& r = lv.rooms[k];
            if (!written(r)) continue;
            rooms++;
            traps += hasFeature(r, "Trap");
            treasure += hasFeature(r, "Treasure");
            puzzles += r.get("kind") == "puzzle";
            lairs += r.get("kind") == "faction";
        }
        for (int i = 1; i <= 8; i++) {
            enc += lv.info.get((String("e") + i).c_str()).length() > 0;
            omens += lv.info.get((String("o") + i).c_str()).length() > 0;
        }
        const db::Rec& boss = lv.rooms[tgt - 1];
        const String manual = lv.info.get("check");
        std::vector<Check> c = {
            {"Name and theme", lv.info.get("name") + (lv.info.get("theme").length() ? " - " + lv.info.get("theme") : String("")),
             lv.info.get("name").length() && lv.info.get("theme").length(), false, nullptr},
            {"Level description", "", lv.info.get("desc").length() > 0, false, nullptr},
            {"Every room written", String(rooms) + " of " + tgt, rooms >= tgt, false, nullptr},
            {"Boss guards the stairs down", boss.get("monsters").length() ? boss.get("monsters") : String("Room ") + tgt,
             boss.get("monsters").length() > 0, false, nullptr},
            {"Boss hoard", "", boss.get("t1").length() > 0, false, nullptr},
            {"Wandering encounters (d8)", String(enc) + " of 8", enc == 8, false, nullptr},
            {"Omens for them", String(omens) + " of 8", omens == 8, false, nullptr},
            {"A puzzle room", "", puzzles > 0, false, nullptr},
            {"A faction lair", "", lairs > 0, false, nullptr},
            {"Traps (3 or more)", String(traps), traps >= 3, false, nullptr},
            {"Treasure in a third of the rooms", String(treasure) + " rooms", treasure * 3 >= tgt, false, nullptr},
            {"Map sketched", "Tick it yourself", manual.indexOf("map") >= 0, true, "map"},
            {"Played through", "Tick it yourself", manual.indexOf("played") >= 0, true, "played"},
        };
        int d = 0;
        for (auto& x : c) d += x.done;
        if (done) *done = d;
        if (total) *total = c.size();
        if (out) *out = std::move(c);
    }

    // ---------------------------------------------------------------- drawing
    void drawLevel() {
        using namespace widgets;
        const Metrics& m = metrics();
        const String name = lv_.info.get("name");
        drawHeader((String("Level ") + level_ + (name.length() ? ": " + name : String(""))).c_str(), ACCENT);
        const int top = HEADER_H + 8;
        const int tw = (W - 32 - (NTABS - 1) * 8) / NTABS;
        for (int t = 0; t < NTABS; t++)
            button(16 + t * (tw + 8), top, tw, 52, TAB_NAMES[t], tab_ == t, ACCENT, [this, t] { tab_ = t; sfx::click(); });
        const int y = top + 64, h = H - FOOTER_H - 10 - y;
        switch (tab_) {
            case T_ROOMS: drawRooms(y, h); break;
            case T_LEVEL: drawLevelSheet(y, h); break;
            case T_ENC: drawEncounters(y, h); break;
            case T_CHECK: drawChecklist(y, h); break;
            default: drawRun(y, h); break;
        }
        (void)m;
    }

    // The rooms as a calendar (a row a week) beside the open room's sheet.
    void drawRooms(int y, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        const int LX = 16, cols = 7, gap = 8, tw = 84, th = std::min(84, (h - 4 * gap - 40) / 5), LW = cols * tw + (cols - 1) * gap;
        int tl, tk;
        const bool dated = today(tl, tk) && tl == level_;
        gfx.setFont(m.small);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        int n = 0;
        for (int k = 1; k <= target(); k++) n += written(room(k));
        gfx.drawString(String(n) + " of " + target() + " rooms   party " + partySize() + " x level " + partyLevel(level_), LX, y);
        const int gy = y + 32;
        for (int k = 1; k <= target(); k++) {
            const int x = LX + ((k - 1) % cols) * (tw + gap), yy = gy + ((k - 1) / cols) * (th + gap);
            const db::Rec& r = room(k);
            const bool on = k == room_, w = written(r), boss = isBoss(k);
            gfx.fillRoundRect(x, yy, tw, th, 8, on ? C_PANEL : w ? C_BG2 : C_BG);
            gfx.drawRoundRect(x, yy, tw, th, 8, on ? ACCENT : boss ? C_BLOOD : dated && tk == k ? C_GOLD : C_EDGE);
            if (dated && tk == k) gfx.drawRoundRect(x + 2, yy + 2, tw - 4, th - 4, 6, C_GOLD);
            gfx.setFont(m.bold);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(boss ? C_BLOOD : w ? C_TEXT : C_EDGE);
            gfx.drawString(String(k), x + 8, yy + 6);
            if (boss) {   // a little crown
                const int cx = x + tw - 26, cy = yy + 12;
                gfx.fillTriangle(cx, cy + 10, cx + 4, cy, cx + 8, cy + 10, C_GOLD);
                gfx.fillTriangle(cx + 6, cy + 10, cx + 10, cy - 2, cx + 14, cy + 10, C_GOLD);
                gfx.fillRect(cx, cy + 9, 18, 4, C_GOLD);
            }
            // a dot per feature
            static const uint16_t FC[] = {C_BLOOD, C_GOLD, C_EMERALD, C_ARCANE};
            for (int f = 0, dx = 0; f < 4; f++)
                if (hasFeature(r, d23::FEATURES[f])) { gfx.fillCircle(x + 12 + dx, yy + th - 14, 6, FC[f]); dx += 16; }
            if (!w) {
                gfx.setFont(m.small);
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(C_EDGE);
                gfx.drawString("+", x + tw / 2 + 12, yy + th / 2 + 6);
            }
            touch::zone(x, yy, tw, th, [this, k] { room_ = k; edPos_ = 0; sfx::click(); });
            touch::onHold(x, yy, tw, th, [this, k] {
                room_ = k;
                holdmenu::open(String("Room ") + k, {{"Fill what's empty", [this, k] { genRoom(k, true); }},
                                                     {"Roll it all again", [this, k] { genRoom(k, false); }, 0, true},
                                                     {"Clear", [this, k] { lv_.rooms[k - 1] = db::Rec(); saveLevel(); }, C_BLOOD, true}});
            });
        }
        // legend
        const int ly = gy + 5 * (th + gap) + 4;
        static const uint16_t FC[] = {C_BLOOD, C_GOLD, C_EMERALD, C_ARCANE};
        gfx.setFont(m.small);
        gfx.setTextDatum(middle_left);
        for (int f = 0, lx = LX; f < 4; f++) {
            gfx.fillCircle(lx + 6, ly + 10, 6, FC[f]);
            gfx.setTextColor(C_DIM);
            gfx.drawString(d23::FEATURES[f], lx + 18, ly + 10);
            lx += 34 + gfx.textWidth(d23::FEATURES[f]);
        }

        drawRoomSheet(LX + LW + 16, y, W - 16 - (LX + LW + 16), h);
        drawFooter("Up/Dn room  Ent fill  </> tab  hold for more  Esc back");
    }

    void drawRoomSheet(int x, int y, int w, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        const int key = room_;
        db::Rec& r = room(key);
        const bool boss = isBoss(key);
        const int kind = boss ? 4 : kindOf(r);
        drawPanel(x, y, w, h, C_BG2, boss ? C_BLOOD : C_EDGE);
        // key, name, kind
        gfx.setTextDatum(top_left);
        gfx.setFont(m.small);
        gfx.setTextColor(boss ? C_BLOOD : ACCENT);
        String sub = String("KEY ") + key + "  " + d23::KIND_NAMES[kind];
        if (r.get("date").length()) sub += "  written " + r.get("date");
        gfx.drawString(sub, x + 16, y + 10);
        gfx.setFont(m.title);
        gfx.setTextColor(written(r) ? C_TEXT : C_DIM);
        const String nm = r.get("name");
        gfx.drawString(fitText(nm.length() ? nm : String("Unwritten room"), w - 32 - 64), x + 16, y + 34);
        touch::zone(x, y, w - 80, 80, [this, key] {
            ask("Room name", room(key).get("name"), 60, [this, key](const String& v) {
                String t = v;
                t.trim();
                room(key).set("name", t);
                changedRoom(key);
            });
        });
        dieButton(x + w - 68, y + 14, y, y + h, [this, key] { rollField(key, "name"); });

        // kind and light as pickers, the features as chips
        int cy = y + 84;
        const int hw = (w - 32 - 8) / 2;
        button(x + 16, cy, hw, 48, boss ? "Boss" : d23::KIND_NAMES[kind], true, boss ? C_BLOOD : ACCENT, [this, key] {
            if (isBoss(key)) { toast("The last room is always the boss", C_BLOOD); return; }
            std::vector<holdmenu::Item> items;
            for (int i = 0; i < 4; i++)
                items.push_back({d23::KIND_NAMES[i], [this, key, i] { room(key).set("kind", d23::KINDS[i]); changedRoom(key); }});
            holdmenu::open("Kind of room", items);
        });
        const String light = r.get("light");
        button(x + 16 + hw + 8, cy, hw, 48, light.length() ? light : String("Light?"), light.length() > 0, C_ICE, [this, key] {
            std::vector<holdmenu::Item> items;
            for (int i = 0; i < 5; i++)
                items.push_back({d23::LIGHTS[i], [this, key, i] { room(key).set("light", d23::LIGHTS[i]); changedRoom(key); }});
            holdmenu::open("Light", items);
        });
        cy += 56;
        const int fw = (w - 32 - 3 * 6) / 4;
        for (int i = 0; i < 4; i++)
            button(x + 16 + i * (fw + 6), cy, fw, 44, d23::FEATURES[i], hasFeature(r, d23::FEATURES[i]), C_GOLD, [this, key, i] {
                db::Rec& rr = room(key);
                String f = rr.get("features"), out;
                const bool on = f.indexOf(d23::FEATURES[i]) >= 0;
                for (int j = 0; j < 4; j++) {
                    const bool has = j == i ? !on : f.indexOf(d23::FEATURES[j]) >= 0;
                    if (has) out += (out.length() ? "," : "") + String(d23::FEATURES[j]);
                }
                rr.set("features", out.length() ? out : String("Empty"));
                changedRoom(key);
                sfx::click();
            });
        cy += 54;

        // the fields of this kind of room
        std::vector<Field> fields(COMMON, COMMON + 8);
        if (kind == 1) fields.insert(fields.begin() + 1, PUZZLE, PUZZLE + 3);
        if (kind == 2 || kind == 4) fields.insert(fields.begin() + 2, SETPIECE, SETPIECE + 3);
        if (kind == 3) fields.insert(fields.begin() + 2, FACTION, FACTION + 4);
        const int bh = 60, fy = cy, fh = y + h - 12 - bh - 8 - fy;
        fieldList(x + 12, fy, w - 24, fh, fields.data(), fields.size(), &edPos_,
                  [this, key](const char* k) -> String { return room(key).get(k); },
                  [this, key](const char* k, const String& v) { room(key).set(k, v); changedRoom(key); },
                  [this, key](const char* k) { rollField(key, k); });

        // what to do with it
        const int by = y + h - 12 - bh, bw3 = (w - 32 - 2 * 8) / 3;
        button(x + 16, by, bw3, bh, written(r) ? "Fill empty" : "Generate", true, C_GOLD, [this, key] { genRoom(key, true); });
        button(x + 16 + bw3 + 8, by, bw3, bh, "Fight!", false, C_BLOOD, [this, key] { fight(room(key).get("monsters")); },
               r.get("monsters").length() > 0);
        button(x + 16 + 2 * (bw3 + 8), by, bw3, bh, "Next room", false, ACCENT, [this] {
            room_ = room_ % target() + 1;
            edPos_ = 0;
            sfx::click();
        });
    }

    void drawLevelSheet(int y, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        const int LX = 16, LW = 700, RX = LX + LW + 16, RW = W - RX - 16;
        static const Field F[] = {{"name", "Level name", true}, {"theme", "Theme", true}, {"desc", "Level description", true}};
        fieldList(LX, y, LW, h, F, 3, &levPos_, [this](const char* k) -> String { return lv_.info.get(k); },
                  [this](const char* k, const String& v) { lv_.info.set(k, v); saveLevel(); },
                  [this](const char* k) { rollLevel(k); });
        int yy = y;
        stepper(RX, yy, RW, 56, "Rooms", String(target()), [this](int d) {
            lv_.info.set("rooms", String(std::max(3, std::min(MAX_ROOMS, target() + d))));
            room_ = std::min(room_, target());
            saveLevel();
        }, 0, nullptr, 120);
        yy += 66;
        gfx.setFont(m.small);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        yy = textWrapped((String("The last room (") + target() + ") is the boss, who guards the stairs down. Monsters are sized for " +
                          partySize() + " heroes of level " + partyLevel(level_) + ".").c_str(), RX, yy, RW, 28, 3) + 16;
        button(RX, yy, RW, 64, "Stock every empty room", true, C_GOLD, [this] {
            holdmenu::open("Stock the level?", {{"Fill every empty room", [this] { stockLevel(); }, 0, true}});
        });
        yy += 76;
        button(RX, yy, RW, 56, "Write the boss room", false, C_BLOOD, [this] { room_ = target(); tab_ = T_ROOMS; edPos_ = 0; });
        yy += 66;
        button(RX, yy, RW, 56, level_ < LEVELS ? "Next level down" : "Back to the dungeon", false, ACCENT, [this] {
            if (level_ < LEVELS) openLevel(level_ + 1, 1);
            else screen_ = HOME;
        });
        drawFooter("</> tab  hold for more  Esc back");
    }

    void drawEncounters(int y, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        const int LX = 16, LW = W - 32;
        button(LX, y, 220, 56, "Roll d8", true, C_GOLD, [this] {
            encHit_ = rng::roll(8);
            const String o = lv_.info.get((String("o") + encHit_).c_str());
            toast(o.length() ? o.c_str() : "Nothing written for that one yet", C_GOLD);
            sfx::diceRattle();
        });
        button(LX + 232, y, 280, 56, "Fill empty rows", false, ACCENT, [this] {
            for (int i = 1; i <= 8; i++) if (!lv_.info.get((String("e") + i).c_str()).length()) rollEncounter(i);
        });
        gfx.setFont(m.small);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString("Mix the danger. Each omen hints at its encounter.", LX + 530, y + 28);
        const int ly = y + 68, lh = h - 68, rowH = 104;
        const float maxPos = std::max(0, 8 * rowH - 8 - lh);
        encPos_ = std::max(0.0f, std::min(encPos_, maxPos + 60));
        touch::scroller(LX, ly, LW, lh, &encPos_, maxPos);
        gfx.setClipRect(LX, ly, LW, lh);
        for (int i = 1; i <= 8; i++) {
            const int cy = ly + (i - 1) * rowH - (int)encPos_, ch = rowH - 8;
            if (cy + ch < ly || cy > ly + lh) continue;
            const String e = lv_.info.get((String("e") + i).c_str()), o = lv_.info.get((String("o") + i).c_str());
            const bool hit = i == encHit_;
            gfx.fillRoundRect(LX, cy, LW, ch, 10, hit ? C_PANEL : C_BG2);
            gfx.drawRoundRect(LX, cy, LW, ch, 10, hit ? C_GOLD : C_EDGE);
            gfx.setFont(m.title);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(ACCENT);
            gfx.drawString(String(i), LX + 40, cy + ch / 2);
            gfx.setTextDatum(top_left);
            gfx.setFont(m.body);
            gfx.setTextColor(e.length() ? C_TEXT : C_EDGE);
            gfx.drawString(fitText(e.length() ? e : String("-"), LW - 80 - 260), LX + 80, cy + 14);
            gfx.setFont(m.small);
            gfx.setTextColor(C_ICE);
            gfx.drawString(fitText(o.length() ? String("Omen: ") + o : String("No omen yet"), LW - 80 - 260), LX + 80, cy + 56);
            const int vy = std::max(cy, ly), vh = std::min(cy + ch, ly + lh) - vy;
            if (vh <= 0) continue;
            touch::zone(LX, vy, LW - 260, vh, [this, i] {
                const String k = String("e") + i;
                ask(String("Encounter ") + i, lv_.info.get(k.c_str()), 120, [this, k](const String& v) {
                    lv_.info.set(k.c_str(), v);
                    saveLevel();
                });
            });
            touch::onHold(LX, vy, LW - 260, vh, [this, i] {
                holdmenu::open(String("Encounter ") + i,
                               {{"Edit the omen", [this, i] {
                                     const String k = String("o") + i;
                                     ask("Omen", lv_.info.get(k.c_str()), 120, [this, k](const String& v) { lv_.info.set(k.c_str(), v); saveLevel(); });
                                 }},
                                {"Roll again", [this, i] { rollEncounter(i); }},
                                {"Clear", [this, i] {
                                     lv_.info.set((String("e") + i).c_str(), "");
                                     lv_.info.set((String("o") + i).c_str(), "");
                                     saveLevel();
                                 }, C_BLOOD}});
            });
            dieButton(LX + LW - 244, cy + (ch - 52) / 2, ly, ly + lh, [this, i] { rollEncounter(i); });
            const int fy = cy + (ch - 52) / 2;
            if (fy >= ly && fy + 52 <= ly + lh)
                button(LX + LW - 180, fy, 164, 52, "Fight!", false, C_BLOOD, [this, e] { fight(e); }, e.length() > 0);
        }
        gfx.clearClipRect();
        drawFooter("</> tab  hold for more  Esc back");
    }

    void drawChecklist(int y, int h) {
        const Metrics& m = metrics();
        std::vector<Check> items;
        int done = 0, total = 0;
        checklist(lv_, level_, &items, &done, &total);
        const int LX = 16, LW = 360, RX = LX + LW + 16, RW = W - RX - 16;
        // the score
        drawPanel(LX, y, LW, h, C_BG2, C_EDGE);
        const int pct = total ? done * 100 / total : 0, cx = LX + LW / 2, cy = y + h / 2 - 30, rr = 120;
        gfx.fillArc(cx, cy, rr, rr - 18, 0, 360, C_BG);
        if (pct) gfx.fillArc(cx, cy, rr, rr - 18, 270, 270 + pct * 360 / 100, pct == 100 ? C_EMERALD : ACCENT);
        gfx.setFont(m.big);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(pct == 100 ? C_EMERALD : C_TEXT);
        gfx.drawString(String(pct) + "%", cx, cy);
        gfx.setFont(m.body);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(done) + " of " + total + " done", cx, y + h - 70);
        // the items
        const int rowH = 76, lh = h;
        const float maxPos = std::max(0, (int)items.size() * rowH - 8 - lh);
        chkPos_ = std::max(0.0f, std::min(chkPos_, maxPos + 60));
        touch::scroller(RX, y, RW, lh, &chkPos_, maxPos);
        gfx.setClipRect(RX, y, RW, lh);
        for (int i = 0; i < (int)items.size(); i++) {
            const Check& c = items[i];
            const int iy = y + i * rowH - (int)chkPos_, ih = rowH - 8;
            if (iy + ih < y || iy > y + lh) continue;
            gfx.fillRoundRect(RX, iy, RW, ih, 10, C_BG2);
            gfx.drawRoundRect(RX, iy, RW, ih, 10, c.done ? C_EMERALD : C_EDGE);
            const int bx = RX + 34, by = iy + ih / 2;
            if (c.done) {
                gfx.fillCircle(bx, by, 18, C_EMERALD);
                gfx.drawWideLine(bx - 8, by + 1, bx - 2, by + 7, 3, C_BG);
                gfx.drawWideLine(bx - 2, by + 7, bx + 9, by - 6, 3, C_BG);
            } else {
                gfx.drawCircle(bx, by, 18, c.manual ? C_GOLD : C_EDGE);
                gfx.drawCircle(bx, by, 17, c.manual ? C_GOLD : C_EDGE);
            }
            gfx.setTextDatum(top_left);
            gfx.setFont(m.body);
            gfx.setTextColor(c.done ? C_DIM : C_TEXT);
            gfx.drawString(c.label, RX + 70, iy + 8);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(fitText(c.detail, RW - 90), RX + 70, iy + 40);
            const int vy = std::max(iy, y), vh = std::min(iy + ih, y + lh) - vy;
            if (vh <= 0) continue;
            if (c.manual) {
                const char* key = c.key;
                touch::zone(RX, vy, RW, vh, [this, key] {
                    String s = lv_.info.get("check");
                    if (s.indexOf(key) >= 0) { s.replace(key, ""); s.replace(",,", ","); }
                    else s += String(s.length() ? "," : "") + key;
                    lv_.info.set("check", s);
                    saveLevel();
                    sfx::click();
                });
            } else {
                // a tap goes where it can be fixed
                const int idx = i;
                touch::zone(RX, vy, RW, vh, [this, idx] {
                    if (idx <= 1) tab_ = T_LEVEL;
                    else if (idx == 3 || idx == 4) { tab_ = T_ROOMS; room_ = target(); edPos_ = 0; }
                    else if (idx == 5 || idx == 6) tab_ = T_ENC;
                    else tab_ = T_ROOMS;
                    sfx::click();
                });
            }
        }
        gfx.clearClipRect();
        drawFooter("</> tab  Esc back");
    }

    // ================================================================ running a level
    // The written rooms in a random order on a grid: the entrance on the left,
    // each room hung off one already placed (a winding tree), a few extra
    // connections for loops, the boss off the deepest room and the stairs
    // down behind the boss.
    static constexpr int GX = 9, GY = 5;
    enum Door : uint8_t { OPEN, DOOR, LOCKED, SECRET };
    struct Node { int key, gx, gy, depth; };
    struct Link { int a, b; Door door; };
    std::vector<Node> nodes_;
    std::vector<Link> links_;
    int downX_ = -1, downY_ = -1, runSel_ = -1;   // the stairs' cell; the room picked on the map
    uint32_t layoutSeed_ = 0;
    bool present_ = false;   // the players' full-screen map

    bool explored(int key) const {
        const String s = "," + lv_.info.get("explored") + ",";
        return s.indexOf("," + String(key) + ",") >= 0;
    }
    void setExplored(int key, bool on) {
        String s = lv_.info.get("explored");
        std::vector<String> keep;
        int start = 0;
        while (start < (int)s.length()) {
            int c = s.indexOf(',', start);
            String t = c < 0 ? s.substring(start) : s.substring(start, c);
            start = c < 0 ? s.length() : c + 1;
            if (t.length() && t.toInt() != key) keep.push_back(t);
        }
        if (on) keep.push_back(String(key));
        String out;
        for (auto& k : keep) out += (out.length() ? "," : "") + k;
        lv_.info.set("explored", out);
        saveLevel();
    }

    void buildLayout() {
        uint32_t seed = (uint32_t)lv_.info.num("seed", 0);
        if (!seed) { seed = esp_random() | 1; lv_.info.set("seed", String(seed)); saveLevel(); }
        layoutSeed_ = seed;
        nodes_.clear();
        links_.clear();
        downX_ = downY_ = -1;
        Lcg r{seed};
        std::vector<int> keys;
        for (int k = 1; k < target(); k++) if (written(room(k))) keys.push_back(k);
        for (int i = keys.size() - 1; i > 0; i--) std::swap(keys[i], keys[r.below(i + 1)]);
        bool used[GX][GY] = {};
        auto place = [&](int key, int gx, int gy, int parent, Door d) {
            used[gx][gy] = true;
            nodes_.push_back({key, gx, gy, parent < 0 ? 0 : nodes_[parent].depth + 1});
            if (parent >= 0) links_.push_back({parent, (int)nodes_.size() - 1, d});
        };
        static const int DX[] = {1, 0, 0, -1}, DY[] = {0, 1, -1, 0};
        auto freeAround = [&](int i, int& fx, int& fy) {
            int dirs[4] = {0, 1, 2, 3};
            for (int k = 3; k > 0; k--) std::swap(dirs[k], dirs[r.below(k + 1)]);
            for (int d : dirs) {
                const int x = nodes_[i].gx + DX[d], y = nodes_[i].gy + DY[d];
                if (x >= 0 && x < GX && y >= 0 && y < GY && !used[x][y]) { fx = x; fy = y; return true; }
            }
            return false;
        };
        auto doorFor = [&]() { const int d = r.below(100); return d < 55 ? OPEN : d < 80 ? DOOR : d < 90 ? LOCKED : SECRET; };
        if (keys.empty()) return;
        place(keys[0], 0, GY / 2, -1, OPEN);
        for (size_t i = 1; i < keys.size(); i++) {
            // grow from a recent room most of the time: long winding passages with side branches
            for (int tries = 0; tries < 60; tries++) {
                const int n = nodes_.size();
                const int p = r.below(10) < 6 ? std::max(0, n - 1 - r.below(3)) : r.below(n);
                int fx, fy;
                if (freeAround(p, fx, fy)) { place(keys[i], fx, fy, p, doorFor()); break; }
            }
        }
        // the boss: off the deepest room that has space, then the stairs behind it
        int deepest = -1, fx = 0, fy = 0, bx = 0, by = 0;
        for (int i = 0; i < (int)nodes_.size(); i++) {
            int x, y;
            if (freeAround(i, x, y) && (deepest < 0 || nodes_[i].depth > nodes_[deepest].depth)) { deepest = i; fx = x; fy = y; }
        }
        if (deepest >= 0) {
            place(target(), fx, fy, deepest, DOOR);
            const int b = nodes_.size() - 1;
            if (freeAround(b, bx, by)) { downX_ = bx; downY_ = by; used[bx][by] = true; }
        }
        // a few loops (never into the boss's room: one way in)
        const int bossIdx = deepest >= 0 ? (int)nodes_.size() - 1 : -1;
        for (int i = 0; i < (int)nodes_.size(); i++)
            for (int j = i + 1; j < (int)nodes_.size(); j++) {
                if (i == bossIdx || j == bossIdx) continue;
                if (abs(nodes_[i].gx - nodes_[j].gx) + abs(nodes_[i].gy - nodes_[j].gy) != 1) continue;
                bool linked = false;
                for (auto& l : links_) linked |= (l.a == i && l.b == j) || (l.a == j && l.b == i);
                if (!linked && r.below(100) < 18) links_.push_back({i, j, doorFor()});
            }
    }

    int nodeOf(int key) const {
        for (int i = 0; i < (int)nodes_.size(); i++) if (nodes_[i].key == key) return i;
        return -1;
    }
    // Players know a room once it's explored, or when an explored room has a
    // (not secret) way into it.
    bool seen(int i) const { return i == 0 || explored(nodes_[i].key); }   // the entrance is always seen
    bool known(int i) const {
        if (seen(i)) return true;
        for (auto& l : links_) {
            const int o = l.a == i ? l.b : l.b == i ? l.a : -1;
            if (o >= 0 && seen(o) && l.door != SECRET) return true;
        }
        return false;
    }

    // The map in (x, y, w, h). players: fog, no keys on unexplored rooms.
    void drawMap(int x, int y, int w, int h, bool players) {
        const Metrics& m = metrics();
        gfx.fillRoundRect(x, y, w, h, 10, rgb(18, 20, 30));
        const int cw = w / GX, ch = h / GY;
        const uint16_t PARCH = rgb(214, 196, 158), PARCH_DIM = rgb(118, 108, 90), INK = rgb(60, 44, 34);
        auto rectOf = [&](int i, int& rx, int& ry, int& rw, int& rh) {
            Lcg r{layoutSeed_ ^ (nodes_[i].key * 2654435761u)};
            rw = cw * (60 + r.below(26)) / 100;
            rh = ch * (58 + r.below(28)) / 100;
            if (nodes_[i].key == target()) { rw = cw * 88 / 100; rh = ch * 86 / 100; }
            rx = x + nodes_[i].gx * cw + (cw - rw) / 2;
            ry = y + nodes_[i].gy * ch + (ch - rh) / 2;
        };
        auto centre = [&](int gx, int gy, int& cx, int& cy) { cx = x + gx * cw + cw / 2; cy = y + gy * ch + ch / 2; };
        // corridors first, rooms over them
        for (auto& l : links_) {
            const bool see = !players || (known(l.a) && known(l.b) && (seen(l.a) || seen(l.b)));
            if (!see || (players && l.door == SECRET && !(seen(l.a) && seen(l.b)))) continue;
            int ax, ay, bx, by;
            centre(nodes_[l.a].gx, nodes_[l.a].gy, ax, ay);
            centre(nodes_[l.b].gx, nodes_[l.b].gy, bx, by);
            gfx.drawWideLine(ax, ay, bx, by, 9, players ? PARCH_DIM : rgb(90, 82, 70));
            const int mx = (ax + bx) / 2, my = (ay + by) / 2;
            const bool horiz = ay == by;
            if (l.door == OPEN) continue;
            const uint16_t dc = l.door == SECRET ? C_ARCANE : rgb(150, 94, 48);
            if (horiz) gfx.fillRect(mx - 4, my - 10, 8, 20, dc);
            else gfx.fillRect(mx - 10, my - 4, 20, 8, dc);
            if (l.door == LOCKED) gfx.fillCircle(mx, my, 3, C_GOLD);
            if (l.door == SECRET && !players) {
                gfx.setFont(m.small);
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(C_ARCANE);
                gfx.drawString("S", mx + (horiz ? 0 : 16), my + (horiz ? -18 : 0));
            }
        }
        // the stairs down, behind the boss
        const int b = nodeOf(target());
        if (downX_ >= 0 && b >= 0 && (!players || explored(target()))) {
            int ax, ay, bx2, by2;
            centre(nodes_[b].gx, nodes_[b].gy, ax, ay);
            centre(downX_, downY_, bx2, by2);
            gfx.drawWideLine(ax, ay, bx2, by2, 9, rgb(90, 82, 70));
            const int sx = x + downX_ * cw + cw / 2 - 30, sy = y + downY_ * ch + ch / 2 - 24;
            gfx.fillRect(sx, sy, 60, 48, rgb(30, 34, 52));
            for (int i = 0; i < 5; i++) gfx.fillRect(sx + 4 + i * 10, sy + 6 + i * 8, 52 - i * 10, 4, PARCH_DIM);
            gfx.setFont(m.small);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_GOLD);
            gfx.drawString("Down", sx + 30, sy + 52);
            if (!players || explored(target()))
                touch::zone(sx - 10, sy - 10, 80, 80, [this] {
                    if (!explored(target())) { toast("The boss still guards the stairs", C_BLOOD); return; }
                    if (level_ >= LEVELS) { toast("The bottom of the dungeon!", C_GOLD); return; }
                    const bool p = present_;
                    openLevel(level_ + 1, 1);
                    tab_ = T_RUN;
                    present_ = p;
                    toast((String("Down to level ") + level_).c_str(), C_GOLD);
                });
        }
        // rooms
        for (int i = 0; i < (int)nodes_.size(); i++) {
            const int key = nodes_[i].key;
            const bool ex = seen(i), kn = known(i);
            if (players && !kn) continue;
            int rx, ry, rw, rh;
            rectOf(i, rx, ry, rw, rh);
            const bool boss = key == target(), sel = !players && runSel_ == i;
            if (players && !ex) {   // seen through a door: an outline and a question
                gfx.fillRect(rx, ry, rw, rh, rgb(18, 20, 30));
                gfx.drawRect(rx, ry, rw, rh, PARCH_DIM);
                gfx.setFont(m.title);
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(PARCH_DIM);
                gfx.drawString("?", rx + rw / 2, ry + rh / 2);
            } else {
                gfx.fillRect(rx, ry, rw, rh, ex ? PARCH : PARCH_DIM);
                for (int gx = rx + 12; gx < rx + rw; gx += 12) gfx.drawFastVLine(gx, ry, rh, ex ? rgb(196, 178, 140) : rgb(106, 96, 80));
                for (int gy = ry + 12; gy < ry + rh; gy += 12) gfx.drawFastHLine(rx, gy, rw, ex ? rgb(196, 178, 140) : rgb(106, 96, 80));
                gfx.drawRect(rx, ry, rw, rh, INK);
                gfx.drawRect(rx + 1, ry + 1, rw - 2, rh - 2, INK);
                gfx.setFont(m.bold);
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(boss ? C_BLOOD_DK : INK);
                gfx.drawString(players ? String("") : String(key), rx + rw / 2, ry + rh / 2);
                if (boss)
                    for (int k = 0; k < 3; k++) gfx.drawCircle(rx + rw / 2, ry + rh / 2, std::min(rw, rh) / 3 + k, C_BLOOD);
                if (i == 0) {   // stairs up: the way in
                    for (int s = 0; s < 3; s++) gfx.fillRect(rx + 4 + s * 6, ry + rh - 8 - s * 6, 18 - s * 6, 4, INK);
                }
            }
            if (sel) {
                gfx.drawRect(rx - 3, ry - 3, rw + 6, rh + 6, C_GOLD);
                gfx.drawRect(rx - 4, ry - 4, rw + 8, rh + 8, C_GOLD);
            }
            touch::zone(rx, ry, rw, rh, [this, i, players, key] {
                if (players) { setExplored(key, true); sfx::select(); return; }
                runSel_ = i;
                sfx::click();
            });
        }
    }

    void drawRun(int y, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        const db::Rec& boss = room(target());
        int n = 0;
        for (int k = 1; k < target(); k++) n += written(room(k));
        if (!written(boss) || n < 2) {
            drawPanel(16, y, W - 32, h, C_BG2, C_EDGE);
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString(!written(boss) ? String("Write the boss room (") + target() + ") first: it guards the way down."
                                          : String("Write a few more rooms first."), W / 2, y + h / 2 - 40);
            button(W / 2 - 200, y + h / 2, 400, 64, "Stock the level", true, C_GOLD, [this] { stockLevel(); });
            drawFooter("</> tab  Esc back");
            return;
        }
        if ((uint32_t)lv_.info.num("seed", 0) != layoutSeed_ || nodes_.empty()) buildLayout();
        const int MW = 846, MX = 16;
        drawMap(MX, y, MW, h, false);
        // the side panel
        const int RX = MX + MW + 14, RW = W - RX - 16;
        int bh = 54, by = y + h - bh;
        button(RX, by, RW, bh, "Show the players", true, C_GOLD, [this] { present_ = true; sfx::select(); });
        by -= bh + 8;
        const int hw = (RW - 8) / 2;
        button(RX, by, hw, bh, "New layout", false, ACCENT, [this] {
            holdmenu::open("A new layout?", {{"Shuffle the rooms (forgets what's explored)", [this] {
                lv_.info.set("seed", String(esp_random() | 1));
                lv_.info.set("explored", "");
                runSel_ = -1;
                saveLevel();
            }, 0, true}});
        });
        button(RX + hw + 8, by, hw, bh, "Reset fog", false, C_ICE, [this] {
            lv_.info.set("explored", "");
            saveLevel();
            sfx::back();
        });
        const int py = y, ph = by - 10 - py;
        drawPanel(RX, py, RW, ph, C_BG2, C_EDGE);
        if (runSel_ < 0 || runSel_ >= (int)nodes_.size()) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_DIM);
            int ty = py + 16;
            for (const char* t : {"Tap a room to read it.", "Small stairs mark the way in. The ringed room is the boss; the stairs down are behind it.",
                                  "Doors: brown, a gold dot is locked, a violet S is secret.",
                                  "Show the players: the fogged map. Tap rooms as the party enters them."})
                ty = textWrapped(t, RX + 14, ty, RW - 28, 28) + 14;
            drawFooter("</> tab  Esc back");
            return;
        }
        const int key = nodes_[runSel_].key;
        const db::Rec& r = room(key);
        gfx.setTextDatum(top_left);
        gfx.setFont(m.small);
        gfx.setTextColor(key == target() ? C_BLOOD : ACCENT);
        gfx.drawString(String("KEY ") + key + "  " + d23::KIND_NAMES[key == target() ? 4 : kindOf(r)] + "  " + r.get("light"),
                       RX + 14, py + 10);
        gfx.setFont(m.bold);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(fitText(r.get("name"), RW - 28), RX + 14, py + 36);
        gfx.setFont(m.small);
        int ty = py + 76;
        auto para = [&](const String& label, const String& v, uint16_t c) {
            const int lines = std::min(3, (py + ph - 140 - ty - 26) / 26);
            if (!v.length() || lines < 1) return;
            gfx.setTextColor(c);
            gfx.drawString(label, RX + 14, ty);
            ty += 26;
            gfx.setTextColor(C_TEXT);
            ty = textWrapped(v.c_str(), RX + 14, ty, RW - 28, 26, lines) + 6;
        };
        para("Inhabitants", r.get("monsters"), C_BLOOD);
        para("Features", r.get("f1") + (r.get("f2").length() ? "  " + r.get("f2") : String("")), C_ICE);
        para("Treasure", r.get("t1") + (r.get("t2").length() ? "; " + r.get("t2") : String("")), C_GOLD);
        para("Description", r.get("desc"), C_DIM);
        const bool ex = explored(key);
        const int ay = py + ph - 128;
        button(RX + 10, ay, RW - 20, 54, ex ? "Explored" : "Mark explored", ex, C_EMERALD, [this, key, ex] { setExplored(key, !ex); sfx::click(); });
        button(RX + 10, ay + 62, (RW - 28) / 2, 54, "Fight!", false, C_BLOOD, [this, key] { fight(room(key).get("monsters")); },
               r.get("monsters").length() > 0);
        button(RX + 18 + (RW - 28) / 2, ay + 62, (RW - 28) / 2, 54, "Edit", false, ACCENT, [this, key] {
            room_ = key;
            tab_ = T_ROOMS;
            edPos_ = 0;
        });
        drawFooter("</> tab  Esc back");
    }

    // The players' view: the fogged map, the whole screen.
    void drawPresent() {
        const Metrics& m = metrics();
        if ((uint32_t)lv_.info.num("seed", 0) != layoutSeed_ || nodes_.empty()) buildLayout();
        const String name = lv_.info.get("name");
        drawHeader((String("Level ") + level_ + (name.length() ? ": " + name : String(""))).c_str(), ACCENT);
        drawMap(16, HEADER_H + 8, W - 32, H - HEADER_H - FOOTER_H - 16, true);
        gfx.setFont(m.small);
        (void)m;
        drawFooter("Esc DM view   Tap rooms as the party enters them");
    }

    // ================================================================ web page
    // The page works on any dungeon and level: the device's own (what it has
    // open) is set aside for the call and put back afterwards - reloaded if
    // the page changed the same files.
    struct Ctx {
        String id;
        db::Rec dungeon;
        int rooms[LEVELS] = {0}, done[LEVELS] = {0};
        String names[LEVELS];
        int level = 1, room = 1;
        Level lv;
        std::vector<Node> nodes;
        std::vector<Link> links;
        int downX = -1, downY = -1;
        uint32_t seed = 0;
    };
    void swapCtx(Ctx& c) {
        std::swap(id_, c.id);
        std::swap(dungeon_, c.dungeon);
        for (int i = 0; i < LEVELS; i++) {
            std::swap(levelRooms_[i], c.rooms[i]);
            std::swap(levelDone_[i], c.done[i]);
            std::swap(levelName_[i], c.names[i]);
        }
        std::swap(level_, c.level);
        std::swap(room_, c.room);
        std::swap(lv_, c.lv);
        std::swap(nodes_, c.nodes);
        std::swap(links_, c.links);
        std::swap(downX_, c.downX);
        std::swap(downY_, c.downY);
        std::swap(layoutSeed_, c.seed);
    }

    static String recJson(const db::Rec& r) {
        String o = "{";
        for (size_t i = 0; i < r.kv.size(); i++) o += String(i ? "," : "") + web::js(r.kv[i].first) + ":" + web::js(r.kv[i].second);
        return o + "}";
    }

    String dungeonJson() {
        int tl = 0, tk = 0;
        const bool dated = today(tl, tk);
        String o = String("{\"id\":") + web::js(id_) + ",\"info\":" + recJson(dungeon_) + ",\"party\":" + partySize() +
                   ",\"today\":" + (dated ? String("{\"level\":") + tl + ",\"room\":" + std::min(tk, MAX_ROOMS) + "}" : String("null")) +
                   ",\"levels\":[";
        for (int l = 1; l <= LEVELS; l++)
            o += String(l > 1 ? "," : "") + "{\"name\":" + web::js(levelName_[l - 1]) + ",\"rooms\":" + levelRooms_[l - 1] +
                 ",\"target\":" + defaultRooms(l) + ",\"pct\":" + levelDone_[l - 1] + ",\"month\":" + web::js(monthOf(l)) +
                 ",\"partyLevel\":" + partyLevel(l) + "}";
        return o + "]}";
    }

    String levelJson() {
        String o = String("{\"level\":") + level_ + ",\"info\":" + recJson(lv_.info) + ",\"target\":" + target() +
                   ",\"partyLevel\":" + partyLevel(level_) + ",\"party\":" + partySize() + ",\"rooms\":[";
        for (int k = 1; k <= target(); k++) o += String(k > 1 ? "," : "") + recJson(room(k));
        o += "],\"checklist\":[";
        std::vector<Check> items;
        int done = 0, total = 0;
        checklist(lv_, level_, &items, &done, &total);
        for (size_t i = 0; i < items.size(); i++)
            o += String(i ? "," : "") + "{\"label\":" + web::js(items[i].label) + ",\"detail\":" + web::js(items[i].detail) +
                 ",\"done\":" + (items[i].done ? "true" : "false") + ",\"key\":" + web::js(items[i].key ? items[i].key : "") + "}";
        o += String("],\"pct\":") + (total ? done * 100 / total : 0);
        // the run map (only once there are rooms to place)
        int n = 0;
        for (int k = 1; k < target(); k++) n += written(room(k));
        o += ",\"map\":";
        if (written(room(target())) && n >= 2) {
            if ((uint32_t)lv_.info.num("seed", 0) != layoutSeed_ || nodes_.empty()) buildLayout();
            o += String("{\"w\":") + GX + ",\"h\":" + GY + ",\"down\":[" + downX_ + "," + downY_ + "],\"nodes\":[";
            for (size_t i = 0; i < nodes_.size(); i++)
                o += String(i ? "," : "") + "{\"key\":" + nodes_[i].key + ",\"x\":" + nodes_[i].gx + ",\"y\":" + nodes_[i].gy + "}";
            o += "],\"links\":[";
            for (size_t i = 0; i < links_.size(); i++)
                o += String(i ? "," : "") + "[" + links_[i].a + "," + links_[i].b + "," + (int)links_[i].door + "]";
            o += "]}";
        } else o += "null";
        return o + "}";
    }

    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (!storage::ok()) { out = "No SD card"; return 503; }
        if (c.op == "list") {
            loadList();
            out = "[";
            for (size_t i = 0; i < list_.size(); i++)
                out += String(i ? "," : "") + "{\"id\":" + web::js(list_[i].id) + ",\"name\":" + web::js(list_[i].name) +
                       ",\"theme\":" + web::js(list_[i].theme) + ",\"rooms\":" + list_[i].rooms + "}";
            out += "]";
            return 200;
        }
        const String id = c.op == "new" ? String("") : c.arg("id");
        if (c.op != "new" && (!id.length() || id.indexOf('/') >= 0 || id.indexOf("..") >= 0)) { out = "No such dungeon"; return 404; }
        if (c.op == "delete") {
            if (id == id_ && screen_ != LIST) { out = "It's open on the device"; return 409; }
            storage::removeDir((String(ROOT) + "/" + id).c_str());
            out = "{}";
            return 200;
        }
        const int l = std::max(1, std::min(LEVELS, c.num("l", 1)));
        const bool deviceSame = id_ == id && screen_ != LIST;
        Ctx keep;
        swapCtx(keep);
        int code = 200;
        if (c.op == "new") {
            String name = c.arg("name");
            name.trim();
            if (!name.length()) name = String("The ") + rng::pick(d23::DUNGEON_ADJ) + " " + rng::pick(d23::DUNGEON_NOUN);
            String nid = slugOf(name);
            for (int i = 2; storage::exists((String(ROOT) + "/" + nid).c_str()); i++) nid = slugOf(name) + "_" + i;
            storage::mkdir(ROOT);
            storage::mkdir((String(ROOT) + "/" + nid).c_str());
            id_ = nid;
            dungeon_.set("name", name);
            dungeon_.set("party", "4");
            dungeon_.set("base", "1");
            if (wallclock::valid()) dungeon_.set("start", wallclock::isoDate());
            saveDungeon();
            loadDungeon(nid);
            out = dungeonJson();
        } else if (!loadDungeon(id)) { out = "No such dungeon"; code = 404; }
        else if (c.op == "dungeon" || c.op == "setd" || c.op == "rolld") {
            const String k = c.arg("key");
            if (c.op == "setd") {
                String v = c.arg("value");
                v.trim();
                if (k == "party") v = String(std::max(1, std::min(10, (int)v.toInt())));
                if (k == "base") v = String(std::max(1, std::min(20, (int)v.toInt())));
                dungeon_.set(k.c_str(), v);
                saveDungeon();
            } else if (c.op == "rolld") rollIntro(k.c_str());
            out = dungeonJson();
        } else {
            loadLevel(l);
            const int key = c.num("room", 0);
            const bool roomOk = key >= 1 && key <= target();
            const String k = c.arg("key");
            if (c.op == "set") {
                String v = c.arg("value");
                v.trim();
                if (!roomOk) {
                    if (k == "rooms") { lv_.info.set("rooms", String(std::max(3, std::min(MAX_ROOMS, (int)v.toInt())))); }
                    else lv_.info.set(k.c_str(), v);
                    saveLevel();
                } else {
                    room(key).set(k.c_str(), v);
                    changedRoom(key);
                }
            } else if (c.op == "roll") {
                if (roomOk) rollField(key, k);
                else if (k.startsWith("e") && k.length() <= 2) rollEncounter(std::max(1, std::min(8, (int)k.substring(1).toInt())));
                else rollLevel(k.c_str());
            } else if (c.op == "gen" && roomOk) genRoom(key, c.arg("all") != "1");
            else if (c.op == "clear" && roomOk) { lv_.rooms[key - 1] = db::Rec(); saveLevel(); }
            else if (c.op == "stock") stockLevel();
            else if (c.op == "check" && k.length()) {
                String s2 = lv_.info.get("check");
                if (s2.indexOf(k) >= 0) { s2.replace(k, ""); s2.replace(",,", ","); }
                else s2 += String(s2.length() ? "," : "") + k;
                lv_.info.set("check", s2);
                saveLevel();
            } else if (c.op == "explore" && roomOk) setExplored(key, c.arg("on") != "0");
            else if (c.op == "fog") { lv_.info.set("explored", ""); saveLevel(); }
            else if (c.op == "layout") {
                lv_.info.set("seed", String(esp_random() | 1));
                lv_.info.set("explored", "");
                saveLevel();
            } else if (c.op != "level") { out = "Unknown"; code = 404; }
            if (code == 200) out = levelJson();
        }
        swapCtx(keep);
        if (deviceSame && c.op != "dungeon" && c.op != "level") {   // the page changed what the device shows
            const int dl = level_;
            loadDungeon(id_);
            if (screen_ == LEVEL) loadLevel(dl);
        }
        if (!appOpen(this)) { gen_.close(); releaseRandomTables(); }   // as onExit: don't hold them for the page
        return code;
    }
};

}  // namespace

App* makeDungeon23App() { return new Dungeon23App(); }
#endif

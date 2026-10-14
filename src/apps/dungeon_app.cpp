#include "../core/app.h"
#include "../core/rng.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "dungeon_tables.h"
#include <math.h>
#include <vector>

using namespace ui;

namespace {

const char* const ROOM_TITLES[5] = {"Entrance & Guardian", "Puzzle / Roleplay", "Trick / Setback",
                                    "Climax", "Reward & Revelation"};
const uint16_t ROOM_COLORS[5] = {C_BLOOD, C_ARCANE, C_ICE, C_GOLD, C_EMERALD};

enum class Layout : uint8_t { Line, Zigzag, Branch, Loop };

class DungeonApp : public App {
public:
    const char* name() const override { return "5-Room Dungeon"; }
    const char* blurb() const override { return "Instant adventure sites."; }
    uint16_t accent() const override { return C_EMERALD; }

    void drawIcon(int cx, int cy, float t) override {
        // tiny map: five rooms joined by corridors
        const int px[5] = {-16, -4, 8, 8, 18}, py[5] = {10, 10, -2, -14, -2};
        for (int i = 0; i < 4; i++) gfx.drawLine(cx + px[i], cy + py[i], cx + px[i + 1], cy + py[i + 1], C_EDGE);
        for (int i = 0; i < 5; i++) {
            bool lit = ((int)(t * 2) % 5) == i;
            gfx.fillRect(cx + px[i] - 4, cy + py[i] - 4, 8, 8, lit ? ROOM_COLORS[i] : C_PANEL);
            gfx.drawRect(cx + px[i] - 4, cy + py[i] - 4, 8, 8, ROOM_COLORS[i]);
        }
    }

    void onEnter() override {
        input::setTextMode(false);
        if (!generated_) {
            loadTables();
            generate();
        }
    }

    bool handle(const input::Event& e) override {
        if (browsing_) return handleBrowser(e);
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_LEFT: if (page_ > 0) { page_--; scroll_ = 0; sfx::click(); } return true;
            case input::K_RIGHT: if (page_ < 5) { page_++; scroll_ = 0; sfx::click(); } return true;
            case input::K_UP: if (scroll_ > 0) scroll_--; return true;
            case input::K_DOWN: if (scroll_ < maxScroll_) scroll_++; return true;
            case input::K_ENTER: generate(); sfx::diceRattle(); return true;
            case input::K_CHAR: break;
            default: return true;
        }
        switch (e.base) {
            case 'g': generate(); sfx::diceRattle(); break;
            case 'r': rerollPage(); sfx::diceRattle(); break;
            case 's': saveToSd(); break;
            case 'l': openBrowser(); break;
            default:
                if (e.base >= '0' && e.base <= '5') { page_ = e.base - '0'; scroll_ = 0; sfx::click(); }
        }
        return true;
    }

    void tick(uint32_t dt) override {
        anim_ += dt;
        token_ = approach(token_, (float)std::max(0, page_ - 1), std::min(1.0f, dt * 0.012f));
        if (reveal_ < 1) reveal_ = std::min(1.0f, reveal_ + dt * 0.004f);
    }

    void draw() override {
        drawBackground();
        drawHeader(name(), C_EMERALD);
        drawMap();
        drawPage();
        if (!dirty_) {
            gfx.setFont(&fonts::Font0);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_EMERALD);
            gfx.drawString("saved", W - 30, HEADER_H / 2);
        }
        drawFooter("</> rooms Ent new R reroll S save L load");
        if (browsing_) drawBrowser();
    }

private:
    std::vector<String> tables_[dtab::TABLE_COUNT];
    bool generated_ = false;
    String title_;
    String r_[dtab::TABLE_COUNT];  // current pick per table
    Layout layout_ = Layout::Line;
    int nodeX_[5], nodeY_[5];
    int page_ = 0, scroll_ = 0, maxScroll_ = 0;
    uint32_t anim_ = 0;
    float token_ = 0, reveal_ = 0;
    bool dirty_ = true, browsing_ = false, confirmDelete_ = false;
    String savedPath_;
    std::vector<String> files_;
    int browseSel_ = 0;

    void loadTables() {
        const dtab::Table* builtin = dtab::FANTASY;
        const char* dir = "/gmtools/dungeon/";
        for (int i = 0; i < dtab::TABLE_COUNT; i++) {
            tables_[i].clear();
            String path = String(dir) + builtin[i].key + ".txt";
            storage::readLines(path.c_str(), tables_[i]);
            if (tables_[i].empty())
                for (int k = 0; k < builtin[i].count; k++) tables_[i].push_back(builtin[i].items[k]);
        }
    }

    String pick(int table, const String& avoid = "") {
        auto& t = tables_[table];
        if (t.empty()) return "";
        // avoid repeating the current entry on reroll
        for (int tries = 0; tries < 4; tries++) {
            const String& s = t[rng::below(t.size())];
            if (s != avoid || t.size() == 1) return s;
        }
        return t[0];
    }

    void generate() {
        for (int i = 0; i < dtab::TABLE_COUNT; i++) r_[i] = pick(i, r_[i]);
        title_ = String("The ") + r_[dtab::ADJECTIVE] + " " + r_[dtab::PLACE];
        layout_ = (Layout)rng::below(4);
        buildLayout();
        generated_ = true;
        dirty_ = true;
        savedPath_ = "";
        page_ = 0;
        scroll_ = 0;
        token_ = 0;
        reveal_ = 0;
    }

    void rerollPage() {
        using namespace dtab;
        auto re = [&](int k) { r_[k] = pick(k, r_[k]); };
        switch (page_) {
            case 0: re(ADJECTIVE); re(PLACE); re(HOOK); savedPath_ = "";
                    title_ = String("The ") + r_[ADJECTIVE] + " " + r_[PLACE]; break;
            case 1: re(ENTRANCE); re(GUARDIAN); break;
            case 2: re(PUZZLE); break;
            case 3: re(TRICK); break;
            case 4: re(BOSS); re(TERRAIN); break;
            case 5: re(REWARD); re(TWIST); break;
        }
        reveal_ = 0;
        dirty_ = true;
    }

    // Map strip occupies y 18..46. Lay out 5 nodes per the chosen shape.
    void buildLayout() {
        const int y0 = 26, y1 = 38;
        const int xs[5] = {30, 75, 120, 165, 210};
        for (int i = 0; i < 5; i++) { nodeX_[i] = xs[i]; nodeY_[i] = (y0 + y1) / 2; }
        switch (layout_) {
            case Layout::Zigzag: for (int i = 0; i < 5; i++) nodeY_[i] = (i % 2) ? y0 : y1; break;
            case Layout::Branch:  // rooms 2 and 3 sit side by side as alternate paths
                nodeX_[1] = nodeX_[2] = 90; nodeY_[1] = y0 - 2; nodeY_[2] = y1 + 2;
                nodeX_[3] = 150; nodeX_[4] = 205; nodeX_[0] = 35; break;
            case Layout::Loop:
                nodeY_[1] = y0; nodeY_[2] = y0; nodeY_[3] = y1; nodeX_[3] = 120; nodeX_[2] = 165;
                nodeX_[4] = 210; nodeY_[4] = y1; break;
            default: break;
        }
    }

    void corridor(int a, int b, uint16_t c) {
        // L-shaped corridors look more dungeon-like than diagonals
        int mx = nodeX_[b];
        gfx.drawFastHLine(std::min(nodeX_[a], mx), nodeY_[a], abs(mx - nodeX_[a]), c);
        gfx.drawFastVLine(mx, std::min(nodeY_[a], nodeY_[b]), abs(nodeY_[b] - nodeY_[a]), c);
    }

    void drawMap() {
        gfx.fillRect(0, HEADER_H, W, 32, rgb(10, 9, 15));
        gfx.drawFastHLine(0, HEADER_H + 31, W, C_EDGE);
        // corridors
        if (layout_ == Layout::Branch) { corridor(0, 1, C_EDGE); corridor(0, 2, C_EDGE); corridor(1, 3, C_EDGE); corridor(2, 3, C_EDGE); corridor(3, 4, C_EDGE); }
        else if (layout_ == Layout::Loop) { corridor(0, 1, C_EDGE); corridor(1, 2, C_EDGE); corridor(2, 4, C_EDGE); corridor(1, 3, C_EDGE); corridor(3, 4, C_EDGE); }
        else for (int i = 0; i < 4; i++) corridor(i, i + 1, C_EDGE);
        // entrance stub
        gfx.drawFastHLine(2, nodeY_[0], nodeX_[0] - 2, C_EDGE);

        for (int i = 0; i < 5; i++) {
            bool cur = page_ == i + 1;
            int s = cur ? 7 : 5;
            uint16_t col = ROOM_COLORS[i];
            gfx.fillRect(nodeX_[i] - s, nodeY_[i] - s, s * 2, s * 2, cur ? col : C_BG2);
            gfx.drawRect(nodeX_[i] - s, nodeY_[i] - s, s * 2, s * 2, col);
            gfx.setFont(&fonts::Font0);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(cur ? C_BG : col);
            gfx.drawString(String(i + 1), nodeX_[i] + 1, nodeY_[i] + 1);
        }
        // party token glides between rooms
        if (page_ > 0) {
            int a = (int)floorf(token_), b = std::min(4, a + 1);
            float f = token_ - a;
            int tx = nodeX_[a] + (int)((nodeX_[b] - nodeX_[a]) * f);
            int ty = nodeY_[a] + (int)((nodeY_[b] - nodeY_[a]) * f) - 11;
            ty += (int)(sinf(anim_ * 0.006f) * 1.5f);
            gfx.fillTriangle(tx - 3, ty - 3, tx + 3, ty - 3, tx, ty + 2, C_TEXT);
        }
    }

    void drawPage() {
        const int top = HEADER_H + 35, bottom = H - FOOTER_H - 2;
        String heading, body;
        uint16_t col = C_GOLD;
        using namespace dtab;
        switch (page_) {
            case 0:
                heading = title_;
                body = String("Hook: ") + r_[HOOK] + "\n\nUse < > to walk the rooms. R rerolls this page; Enter makes a new dungeon.";
                break;
            case 1: body = String("Way in: ") + r_[ENTRANCE] + "\n\nGuardian: " + r_[GUARDIAN]; break;
            case 2: body = r_[PUZZLE]; break;
            case 3: body = r_[TRICK]; break;
            case 4: body = r_[BOSS] + " The fight takes place " + r_[TERRAIN] + "."; break;
            case 5: body = String("Reward: ") + r_[REWARD] + "\n\nTwist: " + r_[TWIST]; break;
        }
        if (page_ > 0) {
            heading = String(page_) + ". " + ROOM_TITLES[page_ - 1];
            col = ROOM_COLORS[page_ - 1];
        }
        gfx.setTextDatum(top_left);
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        if (gfx.textWidth(heading) > W - 12) gfx.setFont(&fonts::Font2);
        gfx.setTextColor(col);
        gfx.drawString(heading, 6, top);

        // typewriter reveal on new content
        int shown = (int)(body.length() * easeOut(reveal_));
        String vis = body.substring(0, shown);

        gfx.setFont(&fonts::Font0);
        gfx.setTextColor(C_TEXT);
        const int lineH = 10, y0 = top + 18;
        int maxLines = (bottom - y0) / lineH;
        // measure total lines for scrolling
        gfx.setClipRect(0, y0, W, bottom - y0);
        int total = (textWrappedDry(body, W - 14, lineH));
        maxScroll_ = std::max(0, total - maxLines);
        if (scroll_ > maxScroll_) scroll_ = maxScroll_;
        textWrapped(vis.c_str(), 7, y0 - scroll_ * lineH, W - 14, lineH);
        gfx.clearClipRect();
        if (maxScroll_ > 0) {
            int sh = std::max(6, (bottom - y0) * maxLines / total);
            int sy = y0 + (bottom - y0 - sh) * scroll_ / maxScroll_;
            gfx.fillRect(W - 3, sy, 2, sh, C_GOLD_DK);
        }
    }

    // Count wrapped lines without drawing (draws far off-screen).
    int textWrappedDry(const String& s, int w, int lineH) {
        int end = textWrapped(s.c_str(), 0, -10000, w, lineH);
        return (end + 10000) / lineH;
    }

    // ---- save / load ----
    // Files are plain text that read well in the Reference viewer and parse back
    // via "Key: value" lines.
    static const char* layoutName(Layout l) {
        switch (l) { case Layout::Zigzag: return "zigzag"; case Layout::Branch: return "branch";
                     case Layout::Loop: return "loop"; default: return "line"; }
    }

    String serialize() {
        using namespace dtab;
        String t = title_ + "\n";
        t += String("Map: ") + layoutName(layout_) + "\n\n";
        t += "Hook: " + r_[HOOK] + "\n\n";
        t += "1. ENTRANCE & GUARDIAN\nWay in: " + r_[ENTRANCE] + "\nGuardian: " + r_[GUARDIAN] + "\n\n";
        t += "2. PUZZLE / ROLEPLAY\nChallenge: " + r_[PUZZLE] + "\n\n";
        t += "3. TRICK / SETBACK\nSetback: " + r_[TRICK] + "\n\n";
        t += "4. CLIMAX\nBoss: " + r_[BOSS] + "\nBattlefield: " + r_[TERRAIN] + "\n\n";
        t += "5. REWARD & REVELATION\nReward: " + r_[REWARD] + "\nTwist: " + r_[TWIST] + "\n";
        return t;
    }

    bool deserialize(const String& text) {
        using namespace dtab;
        struct { const char* key; int slot; } keys[] = {{"Hook: ", HOOK}, {"Way in: ", ENTRANCE},
            {"Guardian: ", GUARDIAN}, {"Challenge: ", PUZZLE}, {"Setback: ", TRICK}, {"Boss: ", BOSS},
            {"Battlefield: ", TERRAIN}, {"Reward: ", REWARD}, {"Twist: ", TWIST}};
        String title;
        int found = 0, start = 0;
        while (start < (int)text.length()) {
            int nl = text.indexOf('\n', start);
            String l = nl < 0 ? text.substring(start) : text.substring(start, nl);
            l.replace("\r", "");
            start = nl < 0 ? text.length() : nl + 1;
            if (!title.length() && l.length()) { title = l; continue; }
            if (l.startsWith("Map: ")) {
                String m = l.substring(5);
                layout_ = m == "zigzag" ? Layout::Zigzag : m == "branch" ? Layout::Branch :
                          m == "loop" ? Layout::Loop : Layout::Line;
            }
            for (auto& k : keys) if (l.startsWith(k.key)) { r_[k.slot] = l.substring(strlen(k.key)); found++; }
        }
        if (found < 5) return false;
        title_ = title;
        buildLayout();
        return true;
    }

    static String fileNameFor(const String& title) {
        String n;
        for (size_t i = 0; i < title.length() && n.length() < 40; i++) {
            char c = title[i];
            n += isalnum((unsigned char)c) ? c : '_';
        }
        while (n.indexOf("__") >= 0) n.replace("__", "_");
        return n.length() ? n : String("dungeon");
    }

    void saveToSd() {
        if (!storage::ok()) { sfx::error(); toast("No SD card - see Settings", C_BLOOD); return; }
        String path = savedPath_;
        if (!path.length()) {
            String base = String("/gmtools/dungeons/") + fileNameFor(title_);
            path = base + ".txt";
            for (int i = 2; storage::exists(path.c_str()) && i < 100; i++) path = base + "_" + i + ".txt";
        }
        if (storage::writeText(path.c_str(), serialize())) {
            savedPath_ = path;
            dirty_ = false;
            sfx::select();
            toast((String("Saved ") + path.substring(path.lastIndexOf('/') + 1)).c_str(), C_EMERALD);
        } else {
            sfx::error();
            toast("Save failed", C_BLOOD);
        }
    }

    void openBrowser() {
        if (!storage::ok()) { sfx::error(); toast("No SD card - see Settings", C_BLOOD); return; }
        files_.clear();
        storage::listFiles("/gmtools/dungeons", files_);
        if (files_.empty()) { sfx::error(); toast("No saved dungeons yet", C_DIM); return; }
        browsing_ = true;
        browseSel_ = 0;
        confirmDelete_ = false;
        sfx::select();
    }

    bool handleBrowser(const input::Event& e) {
        int n = files_.size();
        if (!(e.key == input::K_CHAR && e.base == 'x')) confirmDelete_ = false;
        switch (e.key) {
            case input::K_BACK: browsing_ = false; sfx::back(); return true;
            case input::K_UP: browseSel_ = (browseSel_ + n - 1) % n; sfx::click(); return true;
            case input::K_DOWN: browseSel_ = (browseSel_ + 1) % n; sfx::click(); return true;
            case input::K_ENTER: {
                String path = String("/gmtools/dungeons/") + files_[browseSel_];
                String text;
                if (storage::readText(path.c_str(), text) && deserialize(text)) {
                    savedPath_ = path;
                    dirty_ = false;
                    browsing_ = false;
                    page_ = 0; scroll_ = 0; token_ = 0; reveal_ = 0;
                    sfx::select();
                } else {
                    sfx::error();
                    toast("Not a dungeon file", C_BLOOD);
                }
                return true;
            }
            case input::K_CHAR:
                if (e.base == 'x') {
                    if (!confirmDelete_) { confirmDelete_ = true; toast("X again to delete", C_BLOOD); return true; }
                    String path = String("/gmtools/dungeons/") + files_[browseSel_];
                    storage::remove(path.c_str());
                    if (path == savedPath_) { savedPath_ = ""; dirty_ = true; }
                    files_.erase(files_.begin() + browseSel_);
                    confirmDelete_ = false;
                    sfx::back();
                    if (files_.empty()) browsing_ = false;
                    else browseSel_ = std::min(browseSel_, (int)files_.size() - 1);
                }
                return true;
            default: return true;
        }
    }

    void drawBrowser() {
        const int x = 14, y = 20, w = W - 28, h = 98;
        gfx.fillRoundRect(x + 3, y + 3, w, h, 5, rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_EMERALD);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_EMERALD);
        gfx.drawString("Saved dungeons", x + 6, y + 4);
        gfx.setTextColor(C_DIM);
        gfx.setTextDatum(top_right);
        gfx.drawString("Ent load  X del", x + w - 6, y + 4);
        gfx.setTextDatum(top_left);
        int n = files_.size(), rows = 7;
        int first = std::max(0, std::min(browseSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int ry = y + 17 + (i - first) * 11;
            bool s = i == browseSel_;
            if (s) gfx.fillRect(x + 4, ry - 1, w - 8, 10, C_PANEL);
            String nm = files_[i].substring(0, files_[i].length() - 4);
            nm.replace('_', ' ');
            gfx.setTextColor(s ? C_TEXT : rgb(180, 190, 180));
            gfx.drawString(nm, x + 10, ry);
        }
    }
};

}  // namespace

App* makeDungeonApp() { return new DungeonApp(); }

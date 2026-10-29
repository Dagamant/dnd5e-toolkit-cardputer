#include "../core/app.h"
#include "../core/hold_menu.h"
#include "../core/touch.h"
#include "../core/rng.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/web.h"
#include "../core/widgets.h"
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
            gfx.fillRect(cx + px[i] - u(4), cy + py[i] - u(4), u(8), u(8), lit ? ROOM_COLORS[i] : C_PANEL);
            gfx.drawRect(cx + px[i] - u(4), cy + py[i] - u(4), u(8), u(8), ROOM_COLORS[i]);
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
        token_ = approach(token_, (float)(wide() ? page_ - 1 : std::max(0, page_ - 1)), std::min(1.0f, dt * 0.012f));
        if (reveal_ < 1) reveal_ = std::min(1.0f, reveal_ + dt * 0.004f);
    }

    bool bigLayout() const override { return true; }

    void draw() override {
        drawBackground();
        drawHeader(name(), C_EMERALD);
        if (wide()) { drawWide(); return; }
        drawMap();
        drawPage();
        if (!dirty_) {
            gfx.setFont(ui::font(&fonts::Font0));
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_EMERALD);
            gfx.drawString("saved", W - u(30), HEADER_H / 2);
        }
        drawFooter("</> rooms  Ent new  R reroll  S save  L load");
        if (browsing_) drawBrowser();
    }

private:
    std::vector<String> tables_[dtab::TABLE_COUNT];
    bool generated_ = false;
    String title_;
    String r_[dtab::TABLE_COUNT];  // current pick per table
    Layout layout_ = Layout::Line;
    int nodeX_[5], nodeY_[5];
    bool fog_ = false;   // big screen: show only the rooms reached so far
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
        seed_ = esp_random() | 1;
        buildMap();
        generated_ = true;
        dirty_ = true;
        savedPath_ = "";
        page_ = 0;
        scroll_ = 0;
        token_ = 0;
        reveal_ = 0;
    }

    // The web page's dungeon: the same five rooms, rerolls and saved files.
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (tables_[0].empty()) loadTables();
        if (!generated_) generate();
        if (c.op == "generate") generate();
        else if (c.op == "reroll") {
            const int p = c.num("page", -1);
            if (p < 0 || p > 5) { out = "No such room"; return 400; }
            const int keep = page_;
            page_ = p;
            rerollPage();
            page_ = keep;
        } else if (c.op == "save") saveToSd();
        else if (c.op == "open") {
            const String f = c.arg("file");
            String text;
            if (f.indexOf('/') >= 0 || !storage::readText((String("/gmtools/dungeons/") + f).c_str(), text, 8192) || !deserialize(text)) {
                out = "Couldn't open that dungeon";
                return 400;
            }
            savedPath_ = String("/gmtools/dungeons/") + f;
            dirty_ = false;
        } else if (c.op == "saved") {
            std::vector<String> files;
            storage::listFiles("/gmtools/dungeons", files);
            out = "[";
            for (size_t i = 0; i < files.size(); i++) out += String(i ? "," : "") + web::js(files[i]);
            out += "]";
            return 200;
        } else if (c.op != "state") return 404;
        out = String("{\"title\":") + web::js(title_) + ",\"text\":" + web::js(serialize()) + ",\"layout\":" + web::js(layoutName(layout_)) +
              ",\"saved\":" + web::js(savedPath_.substring(savedPath_.lastIndexOf('/') + 1)) + ",\"dirty\":" + (dirty_ ? "true" : "false") +
              ",\"nodes\":[";
        for (int i = 0; i < 5; i++) out += String(i ? "," : "") + "[" + nodeX_[i] + "," + nodeY_[i] + "]";
        out += "]}";
        return 200;
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

    // node positions are laid out on the classic map strip (y 16..48); these
    // place them on the current one
    int nx(int i) const { return u(nodeX_[i]); }
    int ny(int i) const { return HEADER_H + u(nodeY_[i] - 16); }

    void corridor(int a, int b, uint16_t c) {
        // L-shaped corridors look more dungeon-like than diagonals
        int mx = nx(b);
        gfx.fillRect(std::min(nx(a), mx), ny(a), abs(mx - nx(a)), u(1), c);
        gfx.fillRect(mx, std::min(ny(a), ny(b)), u(1), abs(ny(b) - ny(a)), c);
    }

    void drawMap() {
        gfx.fillRect(0, HEADER_H, W, u(32), rgb(10, 9, 15));
        gfx.drawFastHLine(0, HEADER_H + u(31), W, C_EDGE);
        // corridors
        if (layout_ == Layout::Branch) { corridor(0, 1, C_EDGE); corridor(0, 2, C_EDGE); corridor(1, 3, C_EDGE); corridor(2, 3, C_EDGE); corridor(3, 4, C_EDGE); }
        else if (layout_ == Layout::Loop) { corridor(0, 1, C_EDGE); corridor(1, 2, C_EDGE); corridor(2, 4, C_EDGE); corridor(1, 3, C_EDGE); corridor(3, 4, C_EDGE); }
        else for (int i = 0; i < 4; i++) corridor(i, i + 1, C_EDGE);
        // entrance stub
        gfx.fillRect(u(2), ny(0), nx(0) - u(2), u(1), C_EDGE);

        for (int i = 0; i < 5; i++) {
            bool cur = page_ == i + 1;
            int s = cur ? u(7) : u(5);
            uint16_t col = ROOM_COLORS[i];
            gfx.fillRect(nx(i) - s, ny(i) - s, s * 2, s * 2, cur ? col : C_BG2);
            gfx.drawRect(nx(i) - s, ny(i) - s, s * 2, s * 2, col);
            touch::key(nx(i) - s - u(4), ny(i) - s - u(4), s * 2 + u(8), s * 2 + u(8), input::K_CHAR, '1' + i);
            if (wide()) gfx.drawRect(nx(i) - s + 1, ny(i) - s + 1, s * 2 - 2, s * 2 - 2, col);
            gfx.setFont(ui::font(&fonts::Font0));
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(cur ? C_BG : col);
            gfx.drawString(String(i + 1), nx(i) + u(1), ny(i) + u(1));
        }
        // party token glides between rooms
        if (page_ > 0) {
            int a = (int)floorf(token_), b = std::min(4, a + 1);
            float f = token_ - a;
            int tx = nx(a) + (int)((nx(b) - nx(a)) * f);
            int ty = ny(a) + (int)((ny(b) - ny(a)) * f) - u(11);
            ty += (int)(sinf(anim_ * 0.006f) * u(2) * 0.75f);
            gfx.fillTriangle(tx - u(3), ty - u(3), tx + u(3), ty - u(3), tx, ty + u(2), C_TEXT);
        }
    }

    void drawPage() {
        const int top = HEADER_H + u(35), bottom = H - FOOTER_H - u(2);
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
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        if (gfx.textWidth(heading) > W - u(12)) gfx.setFont(ui::font(&fonts::Font2));
        gfx.setTextColor(col);
        gfx.drawString(heading, u(6), top);

        // typewriter reveal on new content
        int shown = (int)(body.length() * easeOut(reveal_));
        String vis = body.substring(0, shown);

        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextColor(C_TEXT);
        const int lineH = u(10), y0 = top + u(18);
        int maxLines = (bottom - y0) / lineH;
        // measure total lines for scrolling
        gfx.setClipRect(0, y0, W, bottom - y0);
        int total = (textWrappedDry(body, W - u(14), lineH));
        maxScroll_ = std::max(0, total - maxLines);
        if (scroll_ > maxScroll_) scroll_ = maxScroll_;
        textWrapped(vis.c_str(), u(7), y0 - scroll_ * lineH, W - u(14), lineH);
        gfx.clearClipRect();
        if (maxScroll_ > 0) {
            int sh = std::max(u(6), (bottom - y0) * maxLines / total);
            int sy = y0 + (bottom - y0 - sh) * scroll_ / maxScroll_;
            gfx.fillRect(W - u(3), sy, u(2), sh, C_GOLD_DK);
        }
    }

    // Count wrapped lines without drawing (draws far off-screen).
    int textWrappedDry(const String& s, int w, int lineH) {
        const int off = lineH * 1000;
        int end = textWrapped(s.c_str(), 0, -off, w, lineH);
        return (end + off) / lineH;
    }

    // ---- the big screen: a drawn map beside the room's text
    //
    // The map is a grid of tiles: rooms placed after the layout's shape,
    // joined by L-shaped corridors with doors where a corridor meets a room.
    // It comes from seed_ alone (saved with the dungeon), so a loaded dungeon
    // looks the same as when it was saved.
    static constexpr int GW = 40, GH = 31, TILE = 18, MAP_X = 16;
    enum : uint8_t { ROCK, FLOOR, HALL };
    struct Room { int x, y, w, h; int cx() const { return x + w / 2; } int cy() const { return y + h / 2; } };
    struct Edge { int a, b; bool hFirst; };
    uint8_t cell_[GH][GW];
    int8_t owner_[GH][GW];    // room 0-4, a corridor 10+edge, the way in 9, -1 rock
    uint8_t doors_[GH][GW];   // on a corridor tile: bit 0 up, 1 right, 2 down, 3 left
    Room rooms_[5];
    std::vector<Edge> edges_;
    uint32_t seed_ = 1, rs_ = 1;

    int rnd(int n) {
        rs_ = rs_ * 1664525u + 1013904223u;
        return n > 0 ? (int)((rs_ >> 8) % n) : 0;
    }
    int mapY() const { return HEADER_H + 10; }
    int tileX(int x) const { return MAP_X + x * TILE; }
    int tileY(int y) const { return mapY() + y * TILE; }

    void carve(int x, int y, int8_t owner) {
        if (x < 0 || y < 0 || x >= GW || y >= GH || cell_[y][x] != ROCK) return;
        cell_[y][x] = HALL;
        owner_[y][x] = owner;
    }

    void buildMap() {
        rs_ = seed_;
        memset(cell_, ROCK, sizeof(cell_));
        memset(owner_, -1, sizeof(owner_));
        memset(doors_, 0, sizeof(doors_));
        // where each room's centre goes, as a fraction of the map, per layout
        static const float AT[4][5][2] = {
            {{.12f, .5f}, {.32f, .5f}, {.52f, .5f}, {.72f, .5f}, {.91f, .5f}},       // line
            {{.12f, .72f}, {.31f, .28f}, {.5f, .72f}, {.7f, .3f}, {.9f, .72f}},      // zigzag
            {{.12f, .5f}, {.36f, .2f}, {.36f, .8f}, {.66f, .5f}, {.91f, .5f}},       // branch
            {{.12f, .5f}, {.34f, .24f}, {.6f, .22f}, {.45f, .78f}, {.86f, .62f}},    // loop
        };
        static const int MINW[5] = {6, 6, 5, 9, 5}, MAXW[5] = {8, 9, 8, 12, 7};
        static const int MINH[5] = {5, 6, 5, 8, 5}, MAXH[5] = {7, 8, 7, 10, 6};
        const int li = (int)layout_;
        const bool line = layout_ == Layout::Line;
        for (int i = 0; i < 5; i++) {
            Room& r = rooms_[i];
            r.w = MINW[i] + rnd(MAXW[i] - MINW[i] + 1);
            r.h = MINH[i] + rnd(MAXH[i] - MINH[i] + 1);
            const int cx = (int)(AT[li][i][0] * GW) + rnd(3) - 1, cy = (int)(AT[li][i][1] * GH) + rnd(line ? 9 : 5) - (line ? 4 : 2);
            r.x = std::max(2, std::min(GW - r.w - 1, cx - r.w / 2));
            r.y = std::max(1, std::min(GH - r.h - 1, cy - r.h / 2));
        }
        // keep two tiles of rock between rooms: push apart, or shrink when stuck
        for (int it = 0; it < 60; it++) {
            bool moved = false;
            for (int i = 0; i < 5; i++)
                for (int j = i + 1; j < 5; j++) {
                    Room &a = rooms_[i], &b = rooms_[j];
                    const bool hit = a.x < b.x + b.w + 2 && b.x < a.x + a.w + 2 && a.y < b.y + b.h + 2 && b.y < a.y + a.h + 2;
                    if (!hit) continue;
                    moved = true;
                    Room& big = a.w * a.h >= b.w * b.h ? a : b;
                    const int dx = b.cx() - a.cx(), dy = b.cy() - a.cy();
                    if (abs(dx) * GH >= abs(dy) * GW) {
                        Room &l = dx >= 0 ? a : b, &r = dx >= 0 ? b : a;
                        if (l.x > 1) l.x--;
                        else if (r.x + r.w < GW - 1) r.x++;
                        else if (big.w > 4) big.w--;
                    } else {
                        Room &t = dy >= 0 ? a : b, &d = dy >= 0 ? b : a;
                        if (t.y > 1) t.y--;
                        else if (d.y + d.h < GH - 1) d.y++;
                        else if (big.h > 4) big.h--;
                    }
                }
            if (!moved) break;
        }
        for (int i = 0; i < 5; i++)
            for (int y = rooms_[i].y; y < rooms_[i].y + rooms_[i].h; y++)
                for (int x = rooms_[i].x; x < rooms_[i].x + rooms_[i].w; x++) { cell_[y][x] = FLOOR; owner_[y][x] = i; }

        // corridors, in the same shapes as the small screen's map
        edges_.clear();
        auto edge = [&](int a, int b) { edges_.push_back({a, b, rnd(2) == 0}); };
        if (layout_ == Layout::Branch) { edge(0, 1); edge(0, 2); edge(1, 3); edge(2, 3); edge(3, 4); }
        else if (layout_ == Layout::Loop) { edge(0, 1); edge(1, 2); edge(2, 4); edge(1, 3); edge(3, 4); }
        else for (int i = 0; i < 4; i++) edge(i, i + 1);
        for (size_t e = 0; e < edges_.size(); e++) {
            const Room &a = rooms_[edges_[e].a], &b = rooms_[edges_[e].b];
            int bx, by;
            bend(edges_[e], bx, by);
            const int8_t o = 10 + e;
            for (int x = std::min(a.cx(), bx); x <= std::max(a.cx(), bx); x++) carve(x, edges_[e].hFirst ? a.cy() : by, o);
            for (int y = std::min(a.cy(), by); y <= std::max(a.cy(), by); y++) carve(edges_[e].hFirst ? bx : a.cx(), y, o);
            for (int x = std::min(bx, b.cx()); x <= std::max(bx, b.cx()); x++) carve(x, by, o);
            for (int y = std::min(by, b.cy()); y <= std::max(by, b.cy()); y++) carve(bx, y, o);
        }
        // the way in, from the map's edge
        for (int x = 0; x < rooms_[0].x; x++) carve(x, rooms_[0].cy(), 9);

        // a door where a corridor runs straight into a room
        static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) {
                if (cell_[y][x] != HALL) continue;
                for (int d = 0; d < 4; d++) {
                    const int nx = x + DX[d], ny = y + DY[d], bx = x - DX[d], by = y - DY[d];
                    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || cell_[ny][nx] != FLOOR) continue;
                    const bool straight = bx >= 0 && by >= 0 && bx < GW && by < GH && cell_[by][bx] == HALL;
                    if (straight) doors_[y][x] |= 1 << d;
                }
            }
    }

    // The corner of edge e's corridor (from a's centre to b's).
    void bend(const Edge& e, int& x, int& y) const {
        const Room &a = rooms_[e.a], &b = rooms_[e.b];
        x = e.hFirst ? b.cx() : a.cx();
        y = e.hFirst ? a.cy() : b.cy();
    }

    // Fog: the rooms reached so far, and the corridors out of them.
    bool seen(int x, int y) const {
        if (!fog_) return true;
        const int o = owner_[y][x];
        const int reached = page_ - 1;
        if (o == 9) return true;
        if (o >= 0 && o < 5) return o <= reached;
        if (o >= 10) { const Edge& e = edges_[o - 10]; return e.a <= reached || e.b <= reached; }
        return false;
    }

    static uint16_t mix(uint16_t a, uint16_t b, int t) {   // t/256 of b
        const int r = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
        const int g = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
        const int bl = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
        return r << 11 | g << 5 | bl;
    }

    void drawDungeonMap() {
        const int X0 = MAP_X, Y0 = mapY(), MW = GW * TILE, MH = GH * TILE;
        const uint16_t ROCKC = rgb(5, 8, 16), FLOORC = rgb(36, 48, 78), GRIDC = rgb(46, 60, 94), WALL = rgb(206, 192, 156);
        gfx.fillRect(X0, Y0, MW, MH, ROCKC);
        for (int y = 0; y <= GH; y++)
            for (int x = 0; x <= GW; x++) gfx.drawPixel(X0 + x * TILE, Y0 + y * TILE, C_BG2);
        const int cur = page_ - 1;
        // floors, the room being read tinted with its colour
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) {
                if (cell_[y][x] == ROCK || !seen(x, y)) continue;
                const int o = owner_[y][x];
                const uint16_t f = o >= 0 && o < 5 && o == cur ? mix(FLOORC, ROOM_COLORS[o], 70) : FLOORC;
                gfx.fillRect(tileX(x), tileY(y), TILE, TILE, f);
                gfx.drawFastHLine(tileX(x), tileY(y), TILE, GRIDC);
                gfx.drawFastVLine(tileX(x), tileY(y), TILE, GRIDC);
            }
        // walls and doors
        static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) {
                if (cell_[y][x] == ROCK || !seen(x, y)) continue;
                for (int d = 0; d < 4; d++) {
                    const int nx = x + DX[d], ny = y + DY[d];
                    const bool out = nx < 0 || ny < 0 || nx >= GW || ny >= GH;
                    if (out && owner_[y][x] == 9) continue;   // the way in runs off the map
                    const uint8_t n = out ? ROCK : cell_[ny][nx];
                    bool wall = n == ROCK || (n == FLOOR && cell_[y][x] == FLOOR && owner_[ny][nx] != owner_[y][x]);
                    bool door = doors_[y][x] & (1 << d);
                    if (!out && cell_[y][x] == FLOOR && n == HALL) {
                        // the corridor's side draws this edge
                        continue;
                    }
                    if (cell_[y][x] == HALL && n == FLOOR && !door) wall = true;
                    if (!out && n != ROCK && !seen(nx, ny) && !door) wall = true;   // fog: the unknown is a wall
                    const int px = tileX(x), py = tileY(y);
                    if (wall) {
                        if (d == 0) gfx.fillRect(px - 1, py - 1, TILE + 2, 3, WALL);
                        if (d == 2) gfx.fillRect(px - 1, py + TILE - 2, TILE + 2, 3, WALL);
                        if (d == 3) gfx.fillRect(px - 1, py - 1, 3, TILE + 2, WALL);
                        if (d == 1) gfx.fillRect(px + TILE - 2, py - 1, 3, TILE + 2, WALL);
                    } else if (door) {
                        const uint16_t wood = rgb(160, 100, 48);
                        if (d == 0 || d == 2) {
                            const int ey = d == 0 ? py - 3 : py + TILE - 3;
                            gfx.fillRect(px + 2, ey, TILE - 4, 6, wood);
                            gfx.drawRect(px + 2, ey, TILE - 4, 6, WALL);
                        } else {
                            const int ex = d == 3 ? px - 3 : px + TILE - 3;
                            gfx.fillRect(ex, py + 2, 6, TILE - 4, wood);
                            gfx.drawRect(ex, py + 2, 6, TILE - 4, WALL);
                        }
                    }
                }
            }
        // stairs where the way in leaves the map
        const int sy = tileY(rooms_[0].cy());
        for (int i = 0; i < 4; i++) gfx.fillRect(X0 + 2 + i * 8, sy + 3, 3, TILE - 6, WALL);

        for (int i = 0; i < 5; i++) {
            const Room& r = rooms_[i];
            if (fog_ && i > cur) continue;
            drawFeature(i);
            // the room's number, in its corner
            const int nx = tileX(r.x) + 16, ny = tileY(r.y) + 16;
            gfx.fillCircle(nx, ny, 13, ROOM_COLORS[i]);
            gfx.drawCircle(nx, ny, 13, i == cur ? C_TEXT : C_BG);
            gfx.setFont(metrics().bold);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_BG);
            gfx.drawString(String(i + 1), nx, ny + 1);
            if (i == cur) {
                gfx.drawRect(tileX(r.x) - 4, tileY(r.y) - 4, r.w * TILE + 8, r.h * TILE + 8, ROOM_COLORS[i]);
                gfx.drawRect(tileX(r.x) - 5, tileY(r.y) - 5, r.w * TILE + 10, r.h * TILE + 10, ROOM_COLORS[i]);
            }
            const int i2 = i;
            touch::zone(tileX(r.x), tileY(r.y), r.w * TILE, r.h * TILE, [this, i2] { goRoom(i2 + 1); });
            touch::onHold(tileX(r.x), tileY(r.y), r.w * TILE, r.h * TILE, [this, i2] {
                goRoom(i2 + 1);
                holdmenu::open(String(i2 + 1) + ". " + ROOM_TITLES[i2], {{"Reroll this room", [this] { rerollPage(); sfx::diceRattle(); }},
                                                                       {fog_ ? "Fog off" : "Fog: players' view", [this] { fog_ = !fog_; sfx::click(); }}});
            });
        }
        drawParty();
        if (fog_) {
            gfx.setFont(metrics().small);
            gfx.setTextDatum(bottom_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString("players' view", X0 + MW - 8, Y0 + MH - 6);
        }
    }

    void goRoom(int p) {
        if (page_ == p) return;
        page_ = p;
        scroll_ = 0;
        reveal_ = 0;
        sfx::click();
    }

    // What each room holds, drawn in its middle.
    void drawFeature(int i) {
        const Room& r = rooms_[i];
        const int cx = tileX(r.x) + r.w * TILE / 2, cy = tileY(r.y) + r.h * TILE / 2;
        const uint16_t WALL = rgb(206, 192, 156);
        switch (i) {
            case 0:   // the guardian
                gfx.fillCircle(cx, cy, 13, C_BLOOD_DK);
                gfx.drawCircle(cx, cy, 13, C_BLOOD);
                gfx.drawCircle(cx, cy, 12, C_BLOOD);
                gfx.drawLine(cx - 6, cy - 6, cx + 6, cy + 6, C_TEXT);
                gfx.drawLine(cx + 6, cy - 6, cx - 6, cy + 6, C_TEXT);
                break;
            case 1:   // a circle of runes
                gfx.drawCircle(cx, cy, 24, C_ARCANE);
                gfx.drawCircle(cx, cy, 23, C_ARCANE);
                gfx.drawCircle(cx, cy, 10, C_ARCANE);
                for (int k = 0; k < 6; k++) {
                    const float a = k * 1.0472f;
                    gfx.fillRect(cx + (int)(cosf(a) * 17) - 2, cy + (int)(sinf(a) * 17) - 2, 5, 5, C_ARCANE);
                }
                break;
            case 2: {   // a pit trap
                const int s = TILE * 2;
                for (int k = 0; k < s; k += 6) {
                    gfx.drawFastHLine(cx - s / 2 + k, cy - s / 2, 3, C_ICE);
                    gfx.drawFastHLine(cx - s / 2 + k, cy + s / 2, 3, C_ICE);
                    gfx.drawFastVLine(cx - s / 2, cy - s / 2 + k, 3, C_ICE);
                    gfx.drawFastVLine(cx + s / 2, cy - s / 2 + k, 3, C_ICE);
                }
                gfx.drawLine(cx - s / 2 + 5, cy - s / 2 + 5, cx + s / 2 - 5, cy + s / 2 - 5, C_ICE);
                gfx.drawLine(cx + s / 2 - 5, cy - s / 2 + 5, cx - s / 2 + 5, cy + s / 2 - 5, C_ICE);
                break;
            }
            case 3: {   // pillars round the boss
                for (int k = 0; k < 4; k++) {
                    const int px = tileX(r.x + (k % 2 ? r.w - 3 : 2)), py = tileY(r.y + (k / 2 ? r.h - 3 : 2));
                    gfx.fillRect(px + 3, py + 3, TILE - 6, TILE - 6, WALL);
                }
                gfx.fillCircle(cx, cy, 18, C_BLOOD_DK);
                gfx.drawCircle(cx, cy, 18, C_GOLD);
                gfx.drawCircle(cx, cy, 17, C_GOLD);
                gfx.fillCircle(cx - 6, cy - 3, 3, C_GOLD);
                gfx.fillCircle(cx + 6, cy - 3, 3, C_GOLD);
                gfx.fillRect(cx - 6, cy + 7, 12, 3, C_GOLD);
                break;
            }
            case 4:   // the treasure chest
                gfx.fillRect(cx - 14, cy - 8, 28, 18, rgb(120, 70, 30));
                gfx.drawRect(cx - 14, cy - 8, 28, 18, C_GOLD);
                gfx.drawFastHLine(cx - 14, cy - 1, 28, C_GOLD);
                gfx.fillRect(cx - 2, cy - 3, 5, 6, C_GOLD);
                break;
        }
    }

    // The party: at the way in for the overview, gliding along the corridors.
    void roomCentre(int i, float& x, float& y) const {
        if (i < 0) { x = tileX(0) + TILE / 2; y = tileY(rooms_[0].cy()) + TILE / 2; return; }
        x = tileX(rooms_[i].cx()) + TILE / 2;
        y = tileY(rooms_[i].cy()) + TILE / 2;
    }

    void drawParty() {
        const int a = (int)floorf(token_), b = std::min(4, a + 1);
        const float f = token_ - a;
        float ax, ay, bx, by;
        roomCentre(a, ax, ay);
        roomCentre(b, bx, by);
        float x = ax + (bx - ax) * f, y = ay + (by - ay) * f;
        // along the corridor's corner when the two rooms share one
        for (auto& e : edges_)
            if (a >= 0 && ((e.a == a && e.b == b) || (e.a == b && e.b == a))) {
                int kx, ky;
                bend(e, kx, ky);
                const float cx = tileX(kx) + TILE / 2, cy = tileY(ky) + TILE / 2;
                if (f < 0.5f) { x = ax + (cx - ax) * f * 2; y = ay + (cy - ay) * f * 2; }
                else { x = cx + (bx - cx) * (f - 0.5f) * 2; y = cy + (by - cy) * (f - 0.5f) * 2; }
                break;
            }
        // offset to the side of the room's feature, bobbing
        x += 26;
        y += sinf(anim_ * 0.006f) * 3 - 22;
        gfx.fillEllipse(x, y + 18, 9, 3, rgb(3, 6, 14));   // its shadow
        gfx.fillTriangle(x - 10, y - 8, x + 10, y - 8, x, y + 12, C_GOLD);
        gfx.drawTriangle(x - 10, y - 8, x + 10, y - 8, x, y + 12, C_BG);
        gfx.fillCircle(x, y - 3, 3, C_BG);
    }

    // The room's text beside the map: tabs, the text, and actions.
    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawDungeonMap();
        const int RX = MAP_X + GW * TILE + 16, RW = W - RX - 16, top = mapY();
        // title
        gfx.setFont(m.title);
        if (gfx.textWidth(title_) > RW) gfx.setFont(m.bold);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(fitText(title_, RW), RX, top);
        gfx.setFont(m.small);
        gfx.setTextColor(dirty_ ? C_DIM : C_EMERALD);
        static const char* const SHAPE[] = {"in a line", "zigzag", "two ways through", "a loop"};
        gfx.drawString(String(SHAPE[(int)layout_]) + (dirty_ ? "" : "  -  saved"), RX, top + 44);
        // a tab per page: the hook, then the rooms
        const int ty = top + 76, tw = (RW - 5 * 6) / 6;
        for (int i = 0; i < 6; i++) {
            const uint16_t c = i ? ROOM_COLORS[i - 1] : C_GOLD;
            button(RX + i * (tw + 6), ty, tw, 52, i ? String(i) : String("Hook"), page_ == i, c, [this, i] { goRoom(i); });
        }
        // the page
        const int py = ty + 64, bh = 52, by2 = H - FOOTER_H - 10 - bh, by1 = by2 - bh - 8, ph = by1 - 10 - py;
        drawPanel(RX, py, RW, ph, C_BG2, page_ ? ROOM_COLORS[page_ - 1] : C_GOLD);
        String heading, body;
        pageText(heading, body);
        gfx.setFont(m.bold);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(page_ ? ROOM_COLORS[page_ - 1] : C_GOLD);
        gfx.drawString(fitText(heading, RW - 32), RX + 16, py + 14);
        const int shown = (int)(body.length() * easeOut(reveal_));
        gfx.setFont(m.body);
        gfx.setTextColor(C_TEXT);
        const int lh = m.line + 4, y0 = py + 56, rows = (py + ph - 12 - y0) / lh;
        const int total = textWrappedDry(body, RW - 32, lh);
        maxScroll_ = std::max(0, total - rows);
        scroll_ = std::min(scroll_, maxScroll_);
        gfx.setClipRect(RX + 8, y0, RW - 16, rows * lh);
        textWrapped(body.substring(0, shown).c_str(), RX + 16, y0 - scroll_ * lh, RW - 32, lh);
        gfx.clearClipRect();
        if (maxScroll_) {
            touch::key(RX, y0, RW, rows * lh / 2, input::K_UP);
            touch::key(RX, y0 + rows * lh / 2, RW, rows * lh / 2, input::K_DOWN);
            const int sh = std::max(20, rows * lh * rows / total);
            gfx.fillRect(RX + RW - 8, y0 + (rows * lh - sh) * scroll_ / maxScroll_, 4, sh, C_GOLD_DK);
        }
        // actions
        const int hw = (RW - 8) / 2, tw3 = (RW - 16) / 3;
        button(RX, by1, hw, bh, page_ ? "Reroll room" : "Reroll hook", false, C_GOLD, [this] { rerollPage(); sfx::diceRattle(); });
        button(RX + hw + 8, by1, hw, bh, "New dungeon", false, C_EMERALD, [this] { generate(); sfx::diceRattle(); });
        button(RX, by2, tw3, bh, "Save", false, C_EMERALD, [this] { saveToSd(); }, dirty_);
        button(RX + tw3 + 8, by2, tw3, bh, "Load", false, C_ICE, [this] { openBrowser(); });
        button(RX + 2 * (tw3 + 8), by2, tw3, bh, fog_ ? "Fog: on" : "Fog: off", fog_, C_ARCANE, [this] { fog_ = !fog_; sfx::click(); });
        drawFooter("</> rooms  Ent new  hold rooms for more");
        if (browsing_) drawBrowser();
    }

    // The heading and text of the page being read.
    void pageText(String& heading, String& body) {
        using namespace dtab;
        switch (page_) {
            case 0:
                heading = "The hook";
                body = r_[HOOK] + "\n\nTap a room on the map, or a number above, to read it. Holding a room rerolls it. Fog shows the players only the rooms they have reached.";
                break;
            case 1: body = String("Way in: ") + r_[ENTRANCE] + "\n\nGuardian: " + r_[GUARDIAN]; break;
            case 2: body = r_[PUZZLE]; break;
            case 3: body = r_[TRICK]; break;
            case 4: body = r_[BOSS] + " The fight takes place " + r_[TERRAIN] + "."; break;
            case 5: body = String("Reward: ") + r_[REWARD] + "\n\nTwist: " + r_[TWIST]; break;
        }
        if (page_ > 0) heading = String(page_) + ". " + ROOM_TITLES[page_ - 1];
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
        t += String("Map: ") + layoutName(layout_) + "\n";
        t += String("Seed: ") + seed_ + "\n\n";
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
        uint32_t seed = 0;
        while (start < (int)text.length()) {
            int nl = text.indexOf('\n', start);
            String l = nl < 0 ? text.substring(start) : text.substring(start, nl);
            l.replace("\r", "");
            start = nl < 0 ? text.length() : nl + 1;
            if (!title.length() && l.length()) { title = l; continue; }
            if (l.startsWith("Seed: ")) seed = strtoul(l.c_str() + 6, nullptr, 10);
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
        // older files have no seed: one from the title keeps their map the same each time
        if (!seed) for (size_t i = 0; i < title.length(); i++) seed = seed * 31 + title[i];
        seed_ = seed | 1;
        buildMap();
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
        const int rh = touchRow(11), w = wide() ? W * 2 / 3 : W - 28, h = wide() ? u(20) + 7 * rh : u(98), x = (W - w) / 2,
                  y = wide() ? (H - h) / 2 : u(20);
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_EMERALD);
        touch::modal(x, y, w, h);   // a tap outside closes it
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_EMERALD);
        gfx.drawString("Saved dungeons", x + u(6), y + u(4));
        gfx.setTextColor(C_DIM);
        gfx.setTextDatum(top_right);
        gfx.drawString(wide() ? "tap: load   hold: more" : "Ent load  X del", x + w - u(6), y + u(4));
        gfx.setTextDatum(top_left);
        int n = files_.size(), rows = 7;
        int first = std::max(0, std::min(browseSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int ry = y + u(17) + (i - first) * rh;
            bool s = i == browseSel_;
            if (s) gfx.fillRect(x + u(4), ry - u(1), w - u(8), rh - 2, C_PANEL);
            touch::row(x, ry - u(1), w, rh, browseSel_, i);
            touch::onHold(x, ry - u(1), w, rh, [this, i] {
                browseSel_ = i;
                auto key = [this](input::Key k, char ch) {
                    input::Event e;
                    e.key = k;
                    e.ch = e.base = ch;
                    handleBrowser(e);
                };
                String nm = files_[i].substring(0, files_[i].length() - 4);
                nm.replace('_', ' ');
                holdmenu::open(nm, {{"Load", [key] { key(input::K_ENTER, 0); }},
                                           {"Delete", [this, key] { confirmDelete_ = true; key(input::K_CHAR, 'x'); }, C_BLOOD, true}});
            });
            ry += touchPad(11);
            String nm = files_[i].substring(0, files_[i].length() - 4);
            nm.replace('_', ' ');
            gfx.setTextColor(s ? C_TEXT : rgb(180, 190, 180));
            gfx.drawString(nm, x + u(10), ry);
        }
    }
};

}  // namespace

App* makeDungeonApp() { return new DungeonApp(); }

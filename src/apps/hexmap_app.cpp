#ifdef BOARD_TAB5
#include "hexmap_tables.h"
#include "tables_api.h"
#include "../core/app.h"
#include "../core/db.h"
#include "../core/encounter.h"
#include "../core/encounter_gen.h"
#include "../core/hold_menu.h"
#include "../core/prompt.h"
#include "../core/rng.h"
#include "../core/roll_overlay.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/touch.h"
#include "../core/ui.h"
#include "../core/web.h"
#include "../core/widgets.h"
#include <algorithm>
#include <vector>

using namespace ui;

// Hex Map: a hexcrawl region, 24 x 16 six-mile hexes, made the way hexcrawl
// GMs make them by hand:
//
// - Seeded regions: a coast along one or two edges, mountain ranges as
//   random walks, a few seeds of forest, swamp, desert...
// - Neighbour-weighted terrain: every other hex grows from a neighbour,
//   rolling on a "next hex" table (forest mostly stays forest, hills climb
//   into mountains, water edges into swamp), as in the old wilderness
//   terrain tables. The climate swaps grassland for desert or tundra.
// - Rivers run downhill from the high ground to the nearest water (or off
//   the map); settlements sit by rivers and coasts, and roads join them.
// - Keyed hexes: about one land hex in five holds something (ruin, lair,
//   dungeon, camp, tower, shrine, landmark, wonder, resource), filled in from
//   the encounter generator and the Random Tables.
// - Hex templates: each hex is drawn as seven one-and-a-bit-mile sub-hexes;
//   the outer ones lean toward the terrain next door, so borders blend.
//
// The party travels hex by hex: each hex adds travel time (slow in mountains
// and swamps unless there's a road), uncovers the hexes around it, and has a
// 1-in-6 chance of an encounter. The players' view shows only what they've
// seen.
//
// Files: /gmtools/hexmaps/<id>.txt - "key: value" lines (the grids as strings
// of one character per hex), then a "## Hex N" section per keyed hex.
// Big screen only.

namespace {

const uint16_t ACCENT = rgb(120, 190, 110);
const char* const ROOT = "/gmtools/hexmaps";
constexpr int MW = 24, MH = 16, N = MW * MH;
constexpr float SQ3 = 1.7320508f;

enum Ter : uint8_t { WATER, PLAINS, FOREST, HILLS, MOUNT, SWAMP, DESERT, TUNDRA, NTER, UNSET = 255 };
const char TER_CH[] = "wpfhmsdt";
const char* const TER_NAME[] = {"Water", "Grassland", "Forest", "Hills", "Mountains", "Swamp", "Desert", "Tundra"};
const uint16_t TER_COL[] = {rgb(44, 92, 150), rgb(122, 160, 72), rgb(46, 110, 58), rgb(150, 138, 82),
                            rgb(122, 112, 106), rgb(84, 104, 74), rgb(214, 184, 118), rgb(206, 216, 222)};
const int ELEV[] = {0, 2, 2, 3, 4, 1, 2, 2};
// encounter::HABITATS index for each terrain
const int HABITAT[] = {2, 6, 5, 7, 8, 10, 3, 1};
// SRD Travel Terrain: foraging DC, navigation DC, encounter distance (x 10 ft)
const int FORAGE[] = {15, 15, 10, 15, 20, 10, 20, 20};
const int NAVIGATE[] = {10, 5, 15, 10, 15, 15, 10, 10};
const char* const ENC_DIST[] = {"6d6", "6d6", "2d8", "2d10", "4d10", "2d8", "6d6", "6d6"};
const char* const CLIMATES[] = {"Temperate", "Arid", "Cold"};

// "Next hex" table: the terrain a hex grows from (row) -> what it becomes (%).
//                         W   P   F   H   M   S   D   T
const uint8_t NEXT[NTER][NTER] = {{58, 26, 4, 0, 0, 12, 0, 0},    // water
                                  {2, 62, 18, 12, 0, 5, 1, 0},    // grassland
                                  {1, 20, 64, 12, 0, 3, 0, 0},    // forest
                                  {0, 20, 20, 42, 18, 0, 0, 0},   // hills
                                  {0, 0, 10, 40, 50, 0, 0, 0},    // mountains
                                  {14, 22, 14, 0, 0, 50, 0, 0},   // swamp
                                  {0, 10, 0, 14, 6, 0, 70, 0},    // desert
                                  {4, 10, 14, 12, 5, 0, 0, 55}};  // tundra

enum Kind : uint8_t { K_NONE, K_TOWN, K_VILLAGE, K_RUIN, K_DUNGEON, K_LAIR, K_CAMP, K_TOWER, K_SHRINE,
                      K_LANDMARK, K_WONDER, K_RESOURCE, K_WATER, K_NOTE, NKIND };
const char* const KIND_NAME[] = {"", "Town", "Village", "Ruin", "Dungeon", "Lair", "Camp", "Tower", "Shrine",
                                 "Landmark", "Wonder", "Resource", "On the water", "Note"};
const char* const KIND_KEY[] = {"", "town", "village", "ruin", "dungeon", "lair", "camp", "tower", "shrine",
                                "landmark", "wonder", "resource", "water", "note"};
// seen from a hex away (the rest only when the party walks in)
bool farSeen(int k) { return k == K_TOWN || k == K_VILLAGE || k == K_TOWER || k == K_LANDMARK; }

struct Lcg {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    int below(int n) { return n > 0 ? (int)(next() % n) : 0; }
};

// Odd rows sit half a hex to the right. Directions: E, NE, NW, W, SW, SE.
const int8_t DEVEN[6][2] = {{1, 0}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}};
const int8_t DODD[6][2] = {{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {0, 1}, {1, 1}};
int nb(int i, int d) {
    const int c = i % MW, r = i / MW;
    const int8_t* o = (r & 1) ? DODD[d] : DEVEN[d];
    const int nc = c + o[0], nr = r + o[1];
    return nc < 0 || nc >= MW || nr < 0 || nr >= MH ? -1 : nr * MW + nc;
}
int hexDist(int a, int b) {
    auto cube = [](int i, int& x, int& z) { const int c = i % MW, r = i / MW; x = c - (r - (r & 1)) / 2; z = r; };
    int ax, az, bx, bz;
    cube(a, ax, az);
    cube(b, bx, bz);
    const int dx = ax - bx, dz = az - bz, dy = -dx - dz;
    return std::max(abs(dx), std::max(abs(dy), abs(dz)));
}
String hexLabel(int i) {
    char b[8];
    snprintf(b, sizeof(b), "%02d%02d", i % MW + 1, i / MW + 1);
    return b;
}
String slugOf(const String& name) {
    String s;
    for (char c : name) {
        if (isalnum((unsigned char)c)) s += (char)tolower(c);
        else if (s.length() && s[s.length() - 1] != '_') s += '_';
        if (s.length() >= 24) break;
    }
    while (s.endsWith("_")) s.remove(s.length() - 1);
    return s.length() ? s : String("map");
}
uint16_t shade(uint16_t c, int pct) {   // pct of the colour, toward black
    const int r = ((c >> 11) & 31) * pct / 100, g = ((c >> 5) & 63) * pct / 100, b = (c & 31) * pct / 100;
    return (r << 11) | (g << 5) | b;
}

struct Feature {
    uint8_t kind = K_NONE;
    bool found = false;
    String name, text, notes;
};

struct Map {
    String id, name;
    uint32_t seed = 1;
    int climate = 0, party = 4, level = 1, pos = -1, hours = 0;
    uint8_t ter[N], river[N], road[N], seen[N];   // river: direction it flows (255 none); road: 6-bit mask
    std::vector<Feature> feat = std::vector<Feature>(N);
};

// ---------------------------------------------------------------- terrain
uint8_t grow(uint8_t from, int climate, Lcg& g) {
    int roll = g.below(100), t = 0;
    for (; t < NTER - 1; t++) { roll -= NEXT[from][t]; if (roll < 0) break; }
    if (climate == 1) {   // arid: grassland goes to desert, forests thin out
        if (t == PLAINS && g.below(100) < 55) t = DESERT;
        else if (t == FOREST && g.below(100) < 55) t = PLAINS;
        else if (t == SWAMP && g.below(100) < 60) t = PLAINS;
    } else if (climate == 2) {   // cold: tundra on the flats
        if (t == PLAINS && g.below(100) < 60) t = TUNDRA;
        else if (t == SWAMP && g.below(100) < 40) t = TUNDRA;
    } else if (t == DESERT || t == TUNDRA) t = PLAINS;
    return t;
}

void makeTerrain(Map& m) {
    Lcg g{m.seed};
    memset(m.ter, UNSET, N);
    memset(m.river, 255, N);
    memset(m.road, 0, N);
    // coast: one edge (sometimes two), its depth wandering 1-4 hexes
    if (g.below(100) < 70) {
        const int e0 = g.below(4), edges = g.below(100) < 25 ? 2 : 1;
        for (int k = 0; k < edges; k++) {
            const int e = (e0 + k) % 4;
            const int len = e % 2 ? MH : MW;
            int depth = 2;
            for (int p = 0; p < len; p++) {
                depth = std::max(1, std::min(4, depth + g.below(3) - 1));
                for (int d = 0; d < depth; d++) {
                    const int c = e == 0 || e == 2 ? p : e == 1 ? MW - 1 - d : d;
                    const int r = e == 1 || e == 3 ? p : e == 0 ? d : MH - 1 - d;
                    m.ter[r * MW + c] = WATER;
                }
            }
        }
    } else if (g.below(100) < 50) {
        m.ter[(3 + g.below(MH - 6)) * MW + 3 + g.below(MW - 6)] = WATER;   // a lake to grow from
    }
    auto inland = [&](int tries) {
        for (int t = 0; t < tries; t++) {
            const int i = g.below(N);
            if (m.ter[i] == UNSET) return i;
        }
        return -1;
    };
    // mountain ranges: random walks with hills on their flanks
    const int ranges = 1 + g.below(2) + (m.climate == 2 ? 1 : 0);
    for (int k = 0; k < ranges; k++) {
        int i = inland(50), d = g.below(6);
        for (int len = 5 + g.below(7); i >= 0 && len > 0; len--) {
            if (m.ter[i] == WATER) break;
            m.ter[i] = MOUNT;
            const int side = nb(i, (d + (g.below(2) ? 2 : 4)) % 6);
            if (side >= 0 && m.ter[side] == UNSET && g.below(100) < 45) m.ter[side] = HILLS;
            if (g.below(100) < 30) d = (d + (g.below(2) ? 1 : 5)) % 6;
            i = nb(i, d);
        }
    }
    // seeds for the rest
    auto seed = [&](uint8_t t, int n) { for (int k = 0; k < n; k++) { const int i = inland(40); if (i >= 0) m.ter[i] = t; } };
    seed(FOREST, m.climate == 1 ? 1 : 3 + g.below(3));
    seed(PLAINS, 3);
    seed(HILLS, 1 + g.below(2));
    if (m.climate == 1) seed(DESERT, 4 + g.below(3));
    if (m.climate == 2) seed(TUNDRA, 4 + g.below(3));
    if (m.climate != 1) {   // swamps by the water
        for (int k = 0, placed = 0; k < 200 && placed < 1 + g.below(2); k++) {
            const int i = g.below(N);
            if (m.ter[i] != UNSET) continue;
            bool wet = false;
            for (int d = 0; d < 6; d++) { const int n2 = nb(i, d); wet |= n2 >= 0 && m.ter[n2] == WATER; }
            if (wet) { m.ter[i] = SWAMP; placed++; }
        }
    }
    // grow: a random empty hex next to a filled one takes after that neighbour
    std::vector<int> cand;
    cand.reserve(N);
    for (;;) {
        cand.clear();
        for (int i = 0; i < N; i++) {
            if (m.ter[i] != UNSET) continue;
            for (int d = 0; d < 6; d++) { const int n2 = nb(i, d); if (n2 >= 0 && m.ter[n2] != UNSET) { cand.push_back(i); break; } }
        }
        if (cand.empty()) break;
        // a handful per pass keeps it quick and still organic
        for (int k = 0; k < 6 && !cand.empty(); k++) {
            const int at = g.below(cand.size());
            const int i = cand[at];
            cand.erase(cand.begin() + at);
            int from[6], nf = 0;
            for (int d = 0; d < 6; d++) { const int n2 = nb(i, d); if (n2 >= 0 && m.ter[n2] != UNSET) from[nf++] = n2; }
            if (nf) m.ter[i] = grow(m.ter[from[g.below(nf)]], m.climate, g);
        }
    }
    for (int i = 0; i < N; i++) if (m.ter[i] == UNSET) m.ter[i] = PLAINS;
    // smooth: a lone hex surrounded by one terrain usually joins it
    for (int i = 0; i < N; i++) {
        uint8_t t0 = 255;
        bool same = true;
        for (int d = 0; d < 6 && same; d++) {
            const int n2 = nb(i, d);
            if (n2 < 0) continue;
            if (t0 == 255) t0 = m.ter[n2];
            else same = m.ter[n2] == t0;
        }
        if (same && t0 != 255 && t0 != m.ter[i] && m.ter[i] != MOUNT && g.below(100) < 70) m.ter[i] = t0;
    }

    // rivers: downhill from high ground to the nearest water (or off the map)
    std::vector<int> dist(N, 9999), q;
    for (int i = 0; i < N; i++) if (m.ter[i] == WATER) { dist[i] = 0; q.push_back(i); }
    const bool toEdge = q.empty();
    if (toEdge)
        for (int i = 0; i < N; i++)
            if (i % MW == 0 || i % MW == MW - 1 || i / MW == 0 || i / MW == MH - 1) { dist[i] = 0; q.push_back(i); }
    for (size_t h = 0; h < q.size(); h++)
        for (int d = 0; d < 6; d++) {
            const int n2 = nb(q[h], d);
            if (n2 >= 0 && dist[n2] > dist[q[h]] + 1) { dist[n2] = dist[q[h]] + 1; q.push_back(n2); }
        }
    const int rivers = 1 + g.below(3);
    for (int k = 0, made = 0; k < 200 && made < rivers; k++) {
        int i = g.below(N);
        if ((m.ter[i] != MOUNT && m.ter[i] != HILLS) || dist[i] < 3 || m.river[i] != 255) continue;
        made++;
        for (int steps = 0; steps < 80; steps++) {
            if (m.ter[i] == WATER) break;
            if (dist[i] == 0) {   // at the map's edge: flow off it
                for (int d = 0; d < 6; d++) if (nb(i, d) < 0) { m.river[i] = d; break; }
                break;
            }
            int best = -1, bestD = 0, bestKey = 1 << 30;
            for (int d = 0; d < 6; d++) {
                const int n2 = nb(i, d);
                if (n2 < 0 || dist[n2] >= dist[i]) continue;
                const int key = ELEV[m.ter[n2]] * 16 + g.below(12);
                if (key < bestKey) { bestKey = key; best = n2; bestD = d; }
            }
            if (best < 0) break;
            m.river[i] = bestD;
            if (m.river[best] != 255) break;   // joined another river
            i = best;
        }
    }
}

int travelHours(const Map& m, int i) {
    const bool slow = (m.ter[i] == MOUNT || m.ter[i] == SWAMP) && !m.road[i];
    return slow ? 3 : 2;   // 6 miles at 2 or 3 mph
}

// Settlements where people would build, then roads between them.
void makeSettlements(Map& m, std::vector<int>& towns) {
    Lcg g{m.seed ^ 0x5eed5eedu};
    auto score = [&](int i) {
        const uint8_t t = m.ter[i];
        if (t == WATER || t == MOUNT || t == SWAMP) return -1;
        int s = 1 + (m.river[i] != 255 ? 3 : 0) + (t == PLAINS ? 1 : 0);
        for (int d = 0; d < 6; d++) {
            const int n2 = nb(i, d);
            if (n2 >= 0 && m.ter[n2] == WATER) { s += 2; break; }
        }
        for (int d = 0; d < 6; d++) { const int n2 = nb(i, d); if (n2 >= 0 && m.river[n2] == (d + 3) % 6) { s += 1; break; } }
        return s;
    };
    const int want = 4 + g.below(3);
    for (int k = 0; k < want; k++) {
        int best = -1, bestS = -1;
        for (int i = 0; i < N; i++) {
            const int c = i % MW, r = i / MW;
            if (c < 1 || c > MW - 2 || r < 1 || r > MH - 2) continue;
            int s = score(i);
            if (s < 0) continue;
            bool near = false;
            for (int t : towns) near |= hexDist(t, i) < 4;
            if (near) continue;
            s = s * 4 + g.below(10);
            if (s > bestS) { bestS = s; best = i; }
        }
        if (best < 0) break;
        towns.push_back(best);
    }
    if (towns.empty()) return;
    // roads: cheapest paths from the town (the first) to each village, sharing road where they can
    auto path = [&](int from, std::function<bool(int)> isGoal) {
        std::vector<int> cost(N, 1 << 30), prev(N, -1);
        std::vector<bool> done(N, false);
        cost[from] = 0;
        int goal = -1;
        for (;;) {
            int u = -1;
            for (int i = 0; i < N; i++) if (!done[i] && cost[i] < (1 << 30) && (u < 0 || cost[i] < cost[u])) u = i;
            if (u < 0) break;
            done[u] = true;
            if (u != from && isGoal(u)) { goal = u; break; }
            for (int d = 0; d < 6; d++) {
                const int v = nb(u, d);
                if (v < 0 || m.ter[v] == WATER) continue;
                static const int C[] = {0, 2, 4, 5, 12, 7, 3, 3};
                int c = m.road[v] ? 1 : C[m.ter[v]];
                if (m.river[u] == d || m.river[v] == (d + 3) % 6) c += 2;   // a ford or bridge
                if (cost[u] + c < cost[v]) { cost[v] = cost[u] + c; prev[v] = u; }
            }
        }
        for (int v = goal; v >= 0 && prev[v] >= 0; v = prev[v]) {
            const int u = prev[v];
            for (int d = 0; d < 6; d++)
                if (nb(u, d) == v) { m.road[u] |= 1 << d; m.road[v] |= 1 << ((d + 3) % 6); }
        }
        return goal;
    };
    for (size_t k = 1; k < towns.size(); k++) { const int t = towns[k]; path(towns[0], [t](int i) { return i == t; }); }
    // and one road out of the region
    const int out = path(towns[0], [](int i) { const int c = i % MW, r = i / MW; return c == 0 || c == MW - 1 || r == 0 || r == MH - 1; });
    if (out >= 0)
        for (int d = 0; d < 6; d++) if (nb(out, d) < 0) { m.road[out] |= 1 << d; break; }
}

// ---------------------------------------------------------------- file
String gridString(const uint8_t* a, char (*f)(uint8_t)) {
    String s;
    s.reserve(N);
    for (int i = 0; i < N; i++) s += f(a[i]);
    return s;
}

bool saveMap(const Map& m) {
    String s;
    auto put = [&](const char* k, const String& v) { s += k; s += ": "; s += v; s += "\n"; };
    put("name", m.name);
    put("seed", String(m.seed));
    put("climate", String(m.climate));
    put("party", String(m.party));
    put("level", String(m.level));
    put("pos", String(m.pos));
    put("hours", String(m.hours));
    put("terrain", gridString(m.ter, [](uint8_t v) { return v < NTER ? TER_CH[v] : 'p'; }));
    put("river", gridString(m.river, [](uint8_t v) { return v < 6 ? (char)('0' + v) : '-'; }));
    put("road", gridString(m.road, [](uint8_t v) { return (char)('0' + (v & 63)); }));
    put("seen", gridString(m.seen, [](uint8_t v) { return (char)('0' + std::min<uint8_t>(v, 2)); }));
    for (int i = 0; i < N; i++) {
        const Feature& f = m.feat[i];
        if (f.kind == K_NONE && !f.notes.length()) continue;
        s += String("\n## Hex ") + i + "\n";
        put("kind", KIND_KEY[f.kind]);
        if (f.name.length()) put("name", f.name);
        if (f.text.length()) put("text", db::escape(f.text));
        if (f.notes.length()) put("notes", db::escape(f.notes));
        if (f.found) put("found", "1");
    }
    storage::mkdir(ROOT);
    return storage::writeText((String(ROOT) + "/" + m.id + ".txt").c_str(), s);
}

bool loadMap(const String& id, Map& m, bool headerOnly = false) {
    String txt;
    if (!storage::readText((String(ROOT) + "/" + id + ".txt").c_str(), txt, headerOnly ? 4096 : 131072)) return false;
    m = Map();
    m.id = id;
    memset(m.ter, PLAINS, N);
    memset(m.river, 255, N);
    memset(m.road, 0, N);
    memset(m.seen, 0, N);
    Feature* f = nullptr;
    int start = 0;
    while (start < (int)txt.length()) {
        int nl = txt.indexOf('\n', start);
        String l = nl < 0 ? txt.substring(start) : txt.substring(start, nl);
        start = nl < 0 ? txt.length() : nl + 1;
        if (l.startsWith("## Hex ")) {
            if (headerOnly) break;
            const int i = l.substring(7).toInt();
            f = i >= 0 && i < N ? &m.feat[i] : nullptr;
            continue;
        }
        const int c = l.indexOf(": ");
        if (c <= 0) continue;
        const String k = l.substring(0, c), v = l.substring(c + 2);
        if (f) {
            if (k == "kind") { for (int x = 0; x < NKIND; x++) if (v == KIND_KEY[x]) f->kind = x; }
            else if (k == "name") f->name = v;
            else if (k == "text") f->text = db::unescape(v);
            else if (k == "notes") f->notes = db::unescape(v);
            else if (k == "found") f->found = v == "1";
            continue;
        }
        if (k == "name") m.name = v;
        else if (k == "seed") m.seed = strtoul(v.c_str(), nullptr, 10);
        else if (k == "climate") m.climate = std::max(0, std::min(2, (int)v.toInt()));
        else if (k == "party") m.party = std::max(1, std::min(10, (int)v.toInt()));
        else if (k == "level") m.level = std::max(1, std::min(20, (int)v.toInt()));
        else if (k == "pos") m.pos = std::max(-1, std::min(N - 1, (int)v.toInt()));
        else if (k == "hours") m.hours = std::max(0, (int)v.toInt());
        else if (v.length() >= (unsigned)N) {
            for (int i = 0; i < N; i++) {
                const char ch = v[i];
                if (k == "terrain") { const char* p = strchr(TER_CH, ch); m.ter[i] = p ? p - TER_CH : PLAINS; }
                else if (k == "river") m.river[i] = ch >= '0' && ch <= '5' ? ch - '0' : 255;
                else if (k == "road") m.road[i] = (ch - '0') & 63;
                else if (k == "seen") m.seen[i] = ch >= '0' && ch <= '2' ? ch - '0' : 0;
            }
        }
    }
    return true;
}

// Party moves into hex i: it's visited, its neighbours seen (two hexes out
// from hills and mountains), and what can be seen from afar is found.
void reveal(Map& m, int i) {
    m.seen[i] = 2;
    m.feat[i].found = m.feat[i].kind != K_NONE && m.feat[i].kind != K_NOTE;
    const int range = m.ter[i] == MOUNT || m.ter[i] == HILLS ? 2 : 1;
    for (int j = 0; j < N; j++) {
        if (j == i || hexDist(i, j) > range) continue;
        if (!m.seen[j]) m.seen[j] = 1;
        if (farSeen(m.feat[j].kind)) m.feat[j].found = true;
    }
}

class HexMapApp : public App {
public:
    const char* name() const override { return "Hex Map"; }
    const char* blurb() const override { return "Generate and explore a hexcrawl region."; }
    uint16_t accent() const override { return ACCENT; }
    bool bigLayout() const override { return true; }

    void drawIcon(int cx, int cy, float t) override {
        // three hexes: water, forest, mountain
        const int r = 11;
        const uint16_t cols[] = {TER_COL[WATER], TER_COL[FOREST], TER_COL[MOUNT]};
        const int ox[] = {-10, 10, 0}, oy[] = {-8, -8, 9};
        for (int k = 0; k < 3; k++) fillHex(cx + ox[k], cy + oy[k], r, cols[k]);
        gfx.fillTriangle(cx - 6, cy + 14, cx + 6, cy + 14, cx, cy + 3, rgb(230, 230, 236));
        gfx.fillTriangle(cx + 7, cy - 4, cx + 13, cy - 4, cx + 10, cy - 13, rgb(20, 70, 30));
        const int bob = (int)(sinf(t * 3) * 2);
        gfx.fillCircle(cx - 10, cy - 8 + bob, 3, C_BLOOD);
    }

    void onEnter() override {
        input::setTextMode(false);
        screen_ = LIST;
        loadList();
        if (!storage::ok()) toast("No SD card - see Settings", C_BLOOD);
    }
    void onExit() override {
        if (screen_ != LIST) saveMap(m_);
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
        if (screen_ == PLAYERS) {
            if (e.key == input::K_BACK || (e.key == input::K_CHAR && e.base == 'p')) { screen_ = MAP; sfx::back(); }
            return true;
        }
        if (screen_ == LIST) {
            if (e.key == input::K_BACK) return false;
            if (e.key == input::K_ENTER) newMapPrompt();
            return true;
        }
        // the DM's map
        switch (e.key) {
            case input::K_BACK:
                if (sel_ >= 0) { sel_ = -1; sfx::back(); return true; }
                saveMap(m_);
                screen_ = LIST;
                loadList();
                sfx::back();
                return true;
            case input::K_LEFT: case input::K_RIGHT: case input::K_UP: case input::K_DOWN: {
                if (e.gesture) return true;   // a drag on the panel isn't a step across the map
                int i = sel_ >= 0 ? sel_ : std::max(0, m_.pos);
                int c = i % MW, r = i / MW;
                if (e.key == input::K_LEFT) c = std::max(0, c - 1);
                if (e.key == input::K_RIGHT) c = std::min(MW - 1, c + 1);
                if (e.key == input::K_UP) r = std::max(0, r - 1);
                if (e.key == input::K_DOWN) r = std::min(MH - 1, r + 1);
                select(r * MW + c);
                return true;
            }
            case input::K_ENTER: if (sel_ >= 0) travelTo(sel_); return true;
            case input::K_CHAR:
                if (e.base == 'p') { screen_ = PLAYERS; sfx::select(); }
                else if (e.base == 'n' && sel_ >= 0) editNote(sel_);
                else if (e.base == 'r' && sel_ >= 0 && m_.feat[sel_].kind != K_NONE) reroll(sel_);
                else if (e.base == 'e' && sel_ >= 0) encounterAt(sel_);
                return true;
            default: return true;
        }
    }

    void draw() override {
        drawBackground();
        if (screen_ == LIST) drawList();
        else if (screen_ == PLAYERS) drawPlayers();
        else drawMapScreen();
        prompt_.draw();
    }

    int web(const WebCall& c, String& out) override;

private:
    enum Screen { LIST, MAP, PLAYERS };
    Screen screen_ = LIST;
    Prompt prompt_;
    std::function<void(const String&)> onPrompt_;
    encounter::Generator gen_;
    Map m_;
    int sel_ = -1;
    String event_;       // the last thing that happened on the road
    int eventHex_ = -1;
    float textPos_ = 0;

    void ask(const String& label, const String& cur, int maxLen, std::function<void(const String&)> fn) {
        onPrompt_ = std::move(fn);
        prompt_.open(label, cur, 1, maxLen);
    }

    // ================================================================ list
    struct Info { String id, name; int climate; };
    std::vector<Info> list_;
    float listPos_ = 0;

    void loadList() {
        list_.clear();
        std::vector<String> files;
        storage::listFiles(ROOT, files);
        for (auto& f : files) {
            const String id = f.substring(0, f.length() - 4);
            Map m;
            if (loadMap(id, m, true)) list_.push_back({id, m.name.length() ? m.name : id, m.climate});
        }
    }

    void newMapPrompt() {
        std::vector<holdmenu::Item> items;
        for (int c = 0; c < 3; c++)
            items.push_back({String(CLIMATES[c]) + " region", [this, c] {
                ask("Region name (empty: roll one)", "", 40, [this, c](const String& v) { openMap(createMap(v, c)); });
            }});
        holdmenu::open("New map", std::move(items));
    }

    // Makes, keys and saves a new map; returns its id ("" if it couldn't).
    String createMap(String name, int climate) {
        name.trim();
        if (!storage::ok()) { sfx::error(); toast("No SD card", C_BLOOD); return ""; }
        if (!name.length()) name = String("The ") + rng::pick(hexmap::MAP_ADJ) + " " + rng::pick(hexmap::MAP_NOUN);
        String id = slugOf(name);
        for (int i = 2; storage::exists((String(ROOT) + "/" + id + ".txt").c_str()); i++) id = slugOf(name) + "_" + i;
        m_ = Map();
        m_.id = id;
        m_.name = name;
        m_.climate = climate;
        m_.party = 4;
        m_.level = 1;
        build(esp_random() | 1);
        saveMap(m_);
        return id;
    }

    // Terrain, settlements and keyed hexes from a seed (keeps name and party).
    void build(uint32_t seed) {
        m_.seed = seed;
        makeTerrain(m_);
        std::vector<int> towns;
        makeSettlements(m_, towns);
        m_.feat.assign(N, Feature());
        memset(m_.seen, 0, N);
        m_.hours = 0;
        for (size_t k = 0; k < towns.size(); k++) rollFeature(towns[k], k == 0 ? K_TOWN : K_VILLAGE);
        Lcg g{seed ^ 0xfea7u};
        for (int i = 0; i < N; i++) {
            if (m_.feat[i].kind != K_NONE) continue;
            if (m_.ter[i] == WATER) { if (g.below(14) == 0) rollFeature(i, K_WATER); continue; }
            if (g.below(5)) continue;
            rollFeature(i, pickKind(m_.ter[i], g));
        }
        m_.pos = towns.empty() ? N / 2 : towns[0];
        reveal(m_, m_.pos);
        sel_ = -1;
        event_ = "";
        eventHex_ = -1;
    }

    static uint8_t pickKind(uint8_t t, Lcg& g) {
        //               ruin dung lair camp towr shrn land wond reso
        int w[9] = {16, 10, 18, 10, 6, 8, 12, 8, 12};
        if (t == MOUNT) { w[1] += 10; w[2] += 6; w[3] -= 6; }
        if (t == FOREST) { w[2] += 6; w[3] += 4; w[5] += 4; }
        if (t == SWAMP) { w[2] += 8; w[0] += 6; }
        if (t == PLAINS) { w[3] += 4; w[0] += 4; }
        if (t == DESERT) { w[0] += 8; w[6] += 6; }
        if (t == HILLS) { w[1] += 6; w[4] += 4; }
        if (t == TUNDRA) { w[2] += 6; w[7] += 4; }
        int total = 0;
        for (int x : w) total += x;
        int r = g.below(total);
        for (int k = 0; k < 9; k++) { r -= w[k]; if (r < 0) return K_RUIN + k; }
        return K_RUIN;
    }

    void openMap(const String& id) {
        if (!id.length()) return;
        if (!loadMap(id, m_)) { sfx::error(); toast("Couldn't read that map", C_BLOOD); return; }
        if (m_.pos >= 0 && !m_.seen[m_.pos]) reveal(m_, m_.pos);
        screen_ = MAP;
        sel_ = -1;
        event_ = "";
        eventHex_ = -1;
        sfx::select();
    }

    void drawList() {
        using namespace widgets;
        const Metrics& mt = metrics();
        drawHeader("Hex Map", ACCENT);
        const int top = HEADER_H + 10, bottom = H - FOOTER_H - 10, n = list_.size();
        button(16, top, 360, 56, "+ New map", false, C_GOLD, [this] { newMapPrompt(); });
        gfx.setFont(mt.small);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString("A hexcrawl region: 6-mile hexes, rivers, roads, ruins and lairs.", 396, top + 28);
        const int gy = top + 70, gh = bottom - gy, cw = (W - 48) / 2, ch = 110, gap = 14;
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
            gfx.setFont(mt.title);
            gfx.setTextColor(C_TEXT);
            gfx.drawString(fitText(list_[i].name, cw - 44), x + 24, y + 14);
            gfx.setFont(mt.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(CLIMATES[list_[i].climate]) + " region", x + 24, y + 66);
            const int vy = std::max(y, gy), vh = std::min(y + ch, gy + gh) - vy;
            if (vh <= 0) continue;
            touch::zone(x, vy, cw, vh, [this, i] { openMap(list_[i].id); });
            touch::onHold(x, vy, cw, vh, [this, i] {
                const String id = list_[i].id;
                holdmenu::open(list_[i].name, {{"Open", [this, id] { openMap(id); }},
                                               {"Delete this map", [this, id] {
                                                   storage::remove((String(ROOT) + "/" + id + ".txt").c_str());
                                                   loadList();
                                                   toast("Map deleted", C_DIM);
                                               }, C_BLOOD, true}});
            });
        }
        gfx.clearClipRect();
        if (!n) {
            gfx.setFont(mt.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Roll a region: coast, mountains, rivers,", W / 2, gy + gh / 2 - 20);
            gfx.drawString("towns and roads, and a secret in every fifth hex.", W / 2, gy + gh / 2 + 20);
        }
        drawFooter("Ent new map  Hold: map options  Esc back");
    }

    // ================================================================ keyed hexes
    int partyLevel() const { return m_.level; }

    String monsters(int habitat, int diff, int group = 0, int type = 0) {
        if (!gen_.isOpen() && !gen_.open()) return "";
        encounter::GenOptions o;
        o.partySize = m_.party;
        o.partyLevel = m_.level;
        o.habitat = habitat;
        o.diff = diff;
        o.group = group;
        o.type = type;
        encounter::GenResult res;
        if (!gen_.generate(o, res)) return "";
        return encounter::describe(res.monsters);
    }

    String loot(bool hoard) {
        const int lv = std::min(20, m_.level + (hoard ? 2 : 0));
        return rollRandomTable(lv <= 4 ? "Loot (Tier 1, levels 1-4)" : lv <= 10 ? "Loot (Tier 2, levels 5-10)"
                               : lv <= 16 ? "Loot (Tier 3, levels 11-16)" : "Loot (Tier 4, levels 17-20)");
    }

    String settlementName(int i) {
        const uint8_t t = m_.ter[i];
        bool coast = false, river = m_.river[i] != 255;
        for (int d = 0; d < 6; d++) {
            const int n2 = nb(i, d);
            if (n2 < 0) continue;
            coast |= m_.ter[n2] == WATER;
            river |= m_.river[n2] == (d + 3) % 6;
        }
        const char* end = river && rng::below(2) ? rng::pick(hexmap::END_RIVER)
                          : coast ? rng::pick(hexmap::END_COAST)
                          : t == HILLS ? rng::pick(hexmap::END_HILL)
                          : t == FOREST ? rng::pick(hexmap::END_FOREST) : rng::pick(hexmap::END_PLAIN);
        return String(rng::pick(hexmap::TOWN_START)) + end;
    }

    // Fills hex i with a feature of `kind`, from the generators.
    void rollFeature(int i, uint8_t kind) {
        Feature& f = m_.feat[i];
        const String notes = f.notes;
        f = Feature();
        f.kind = kind;
        f.notes = notes;
        const int hab = HABITAT[m_.ter[i]];
        String t;
        auto line = [&](const String& label, const String& v) { if (v.length()) t += (t.length() ? "\n" : "") + label + v; };
        switch (kind) {
            case K_TOWN: case K_VILLAGE: {
                f.name = settlementName(i);
                const int pop = kind == K_TOWN ? 1500 + rng::below(3500) : 50 + rng::below(400);
                line("", kind == K_TOWN ? String(rng::pick(hexmap::TOWN_EXTRA)) : String("A village living on ") + rng::pick(hexmap::VILLAGE_TRADE) + ".");
                line("People: about ", String(pop));
                line("Inn: ", rollRandomTable("Tavern"));
                line("Who's who: ", rollRandomTable("NPC"));
                if (kind == K_TOWN) line("Also: ", rollRandomTable("NPC"));
                line("Trouble: ", rng::pick(hexmap::VILLAGE_TROUBLE));
                line("Rumor: ", rollRandomTable("Rumor"));
                if (kind == K_TOWN) line("", "Shops: open the Magic Shop app for what's for sale.");
                break;
            }
            case K_RUIN: {
                f.name = rollRandomTable("Location");
                line("", String("Here: ") + rng::pick(hexmap::RUIN_KIND) + ", " + rng::pick(hexmap::RUIN_STATE) + ".");
                if (rng::below(2)) line("Lurking: ", monsters(hab, 0));
                line("Treasure: ", loot(false));
                break;
            }
            case K_DUNGEON: {
                f.name = rollRandomTable("Location");
                line("", String(rng::pick(hexmap::DUNGEON_WAY)) + ".");
                line("Guardian: ", monsters(4, 2, 2));
                line("Treasure: ", loot(true));
                line("", "Map it with the 5-Room Dungeon or Dungeon23 app.");
                break;
            }
            case K_LAIR: {
                const String who = monsters(hab, 2);
                const int x = who.indexOf("x ");
                String first = who;
                if (x >= 0 && x < 4) first = who.substring(x + 2);
                const int cut = first.indexOf(',');
                if (cut > 0) first = first.substring(0, cut);
                f.name = first.length() ? first + " lair" : String("A lair");
                line("", rng::pick(hexmap::LAIR_SIGN));
                line("Inside: ", who);
                line("Treasure: ", loot(true));
                break;
            }
            case K_CAMP: {
                const String kindTxt = rng::pick(hexmap::CAMP_KIND);
                f.name = String(char(toupper(kindTxt[0]))) + kindTxt.substring(1);
                line("Who: ", monsters(hab, 1, 0, 10));
                line("Leader: ", rollRandomTable("NPC"));
                line("Loot: ", loot(false));
                break;
            }
            case K_TOWER: {
                const String who = rollRandomTable("_last");
                f.name = (who.length() ? who : String("Old")) + "'s Tower";
                line("", String("It's ") + rng::pick(hexmap::TOWER_KIND) + ".");
                line("Lives here: ", rollRandomTable("NPC"));
                line("Knows: ", rollRandomTable("Rumor"));
                break;
            }
            case K_SHRINE:
                f.name = String("Shrine to ") + rng::pick(hexmap::SHRINE_TO);
                line("", rng::pick(hexmap::SHRINE_GIFT));
                break;
            case K_LANDMARK:
                f.name = rng::pick(hexmap::LANDMARK);
                if (rng::below(3) == 0) line("Locals say: ", rollRandomTable("Rumor"));
                break;
            case K_WONDER:
                f.name = rng::pick(hexmap::WONDER);
                break;
            case K_RESOURCE:
                f.name = rng::pick(hexmap::RESOURCE);
                break;
            case K_WATER:
                f.name = rng::pick(hexmap::WATER_FEATURE);
                if (rng::below(2)) line("Lurking: ", monsters(2, 1));
                if (rng::below(2)) line("Treasure: ", loot(false));
                break;
            default: break;
        }
        f.text = t;
        if (m_.seen[i] == 2 || (m_.seen[i] && farSeen(kind))) f.found = true;
    }

    void reroll(int i) {
        const uint8_t k = m_.feat[i].kind;
        if (k == K_NONE || k == K_NOTE) return;
        rollFeature(i, k);
        saveMap(m_);
        sfx::select();
    }

    void editNote(int i) {
        ask(String("Note for hex ") + hexLabel(i), m_.feat[i].notes, 120, [this, i](const String& v) {
            Feature& f = m_.feat[i];
            f.notes = v;
            if (f.kind == K_NONE && v.length()) f.kind = K_NOTE;
            if (f.kind == K_NOTE && !v.length()) f.kind = K_NONE;
            saveMap(m_);
        });
    }

    // ================================================================ travel
    void select(int i) {
        if (i == sel_) return;
        sel_ = i;
        textPos_ = 0;
        sfx::click();
    }

    // Moves the party: one hex (with travel time and an encounter check), or
    // anywhere when jumping.
    void travelTo(int i, bool jump = false) {
        if (i < 0 || i >= N) return;
        if (i == m_.pos) { toast("The party is here", C_DIM); return; }
        if (!jump && (m_.pos < 0 || hexDist(m_.pos, i) != 1)) { sfx::error(); toast("Next door only - hold to jump the party there", C_BLOOD); return; }
        if (!jump && m_.ter[i] == WATER) { sfx::error(); toast("That's water - hold to jump if they have a boat", C_BLOOD); return; }
        m_.pos = i;
        const bool firstTime = m_.seen[i] < 2;
        reveal(m_, i);
        if (!jump) {
            m_.hours += travelHours(m_, i);
            event_ = String("Day ") + (m_.hours / 8 + 1) + ": into " + TER_NAME[m_.ter[i]] + " (" + hexLabel(i) + ")";
            eventHex_ = -1;
            if (m_.feat[i].kind != K_NONE && m_.feat[i].kind != K_NOTE && firstTime) event_ += String(" - found: ") + m_.feat[i].name;
            if (rng::roll(6) == 1) encounterAt(i);
            else sfx::click();
        }
        saveMap(m_);
    }

    void encounterAt(int i) {
        const int d = rng::below(100);
        const String who = monsters(HABITAT[m_.ter[i]], d < 50 ? 0 : d < 85 ? 1 : 2);
        if (!who.length()) { toast("No monsters fit", C_DIM); return; }
        event_ = String("Encounter in ") + hexLabel(i) + ": " + who;
        eventHex_ = i;
        rollui::show(String("Encounter: ") + who, {{"Distance (x 10 ft)", ENC_DIST[m_.ter[i]], ""}});
        saveMap(m_);
    }

    void fight(const String& text) {
        String unknown;
        auto list = encounter::parseList(text, unknown);
        if (list.empty()) { sfx::error(); toast("No monsters to fight here", C_BLOOD); return; }
        encounter::enqueue(list, true);
        if (unknown.length()) toast((String("Unknown: ") + unknown).c_str(), C_GOLD);
        sfx::crit();
        requestApp("Initiative");
    }
    // The monsters in a feature's text ("Inside: 2x Owlbear" ...).
    static String monstersIn(const String& text) {
        for (const char* k : {"Inside: ", "Lurking: ", "Guardian: ", "Who: "}) {
            const int a = text.indexOf(k);
            if (a < 0) continue;
            const int e = text.indexOf('\n', a);
            return text.substring(a + strlen(k), e < 0 ? text.length() : e);
        }
        return "";
    }

    // ================================================================ drawing
    static void fillHex(int cx, int cy, int r, uint16_t col) {
        const int hw = (int)(r * SQ3 / 2 + 0.5f), hr = r / 2;
        gfx.fillRect(cx - hw, cy - hr, 2 * hw + 1, 2 * hr + 1, col);
        gfx.fillTriangle(cx, cy - r, cx - hw, cy - hr, cx + hw, cy - hr, col);
        gfx.fillTriangle(cx, cy + r, cx - hw, cy + hr, cx + hw, cy + hr, col);
    }
    static void outlineHex(int cx, int cy, int r, uint16_t col) {
        int px[6], py[6];
        for (int k = 0; k < 6; k++) {
            const float a = (60 * k - 90) * DEG_TO_RAD;
            px[k] = cx + (int)lroundf(r * cosf(a));
            py[k] = cy + (int)lroundf(r * sinf(a));
        }
        for (int k = 0; k < 6; k++) gfx.drawLine(px[k], py[k], px[(k + 1) % 6], py[(k + 1) % 6], col);
    }
    // Small pictures of each terrain, sized to the hex.
    static void glyph(uint8_t t, int cx, int cy, int r, uint32_t salt) {
        const int s = std::max(3, r / 4);
        switch (t) {
            case FOREST:
                for (int k = 0; k < 3; k++) {
                    const int x = cx + (k - 1) * s * 2 + (k == 1 ? 0 : 0), y = cy + (k == 1 ? -s : s / 2);
                    gfx.fillTriangle(x - s, y + s, x + s, y + s, x, y - s - s / 2, rgb(24, 70, 34));
                    gfx.fillRect(x - 1, y + s, 2, s / 2 + 1, rgb(70, 50, 30));
                }
                break;
            case HILLS:
                for (int k = 0; k < 2; k++) {
                    const int x = cx + (k ? s : -s), y = cy + (k ? s / 2 : -s / 3);
                    gfx.fillCircle(x, y + s, s + 1, rgb(120, 108, 62));
                    gfx.fillRect(x - s - 2, y + s + 1, 2 * s + 5, s + 2, TER_COL[HILLS]);
                }
                break;
            case MOUNT: {
                const int h = s * 3;
                gfx.fillTriangle(cx - h, cy + s + 2, cx + h, cy + s + 2, cx, cy - h + 2, rgb(86, 80, 78));
                gfx.fillTriangle(cx - h / 3, cy - h / 3 + 2, cx + h / 3, cy - h / 3 + 2, cx, cy - h + 2, rgb(236, 236, 242));
                break;
            }
            case WATER:
                for (int k = 0; k < 2; k++) {
                    const int y = cy + (k ? s : -s / 2), x = cx + (k ? -s / 2 : s / 2);
                    gfx.drawLine(x - s, y, x - s / 2, y - 2, rgb(120, 170, 220));
                    gfx.drawLine(x - s / 2, y - 2, x, y, rgb(120, 170, 220));
                    gfx.drawLine(x, y, x + s / 2, y - 2, rgb(120, 170, 220));
                }
                break;
            case SWAMP:
                for (int k = 0; k < 3; k++) {
                    const int x = cx + (k - 1) * s * 2, y = cy + (k == 1 ? -s / 2 : s / 2);
                    gfx.drawLine(x, y + s, x - s / 2, y, rgb(150, 170, 110));
                    gfx.drawLine(x, y + s, x, y - 1, rgb(150, 170, 110));
                    gfx.drawLine(x, y + s, x + s / 2, y, rgb(150, 170, 110));
                    gfx.drawFastHLine(x - s, y + s + 1, 2 * s, rgb(60, 90, 110));
                }
                break;
            case DESERT:
                for (int k = 0; k < 2; k++) {
                    const int y = cy + (k ? s : -s / 2);
                    gfx.drawArc(cx + (k ? -s : s / 2), y + s, s + 2, s, 200, 340, rgb(180, 146, 84));
                }
                break;
            case TUNDRA:
                for (int k = 0; k < 4; k++) {
                    const int x = cx + (int)((salt >> (k * 4)) % 9) - 4 + (k % 2 ? s : -s), y = cy + (k / 2 ? s : -s / 2);
                    gfx.drawFastHLine(x - 2, y, 5, rgb(150, 170, 186));
                }
                break;
            case PLAINS:
                for (int k = 0; k < 3; k++) {
                    const int x = cx + (k - 1) * s * 2 + (int)((salt >> (k * 3)) % 3) - 1, y = cy + (k == 1 ? s / 2 : -s / 3);
                    gfx.drawLine(x, y, x - 2, y - 3, rgb(84, 120, 50));
                    gfx.drawLine(x, y, x + 2, y - 3, rgb(84, 120, 50));
                }
                break;
            default: break;
        }
    }
    static void featureIcon(uint8_t k, int cx, int cy, int r) {
        const int s = std::max(4, r / 3);
        const uint16_t ink = rgb(30, 22, 18);
        switch (k) {
            case K_TOWN:
                for (int q = -1; q <= 1; q++) {
                    const int x = cx + q * (s + 1), y = cy + (q ? 2 : -2);
                    gfx.fillRect(x - s / 2, y - s / 2, s, s, rgb(236, 220, 186));
                    gfx.fillTriangle(x - s / 2 - 1, y - s / 2, x + s / 2 + 1, y - s / 2, x, y - s, C_BLOOD);
                    gfx.drawRect(x - s / 2, y - s / 2, s, s, ink);
                }
                break;
            case K_VILLAGE:
                gfx.fillRect(cx - s / 2, cy - s / 3, s, s * 2 / 3 + 1, rgb(236, 220, 186));
                gfx.fillTriangle(cx - s / 2 - 2, cy - s / 3, cx + s / 2 + 2, cy - s / 3, cx, cy - s, C_BLOOD);
                gfx.drawRect(cx - s / 2, cy - s / 3, s, s * 2 / 3 + 1, ink);
                break;
            case K_RUIN:
                gfx.fillRect(cx - s, cy - s / 2, 3, s + 2, rgb(200, 190, 170));
                gfx.fillRect(cx - 1, cy - s, 3, s * 3 / 2 + 2, rgb(200, 190, 170));
                gfx.fillRect(cx + s - 2, cy, 3, s / 2 + 2, rgb(200, 190, 170));
                gfx.drawFastHLine(cx - s - 1, cy + s / 2 + 2, 2 * s + 3, ink);
                break;
            case K_DUNGEON:
                gfx.fillCircle(cx, cy, s, rgb(60, 52, 48));
                gfx.fillRect(cx - s, cy, 2 * s + 1, s, rgb(60, 52, 48));
                gfx.fillCircle(cx, cy + 1, s - 3, rgb(8, 6, 8));
                gfx.fillRect(cx - s + 3, cy + 1, 2 * s - 5, s - 1, rgb(8, 6, 8));
                break;
            case K_LAIR:
                gfx.fillCircle(cx, cy - 1, s - 1, rgb(232, 226, 210));
                gfx.fillRect(cx - s / 2, cy + s / 2 - 2, s, s / 2 + 1, rgb(232, 226, 210));
                gfx.fillCircle(cx - s / 3, cy - 1, 2, C_BLOOD);
                gfx.fillCircle(cx + s / 3, cy - 1, 2, C_BLOOD);
                break;
            case K_CAMP:
                gfx.fillTriangle(cx - s, cy + s / 2, cx + s, cy + s / 2, cx, cy - s, rgb(200, 150, 80));
                gfx.drawLine(cx, cy - s, cx, cy + s / 2, ink);
                gfx.fillCircle(cx + s, cy + s / 2, 2, C_GOLD);
                break;
            case K_TOWER:
                gfx.fillRect(cx - s / 3, cy - s, s * 2 / 3 + 1, 2 * s, rgb(170, 160, 186));
                gfx.fillTriangle(cx - s / 2, cy - s, cx + s / 2, cy - s, cx, cy - s - s / 2 - 2, C_ARCANE);
                gfx.drawRect(cx - s / 3, cy - s, s * 2 / 3 + 1, 2 * s, ink);
                break;
            case K_SHRINE:
                gfx.fillTriangle(cx - s, cy - s / 3, cx + s, cy - s / 3, cx, cy - s, C_GOLD);
                gfx.fillRect(cx - s + 2, cy - s / 3, 2, s, rgb(236, 230, 210));
                gfx.fillRect(cx + s - 3, cy - s / 3, 2, s, rgb(236, 230, 210));
                gfx.drawFastHLine(cx - s, cy + s * 2 / 3, 2 * s, rgb(236, 230, 210));
                break;
            case K_LANDMARK:
                gfx.fillRoundRect(cx - s / 2, cy - s, s, 2 * s, 3, rgb(150, 146, 156));
                gfx.drawRoundRect(cx - s / 2, cy - s, s, 2 * s, 3, ink);
                break;
            case K_WONDER:
                gfx.drawLine(cx - s, cy, cx + s, cy, C_FIREFLY);
                gfx.drawLine(cx, cy - s, cx, cy + s, C_FIREFLY);
                gfx.drawLine(cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2, C_FIREFLY);
                gfx.drawLine(cx - s / 2, cy + s / 2, cx + s / 2, cy - s / 2, C_FIREFLY);
                gfx.fillCircle(cx, cy, 2, rgb(255, 255, 230));
                break;
            case K_RESOURCE:
                gfx.fillTriangle(cx - s / 2, cy - s / 3, cx + s / 2, cy - s / 3, cx, cy + s / 2 + 2, rgb(110, 210, 200));
                gfx.fillTriangle(cx - s / 2, cy - s / 3, cx + s / 2, cy - s / 3, cx, cy - s, rgb(170, 240, 230));
                break;
            case K_WATER:
                gfx.fillTriangle(cx - s, cy, cx + s, cy, cx + s / 2, cy + s / 2, rgb(120, 80, 50));
                gfx.fillRect(cx - s, cy - 1, 2 * s, 2, rgb(120, 80, 50));
                gfx.drawLine(cx, cy - s, cx, cy, ink);
                gfx.fillTriangle(cx + 1, cy - s, cx + 1, cy - 3, cx + s / 2 + 1, cy - 3, rgb(236, 230, 210));
                break;
            case K_NOTE:
                gfx.fillRect(cx - s / 2, cy - s / 2, s, s, rgb(236, 220, 150));
                gfx.drawRect(cx - s / 2, cy - s / 2, s, s, ink);
                break;
            default: break;
        }
    }

    struct View { int ox, oy, r; };
    static View fit(int x, int y, int w, int h) {
        const int r = std::max(8, (int)std::min(w / (SQ3 * (MW + 0.5f)), h / (1.5f * MH + 0.5f)));
        const int mw = (int)(r * SQ3 * (MW + 0.5f)), mh = (int)(r * (1.5f * MH + 0.5f));
        return {x + (w - mw) / 2, y + (h - mh) / 2, r};
    }
    static void center(const View& v, int i, int& x, int& y) {
        const int c = i % MW, r = i / MW;
        x = v.ox + (int)(v.r * SQ3 * (c + 0.5f + 0.5f * (r & 1)));
        y = v.oy + (int)(v.r * (1.0f + 1.5f * r));
    }
    static int hexAt(const View& v, int px, int py) {
        int best = -1, bestD = 1 << 30;
        for (int i = 0; i < N; i++) {
            int x, y;
            center(v, i, x, y);
            const int d = (x - px) * (x - px) + (y - py) * (y - py);
            if (d < bestD) { bestD = d; best = i; }
        }
        return bestD <= v.r * v.r ? best : -1;
    }
    // Point on hex i's edge toward direction d (or the centre of the next hex).
    static void edgePoint(const View& v, int i, int d, int& x, int& y) {
        int cx, cy;
        center(v, i, cx, cy);
        const float a = -60.0f * d * DEG_TO_RAD;
        x = cx + (int)lroundf(v.r * SQ3 / 2 * cosf(a));
        y = cy + (int)lroundf(v.r * SQ3 / 2 * sinf(a));
    }

    // The whole map. players: only what the party has seen, with fog.
    void drawHexes(const View& v, bool players) {
        const uint16_t fog = rgb(18, 20, 30);
        for (int i = 0; i < N; i++) {
            int x, y;
            center(v, i, x, y);
            const int seen = m_.seen[i];
            if (players && !seen) { fillHex(x, y, v.r, fog); outlineHex(x, y, v.r, rgb(34, 38, 54)); continue; }
            const uint16_t col = players && seen == 1 ? shade(TER_COL[m_.ter[i]], 60) : TER_COL[m_.ter[i]];
            fillHex(x, y, v.r, col);
            if (v.r >= 14 && !(players && seen == 1)) glyph(m_.ter[i], x, y, v.r, m_.seed ^ (i * 2654435761u));
        }
        // grid
        for (int i = 0; i < N; i++) {
            int x, y;
            center(v, i, x, y);
            outlineHex(x, y, v.r, players && !m_.seen[i] ? rgb(34, 38, 54) : rgb(30, 36, 40));
        }
        const uint16_t riverCol = rgb(70, 140, 220), roadCol = rgb(150, 100, 60);
        const int rw = std::max(2, v.r / 7);
        for (int i = 0; i < N; i++) {
            if (players && !m_.seen[i]) continue;
            int x, y, ex, ey;
            center(v, i, x, y);
            // rivers: from the centre out to the edge it flows over, and in from each hex flowing here
            if (m_.river[i] != 255 && m_.ter[i] != WATER) {
                edgePoint(v, i, m_.river[i], ex, ey);
                gfx.drawWideLine(x, y, ex, ey, rw, riverCol);
            }
            for (int d = 0; d < 6; d++) {
                const int n2 = nb(i, d);
                if (n2 >= 0 && m_.river[n2] == (d + 3) % 6) {
                    edgePoint(v, i, d, ex, ey);
                    gfx.drawWideLine(x, y, ex, ey, rw, riverCol);
                }
            }
        }
        for (int i = 0; i < N; i++) {
            if (!m_.road[i] || (players && !m_.seen[i])) continue;
            int x, y, ex, ey;
            center(v, i, x, y);
            for (int d = 0; d < 6; d++)
                if (m_.road[i] & (1 << d)) {
                    edgePoint(v, i, d, ex, ey);
                    // dashed: a few short pieces
                    for (int k = 0; k < 4; k += 2) {
                        const int x1 = x + (ex - x) * k / 4, y1 = y + (ey - y) * k / 4, x2 = x + (ex - x) * (k + 1) / 4, y2 = y + (ey - y) * (k + 1) / 4;
                        gfx.drawWideLine(x1, y1, x2, y2, std::max(1, rw - 1), roadCol);
                    }
                }
        }
        for (int i = 0; i < N; i++) {
            const Feature& f = m_.feat[i];
            if (f.kind == K_NONE || (players && (!f.found || f.kind == K_NOTE))) continue;
            int x, y;
            center(v, i, x, y);
            featureIcon(f.kind, x, y + (v.r >= 20 ? 2 : 0), v.r);
        }
        if (m_.pos >= 0) {
            int x, y;
            center(v, m_.pos, x, y);
            const int pr = std::max(5, v.r / 3);
            const int pulse = (millis() / 300) % 2;
            gfx.fillCircle(x, y - v.r / 3, pr + pulse, C_TEXT);
            gfx.fillCircle(x, y - v.r / 3, pr - 1 + pulse, C_BLOOD);
        }
    }

    static constexpr int SIDE_W = 330;
    View mapView() const { return fit(8, HEADER_H + 6, W - SIDE_W - 24, H - HEADER_H - FOOTER_H - 12); }

    void drawMapScreen() {
        const Metrics& mt = metrics();
        drawHeader(m_.name.c_str(), ACCENT);
        const View v = mapView();
        drawHexes(v, false);
        if (eventHex_ >= 0) { int x, y; center(v, eventHex_, x, y); gfx.drawCircle(x, y, v.r - 3, C_BLOOD); gfx.drawCircle(x, y, v.r - 4, C_BLOOD); }
        if (sel_ >= 0) {
            int x, y;
            center(v, sel_, x, y);
            for (int k = 0; k < 3; k++) outlineHex(x, y, v.r - k, C_GOLD);
        }
        const int mapW = (int)(v.r * SQ3 * (MW + 0.5f)), mapH = (int)(v.r * (1.5f * MH + 0.5f));
        touch::zone(v.ox, v.oy, mapW, mapH, [this, v] {
            const int i = hexAt(v, touch::tapX(), touch::tapY());
            if (i >= 0) select(i == sel_ ? -1 : i);
        });
        touch::onHold(v.ox, v.oy, mapW, mapH, [this, v] {
            const int i = hexAt(v, touch::tapX(), touch::tapY());
            if (i >= 0) holdHex(i);
        });
        const int sx = W - SIDE_W - 8, sy = HEADER_H + 6, sh = H - HEADER_H - FOOTER_H - 12;
        drawPanel(sx, sy, SIDE_W, sh, C_BG2, C_EDGE);
        if (sel_ >= 0) drawHexPanel(sx, sy, SIDE_W, sh);
        else drawSummary(sx, sy, SIDE_W, sh);
        (void)mt;
        drawFooter(sel_ >= 0 ? "Arrows pick  Ent move party  N note  R re-roll  E encounter  P players  Esc"
                             : "Tap: pick hex  Hold: more  Arrows move  P players' view  Esc back");
    }

    void drawSummary(int x, int y, int w, int h) {
        using namespace widgets;
        const Metrics& mt = metrics();
        gfx.setTextDatum(top_left);
        gfx.setFont(mt.bold);
        gfx.setTextColor(ACCENT);
        gfx.drawString(String(CLIMATES[m_.climate]) + " region", x + 16, y + 14);
        gfx.setFont(mt.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString("24 x 16 hexes of 6 miles", x + 16, y + 46);
        int cy = y + 78;
        gfx.setFont(mt.title);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(String("Day ") + (m_.hours / 8 + 1), x + 16, cy);
        gfx.setFont(mt.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(m_.hours % 8) + " of 8 hours travelled", x + 16, cy + 44);
        cy += 80;
        stepper(x + 12, cy, w - 24, 52, "Party", String(m_.party), [this](int d) { m_.party = std::max(1, std::min(10, m_.party + d)); saveMap(m_); }, 0, nullptr, 110);
        cy += 60;
        stepper(x + 12, cy, w - 24, 52, "Level", String(m_.level), [this](int d) { m_.level = std::max(1, std::min(20, m_.level + d)); saveMap(m_); }, 0, nullptr, 110);
        cy += 66;
        const int bw = (w - 36) / 2;
        button(x + 12, cy, bw, 52, "Players", false, ACCENT, [this] { screen_ = PLAYERS; sfx::select(); });
        button(x + 24 + bw, cy, bw, 52, "Make camp", false, ACCENT, [this] {
            m_.hours = (m_.hours / 8 + 1) * 8;
            event_ = String("Camped. Day ") + (m_.hours / 8 + 1) + " begins.";
            eventHex_ = -1;
            saveMap(m_);
            sfx::select();
        });
        cy += 62;
        button(x + 12, cy, w - 24, 52, "More...", false, C_DIM, [this] {
            holdmenu::open(m_.name, {{"Rename", [this] { ask("Region name", m_.name, 40, [this](const String& v) { if (v.length()) { m_.name = v; saveMap(m_); } }); }},
                                     {"Reset fog of war", [this] { memset(m_.seen, 0, N); for (auto& f : m_.feat) f.found = false; if (m_.pos >= 0) reveal(m_, m_.pos); saveMap(m_); }},
                                     {"Re-roll the whole map", [this] { build(esp_random() | 1); saveMap(m_); }, C_BLOOD, true}});
        });
        cy += 66;
        if (event_.length()) {
            gfx.setFont(mt.small);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(eventHex_ >= 0 ? C_BLOOD : C_GOLD);
            std::vector<String> lines;
            wrapLines(event_, w - 32, lines);
            for (size_t k = 0; k < lines.size() && cy + 26 < y + h; k++, cy += 26) gfx.drawString(lines[k], x + 16, cy);
            if (eventHex_ >= 0 && cy + 52 < y + h)
                button(x + 12, cy + 4, w - 24, 48, "Fight!", false, C_BLOOD, [this] {
                    const int c = event_.indexOf(": ");
                    fight(c > 0 ? event_.substring(c + 2) : event_);
                });
        }
    }

    // One hex: its terrain, the seven sub-hexes, what's here, and actions.
    void drawHexPanel(int x, int y, int w, int h) {
        using namespace widgets;
        const Metrics& mt = metrics();
        const int i = sel_;
        const uint8_t t = m_.ter[i];
        gfx.setTextDatum(top_left);
        gfx.setFont(mt.bold);
        gfx.setTextColor(ACCENT);
        gfx.drawString(fitText(String("Hex ") + hexLabel(i) + "  " + TER_NAME[t], w - 84), x + 16, y + 12);
        button(x + w - 56, y + 8, 44, 40, "x", false, C_DIM, [this] { sel_ = -1; sfx::back(); });
        gfx.setTextDatum(top_left);
        gfx.setFont(mt.small);
        gfx.setTextColor(C_DIM);
        String travel = String(travelHours(m_, i)) + " h to cross";
        if (m_.road[i]) travel += ", road";
        if (m_.river[i] != 255 && t != WATER) travel += ", river";
        gfx.drawString(fitText(travel, w - 32), x + 16, y + 44);
        gfx.drawString(String("Navigate ") + NAVIGATE[t] + "  Forage " + FORAGE[t] + " (DC)", x + 16, y + 68);
        // the hex template: seven sub-hexes leaning toward the neighbours
        drawTemplate(i, x + 16 + 62, y + 160, 26);
        int cy = y + 98;
        const Feature& f = m_.feat[i];
        const int tx = x + 160, tw = w - 172;
        gfx.setFont(mt.small);
        gfx.setTextColor(m_.pos == i ? C_BLOOD : C_DIM);
        gfx.drawString(m_.pos == i ? "Party is here" : m_.pos >= 0 ? String(hexDist(m_.pos, i)) + (hexDist(m_.pos, i) == 1 ? " hex away" : " hexes away") : String(""), tx, cy);
        gfx.setTextColor(m_.seen[i] == 2 ? C_EMERALD : m_.seen[i] ? C_GOLD : C_DIM);
        gfx.drawString(m_.seen[i] == 2 ? "Visited" : m_.seen[i] ? "Seen" : "Unexplored", tx, cy + 26);
        if (f.kind != K_NONE && f.kind != K_NOTE) {
            gfx.setTextColor(f.found ? C_EMERALD : C_DIM);
            gfx.drawString(f.found ? "Found" : "Not found", tx, cy + 52);
            touch::zone(tx - 4, cy + 48, tw, 30, [this, i] { m_.feat[i].found = !m_.feat[i].found; saveMap(m_); sfx::click(); });
        }
        cy = y + 236;
        // what's here (scrolls)
        const int bottomBtns = y + h - 2 * 58 - 8;
        std::vector<String> lines;
        std::vector<uint16_t> cols;
        auto add = [&](const String& s, uint16_t c) {
            std::vector<String> l;
            wrapLines(s, w - 32, l);
            for (auto& x2 : l) { lines.push_back(x2); cols.push_back(c); }
        };
        gfx.setFont(mt.small);
        gfx.setTextDatum(top_left);
        if (f.kind != K_NONE && f.kind != K_NOTE) {
            add(String(KIND_NAME[f.kind]).c_str(), C_GOLD);
            add(f.name, C_TEXT);
            int a = 0;
            while (a <= (int)f.text.length()) {
                int e = f.text.indexOf('\n', a);
                if (e < 0) e = f.text.length();
                if (e > a) add(f.text.substring(a, e), C_DIM);
                a = e + 1;
            }
        } else add("Nothing here yet.", C_DIM);
        if (f.notes.length()) add(String("Note: ") + f.notes, C_ARCANE);
        const int lh = 26, areaH = bottomBtns - cy - 6;
        const float maxPos = std::max(0, (int)lines.size() * lh - areaH);
        textPos_ = std::max(0.0f, std::min(textPos_, maxPos + 40));
        touch::scroller(x, cy, w, areaH, &textPos_, maxPos);
        gfx.setClipRect(x + 8, cy, w - 16, areaH);
        for (size_t k = 0; k < lines.size(); k++) {
            const int ly = cy + k * lh - (int)textPos_;
            if (ly + lh < cy || ly > cy + areaH) continue;
            gfx.setTextColor(cols[k]);
            gfx.drawString(lines[k], x + 16, ly);
        }
        gfx.clearClipRect();
        // actions
        const int bw = (w - 36) / 2, by = bottomBtns;
        const bool next = m_.pos >= 0 && hexDist(m_.pos, i) == 1;
        button(x + 12, by, bw, 50, next ? "Travel" : "Jump party", false, ACCENT, [this, i, next] { travelTo(i, !next); });
        button(x + 24 + bw, by, bw, 50, "Encounter", false, C_BLOOD, [this, i] { encounterAt(i); });
        const String who = monstersIn(f.text);
        if (who.length()) button(x + 12, by + 58, bw, 50, "Fight", false, C_BLOOD, [this, who] { fight(who); });
        else button(x + 12, by + 58, bw, 50, "Note", false, C_ARCANE, [this, i] { editNote(i); });
        button(x + 24 + bw, by + 58, bw, 50, "More...", false, C_DIM, [this, i] { holdHex(i); });
    }

    // Seven sub-hexes: the middle one is the hex's own terrain; each outer one
    // keeps it on 1-4 on a d6, else takes the terrain next door that way.
    uint8_t subTerrain(int i, int k) const {
        if (k == 6) return m_.ter[i];
        Lcg g{m_.seed ^ (uint32_t)(i * 2654435761u) ^ (uint32_t)(k * 97)};
        g.next();
        const int n2 = nb(i, k);
        if (n2 < 0 || g.below(6) < 4) return m_.ter[i];
        return m_.ter[n2];
    }
    void drawTemplate(int i, int cx, int cy, int r) {
        View v{0, 0, r};
        int pts[7][2];
        for (int k = 0; k < 6; k++) {
            const float a = -60.0f * k * DEG_TO_RAD;
            pts[k][0] = cx + (int)lroundf(r * SQ3 * cosf(a));
            pts[k][1] = cy + (int)lroundf(r * SQ3 * sinf(a));
        }
        pts[6][0] = cx;
        pts[6][1] = cy;
        for (int k = 0; k < 7; k++) {
            const uint8_t t = subTerrain(i, k);
            fillHex(pts[k][0], pts[k][1], r, TER_COL[t]);
            glyph(t, pts[k][0], pts[k][1], r, k * 77 + i);
            outlineHex(pts[k][0], pts[k][1], r, rgb(30, 36, 40));
        }
        // rivers and roads through it
        const int rw = std::max(2, r / 6);
        for (int d = 0; d < 6; d++) {
            const int n2 = nb(i, d);
            const bool riv = (m_.river[i] == d && m_.ter[i] != WATER) || (n2 >= 0 && m_.river[n2] == (d + 3) % 6);
            if (riv) gfx.drawWideLine(cx, cy, pts[d][0] + (pts[d][0] - cx) / 2, pts[d][1] + (pts[d][1] - cy) / 2, rw, rgb(70, 140, 220));
            if (m_.road[i] & (1 << d)) gfx.drawWideLine(cx, cy, pts[d][0] + (pts[d][0] - cx) / 2, pts[d][1] + (pts[d][1] - cy) / 2, std::max(1, rw - 1), rgb(150, 100, 60));
        }
        outlineHex(cx, cy, (int)(r * 2.6f), C_EDGE);
        if (m_.feat[i].kind != K_NONE) featureIcon(m_.feat[i].kind, cx, cy, r);
        (void)v;
    }

    void holdHex(int i) {
        select(i);
        std::vector<holdmenu::Item> items;
        const bool next = m_.pos >= 0 && hexDist(m_.pos, i) == 1;
        if (next) items.push_back({"Travel here", [this, i] { travelTo(i); }});
        items.push_back({"Jump the party here", [this, i] { travelTo(i, true); }});
        items.push_back({"Roll an encounter here", [this, i] { encounterAt(i); }});
        items.push_back({m_.feat[i].notes.length() ? "Edit the note" : "Add a note", [this, i] { editNote(i); }});
        if (m_.feat[i].kind != K_NONE && m_.feat[i].kind != K_NOTE) {
            items.push_back({"Re-roll what's here", [this, i] { reroll(i); }});
            items.push_back({"Clear this hex", [this, i] { const String n = m_.feat[i].notes; m_.feat[i] = Feature(); m_.feat[i].notes = n; if (n.length()) m_.feat[i].kind = K_NOTE; saveMap(m_); }, C_BLOOD, true});
        }
        items.push_back({"Put something here...", [this, i] {
            std::vector<holdmenu::Item> kinds;
            for (uint8_t k : {K_VILLAGE, K_RUIN, K_DUNGEON, K_LAIR, K_CAMP, K_TOWER, K_SHRINE, K_LANDMARK, K_WONDER})
                kinds.push_back({KIND_NAME[k], [this, i, k] { rollFeature(i, k); saveMap(m_); sfx::select(); }});
            holdmenu::open(String("Hex ") + hexLabel(i), std::move(kinds));
        }});
        holdmenu::open(String("Hex ") + hexLabel(i) + " - " + TER_NAME[m_.ter[i]], std::move(items));
    }

    void drawPlayers() {
        const Metrics& mt = metrics();
        drawHeader((m_.name + " - Day " + (m_.hours / 8 + 1)).c_str(), ACCENT);
        const View v = fit(8, HEADER_H + 6, W - 16, H - HEADER_H - FOOTER_H - 12);
        drawHexes(v, true);
        (void)mt;
        drawFooter("Esc back to the DM's map");
    }

    // ================================================================ web
    String mapJson() {
        auto grid = [](const uint8_t* a, char (*f)(uint8_t)) { return web::js(gridString(a, f)); };
        String o = String("{\"id\":") + web::js(m_.id) + ",\"name\":" + web::js(m_.name) + ",\"climate\":" + m_.climate +
                   ",\"w\":" + MW + ",\"h\":" + MH + ",\"party\":" + m_.party + ",\"level\":" + m_.level + ",\"pos\":" + m_.pos +
                   ",\"hours\":" + m_.hours +
                   ",\"ter\":" + grid(m_.ter, [](uint8_t v) { return v < NTER ? TER_CH[v] : 'p'; }) +
                   ",\"river\":" + grid(m_.river, [](uint8_t v) { return v < 6 ? (char)('0' + v) : '-'; }) +
                   ",\"road\":" + grid(m_.road, [](uint8_t v) { return (char)('0' + (v & 63)); }) +
                   ",\"seen\":" + grid(m_.seen, [](uint8_t v) { return (char)('0' + std::min<uint8_t>(v, 2)); }) + ",\"sub\":";
        String sub;
        sub.reserve(N * 7);
        for (int i = 0; i < N; i++) for (int k = 0; k < 7; k++) sub += TER_CH[subTerrain(i, k)];
        o += web::js(sub) + ",\"event\":" + web::js(event_) + ",\"eventHex\":" + eventHex_ + ",\"hexes\":[";
        bool first = true;
        for (int i = 0; i < N; i++) {
            const Feature& f = m_.feat[i];
            if (f.kind == K_NONE && !f.notes.length()) continue;
            o += String(first ? "" : ",") + "{\"i\":" + i + ",\"kind\":" + web::js(KIND_KEY[f.kind]) + ",\"label\":" + web::js(KIND_NAME[f.kind]) +
                 ",\"name\":" + web::js(f.name) + ",\"text\":" + web::js(f.text) + ",\"notes\":" + web::js(f.notes) +
                 ",\"found\":" + (f.found ? "true" : "false") + ",\"monsters\":" + web::js(monstersIn(f.text)) + "}";
            first = false;
        }
        return o + "]}";
    }
};

int HexMapApp::web(const WebCall& c, String& out) {
    if (c.op == "hello") return 200;
    if (!storage::ok()) { out = "No SD card"; return 503; }
    if (c.op == "list") {
        loadList();
        out = "[";
        for (size_t i = 0; i < list_.size(); i++)
            out += String(i ? "," : "") + "{\"id\":" + web::js(list_[i].id) + ",\"name\":" + web::js(list_[i].name) +
                   ",\"climate\":" + web::js(CLIMATES[list_[i].climate]) + "}";
        out += "]";
        return 200;
    }
    const String id = c.arg("id");
    if (c.op != "new" && (!id.length() || id.indexOf('/') >= 0 || id.indexOf("..") >= 0)) { out = "No such map"; return 404; }
    const bool deviceSame = screen_ != LIST && m_.id == id;
    if (c.op == "delete") {
        if (deviceSame) { out = "It's open on the device"; return 409; }
        storage::remove((String(ROOT) + "/" + id + ".txt").c_str());
        out = "{}";
        return 200;
    }
    // the page's map is loaded into m_ for the call; the device's is put back after
    Map keep = m_;
    const String keepEvent = event_;
    const int keepEventHex = eventHex_, keepSel = sel_;
    int code = 200;
    if (c.op == "new") {
        const String nid = createMap(c.arg("name"), std::max(0, std::min(2, c.num("climate", 0))));
        if (!nid.length()) { out = "Couldn't make it"; code = 500; }
        else out = mapJson();
    } else if (deviceSame) {
        m_ = keep;   // the device's copy is the latest
    } else if (!loadMap(id, m_)) { out = "No such map"; code = 404; }
    if (code == 200 && c.op != "new") {
        if (!deviceSame) { event_ = ""; eventHex_ = -1; }
        const int i = c.num("i", -1);
        const bool hexOk = i >= 0 && i < N;
        if (c.op == "map") {}
        else if (c.op == "move" && hexOk) travelTo(i, c.arg("jump") == "1");
        else if (c.op == "encounter" && hexOk) {
            const int d = rng::below(100);
            const String who = monsters(HABITAT[m_.ter[i]], d < 50 ? 0 : d < 85 ? 1 : 2);
            event_ = who.length() ? String("Encounter in ") + hexLabel(i) + ": " + who : String("No monsters fit");
            eventHex_ = who.length() ? i : -1;
        } else if (c.op == "reroll" && hexOk) reroll(i);
        else if (c.op == "put" && hexOk) {
            const String k = c.arg("kind");
            for (int x = K_TOWN; x < K_NOTE; x++) if (k == KIND_KEY[x]) rollFeature(i, x);
            saveMap(m_);
        } else if (c.op == "clear" && hexOk) {
            const String n = m_.feat[i].notes;
            m_.feat[i] = Feature();
            m_.feat[i].notes = n;
            if (n.length()) m_.feat[i].kind = K_NOTE;
            saveMap(m_);
        } else if (c.op == "set") {
            const String k = c.arg("key");
            String v = c.arg("value");
            v.trim();
            if (hexOk) {
                Feature& f = m_.feat[i];
                if (k == "notes") { f.notes = v; if (f.kind == K_NONE && v.length()) f.kind = K_NOTE; if (f.kind == K_NOTE && !v.length()) f.kind = K_NONE; }
                else if (k == "name") f.name = v;
                else if (k == "text") f.text = v;
                else if (k == "found") f.found = v == "1";
            } else if (k == "name" && v.length()) m_.name = v;
            else if (k == "party") m_.party = std::max(1, std::min(10, (int)v.toInt()));
            else if (k == "level") m_.level = std::max(1, std::min(20, (int)v.toInt()));
            saveMap(m_);
        } else if (c.op == "camp") {
            m_.hours = (m_.hours / 8 + 1) * 8;
            event_ = String("Camped. Day ") + (m_.hours / 8 + 1) + " begins.";
            saveMap(m_);
        } else if (c.op == "fog") {
            memset(m_.seen, 0, N);
            for (auto& f : m_.feat) f.found = false;
            if (m_.pos >= 0) reveal(m_, m_.pos);
            saveMap(m_);
        } else if (c.op == "regen") { build(esp_random() | 1); saveMap(m_); }
        else { out = "Unknown"; code = 404; }
        if (code == 200) out = mapJson();
    }
    if (deviceSame) {
        // the device shows what the page did (and what happened on the road)
        sel_ = keepSel;
    } else {
        m_ = keep;
        event_ = keepEvent;
        eventHex_ = keepEventHex;
        sel_ = keepSel;
    }
    if (!appOpen(this)) { gen_.close(); releaseRandomTables(); }
    return code;
}

}  // namespace

App* makeHexMapApp() { return new HexMapApp(); }
#endif

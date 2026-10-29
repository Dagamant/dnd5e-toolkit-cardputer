#include "touch.h"
#include "ui.h"
#include <vector>
#include <math.h>

namespace touch {

struct Zone {
    int16_t x, y, w, h;
    std::function<void()> fn;
    bool repeat = false;
    std::function<void()> held;   // long press; such a zone ignores taps
};
static std::vector<Zone> s_zones;
static int s_x = 0, s_y = 0;

int tapX() { return s_x; }
int tapY() { return s_y; }

static bool s_frozen = false;

void freeze(bool on) { s_frozen = on; }
bool frozen() { return s_frozen; }

struct Scroller {
    int16_t x, y, w, h;
    float* pos;
    float max;
};
static std::vector<Scroller> s_scrollers;
static float* s_active = nullptr;   // the scroller being dragged or coasting
static float s_vel = 0;             // px/ms, + = further down
static bool s_down = false;
static uint32_t s_lastDrag = 0;

void clear() {
    if (s_frozen) return;
    s_zones.clear();
    s_scrollers.clear();
}

void scroller(int x, int y, int w, int h, float* pos, float max) {
    if (s_frozen || w <= 0 || h <= 0 || !ui::wide()) return;
    s_scrollers.push_back({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, pos, std::max(0.0f, max)});
}

static const Scroller* findScroller(float* pos) {
    for (auto& s : s_scrollers) if (s.pos == pos) return &s;
    return nullptr;
}

void press() {
    s_down = true;
    s_vel = 0;   // a finger stops a coasting list
}

bool drag(int x0, int y0, int dy) {
    const Scroller* sc = nullptr;
    for (int i = (int)s_scrollers.size() - 1; i >= 0 && !sc; i--) {
        const Scroller& s = s_scrollers[i];
        if (x0 >= s.x && x0 < s.x + s.w && y0 >= s.y && y0 < s.y + s.h) sc = &s;
    }
    if (!sc) return false;
    s_active = sc->pos;
    float p = *sc->pos;
    // past the ends it stretches: the finger moves it less
    *sc->pos = p + (p < 0 || p > sc->max ? dy * 0.4f : dy);
    const uint32_t now = millis(), dt = now - s_lastDrag;
    const float v = (float)dy / std::max<uint32_t>(dt, 8);
    s_vel = dt < 100 ? s_vel * 0.6f + v * 0.4f : v;
    s_lastDrag = now;
    return true;
}

void release() {
    s_down = false;
    if (millis() - s_lastDrag > 80) s_vel = 0;   // the finger stopped before lifting
}

void update(uint32_t dt) {
    if (!s_active) return;
    const Scroller* sc = findScroller(s_active);
    if (!sc) { s_active = nullptr; return; }   // that screen is gone
    if (s_down) return;
    dt = std::min<uint32_t>(dt, 50);
    float& p = *s_active;
    p += s_vel * dt;
    s_vel *= powf(0.996f, (float)dt);
    // past an end: stop coasting and ease back
    const float edge = p < 0 ? 0 : p > sc->max ? sc->max : p;
    if (edge != p) {
        s_vel *= 0.5f;
        p += (edge - p) * std::min(1.0f, dt * 0.012f);
        if (fabsf(edge - p) < 0.5f) p = edge;
    }
    if (fabsf(s_vel) < 0.01f && p == edge) s_active = nullptr;
}

bool scrolling() { return s_active != nullptr; }

void zone(int x, int y, int w, int h, std::function<void()> fn) {
    if (s_frozen || w <= 0 || h <= 0 || !ui::wide()) return;   // only the big screen has touch
    s_zones.push_back({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, std::move(fn)});
}

void repeat(int x, int y, int w, int h, std::function<void()> fn) {
    zone(x, y, w, h, std::move(fn));
    if (!s_zones.empty() && s_zones.back().x == x && s_zones.back().y == y) s_zones.back().repeat = true;
}

void onHold(int x, int y, int w, int h, std::function<void()> fn) {
    if (s_frozen || w <= 0 || h <= 0 || !ui::wide()) return;
    s_zones.push_back({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, nullptr, false, std::move(fn)});
}

// The top target under (x, y); for a tap, long-press-only ones are skipped.
static const Zone* find(int x, int y, bool forTap = true) {
    for (int i = (int)s_zones.size() - 1; i >= 0; i--) {
        const Zone& z = s_zones[i];
        if (forTap && !z.fn) continue;
        if (x >= z.x && x < z.x + z.w && y >= z.y && y < z.y + z.h) return &z;
    }
    return nullptr;
}

bool hold(int x, int y, uint32_t heldMs) {
    constexpr uint32_t DELAY_MS = 500, EVERY_MS = 90;
    static uint32_t s_last = 0;
    static bool s_held = false;
    if (heldMs < DELAY_MS) { s_last = 0; s_held = false; return false; }
    if (s_held) return true;
    const Zone* z = find(x, y, false);
    if (z && z->held) {
        s_held = true;
        s_frozen = false;   // the menu it opens needs a full frame
        s_x = x;
        s_y = y;
        auto fn = z->held;
        fn();
        input::poke();
        return true;
    }
    if (!z || !z->repeat) return s_last != 0;
    uint32_t now = millis();
    if (s_last && now - s_last < EVERY_MS) return true;
    s_last = now;
    s_x = x;
    s_y = y;
    auto fn = z->fn;
    fn();
    input::poke();   // keeps the screen redrawing
    return true;
}

static void send(input::Key k, char ch = 0, bool fn = false) {
    input::Event e;
    e.key = k;
    e.ch = e.base = ch;
    e.fn = fn;
    input::inject(e);
}

void key(int x, int y, int w, int h, input::Key k, char ch, bool fn) {
    zone(x, y, w, h, [k, ch, fn] { send(k, ch, fn); });
}

void row(int x, int y, int w, int h, int& sel, int i) {
    int* p = &sel;
    zone(x, y, w, h, [p, i] { *p = i; send(input::K_ENTER); });
}

void pick(int x, int y, int w, int h, int& sel, int i) {
    int* p = &sel;
    zone(x, y, w, h, [p, i] {
        if (*p == i) send(input::K_ENTER);
        *p = i;
    });
}

void field(int x, int y, int w, int h, int& sel, int i) {
    int* p = &sel;
    zone(x, y, w, h, [p, i] {
        if (*p == i) send(input::K_RIGHT);
        *p = i;
    });
}

void modal(int x, int y, int w, int h) {
    // nothing beneath scrolls either
    s_scrollers.clear();
    zone(0, 0, 2000, 2000, [] { send(input::K_BACK); });
    zone(x, y, w, h, [] {});
}

bool tap(int x, int y) {
    s_frozen = false;
    s_x = x;
    s_y = y;
    const Zone* z = find(x, y);
    if (!z) return false;
    auto fn = z->fn;   // the callback may draw or clear the list
    fn();
    return true;
}

}  // namespace touch

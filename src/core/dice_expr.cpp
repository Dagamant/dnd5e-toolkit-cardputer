#include "dice_expr.h"
#include "rng.h"
#include <algorithm>

namespace dice {

static constexpr int MAX_DICE = 100;
static constexpr int MAX_SIDES = 1000;
static constexpr int MAX_EXPLODE = 50;

int Group::subtotal() const {
    if (sides == 0) return sign * constant;
    int s = 0;
    for (size_t i = 0; i < vals.size(); i++) if (kept[i]) s += vals[i];
    return sign * s;
}

int Result::largestDie() const {
    int m = 0;
    for (auto& g : groups) m = std::max(m, g.sides);
    return m;
}

String Result::breakdown() const {
    String s;
    for (size_t gi = 0; gi < groups.size(); gi++) {
        const Group& g = groups[gi];
        if (gi > 0 || g.sign < 0) s += g.sign < 0 ? " - " : " + ";
        if (g.sides == 0) { s += String(g.constant); continue; }
        s += "[";
        for (size_t i = 0; i < g.vals.size(); i++) {
            if (i) s += ",";
            if (!g.kept[i]) s += "~";
            s += String(g.vals[i]);
            if (!g.kept[i]) s += "~";
        }
        s += "]";
    }
    return s;
}

const char* degreeName(Degree d) {
    switch (d) {
        case Degree::CritFail: return "CRITICAL FAILURE";
        case Degree::Fail: return "FAILURE";
        case Degree::Success: return "SUCCESS";
        case Degree::CritSuccess: return "CRITICAL SUCCESS";
        default: return "";
    }
}

namespace {
struct Parser {
    const char* p;
    String err;

    void ws() { while (*p == ' ') p++; }
    bool lit(const char* s) {
        ws();
        size_t n = strlen(s);
        if (strncasecmp(p, s, n) == 0) { p += n; return true; }
        return false;
    }
    bool number(int& out) {
        ws();
        if (!isdigit((unsigned char)*p)) return false;
        long v = 0;
        while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); if (v > 100000) v = 100000; p++; }
        out = (int)v;
        return true;
    }

    // Rolls one die group with modifiers; fills g.
    bool dice(int count, Group& g) {
        int sides;
        if (*p == '%') { p++; sides = 100; }
        else if (!number(sides)) { err = "die size?"; return false; }
        if (count < 1 || count > MAX_DICE) { err = "1-100 dice"; return false; }
        if (sides < 2 || sides > MAX_SIDES) { err = "d2-d1000"; return false; }
        g.sides = sides;

        enum { NONE, KH, KL, DH, DL } keepMode = NONE;
        int keepN = 1, rerollBelow = 0;
        bool explode = false;
        for (;;) {
            if (lit("kh")) { keepMode = KH; if (!number(keepN)) keepN = 1; }
            else if (lit("kl")) { keepMode = KL; if (!number(keepN)) keepN = 1; }
            else if (lit("dh")) { keepMode = DH; if (!number(keepN)) keepN = 1; }
            else if (lit("dl")) { keepMode = DL; if (!number(keepN)) keepN = 1; }
            else if (lit("k")) { keepMode = KH; if (!number(keepN)) keepN = 1; }
            else if (lit("!")) { explode = true; }
            else if (lit("r")) { if (!number(rerollBelow)) rerollBelow = 1; }
            else break;
        }
        if (rerollBelow >= sides) rerollBelow = sides - 1;

        int explosions = 0;
        for (int i = 0; i < count; i++) {
            int v = rng::roll(sides);
            if (v <= rerollBelow) v = rng::roll(sides);
            g.vals.push_back(v);
            while (explode && v == sides && explosions < MAX_EXPLODE) {
                v = rng::roll(sides);
                g.vals.push_back(v);
                explosions++;
            }
        }
        g.kept.assign(g.vals.size(), true);

        if (keepMode != NONE) {
            int n = (int)g.vals.size();
            std::vector<int> idx(n);
            for (int i = 0; i < n; i++) idx[i] = i;
            // ascending by value; stable keeps leftmost first among ties
            std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return g.vals[a] < g.vals[b]; });
            int k = std::max(0, std::min(keepN, n));
            std::vector<bool> keep(n, false);
            for (int i = 0; i < n; i++) {
                int rank = i;  // 0 = lowest
                bool kept;
                switch (keepMode) {
                    case KH: kept = rank >= n - k; break;
                    case KL: kept = rank < k; break;
                    case DH: kept = rank < n - k; break;
                    default: kept = rank >= k; break;  // DL
                }
                keep[idx[i]] = kept;
            }
            g.kept = keep;
        }
        return true;
    }

    bool term(int sign, std::vector<Group>& out) {
        ws();
        Group g;
        g.sign = sign;
        if (lit("adv")) { g.sides = 20; g.vals = {rng::roll(20), rng::roll(20)};
            g.kept = {g.vals[0] >= g.vals[1], g.vals[0] < g.vals[1]}; out.push_back(g); return true; }
        if (lit("dis")) { g.sides = 20; g.vals = {rng::roll(20), rng::roll(20)};
            g.kept = {g.vals[0] <= g.vals[1], g.vals[0] > g.vals[1]}; out.push_back(g); return true; }
        int n = 1;
        bool hadNum = number(n);
        ws();
        if (*p == 'd' || *p == 'D') {
            p++;
            if (!dice(hadNum ? n : 1, g)) return false;
        } else if (hadNum) {
            g.sides = 0;
            g.constant = n;
        } else {
            err = *p ? String("unexpected '") + *p + "'" : String("incomplete");
            return false;
        }
        out.push_back(g);
        return true;
    }
};
}  // namespace

Result roll(const String& exprIn) {
    Result r;
    String e = exprIn;
    e.trim();
    if (e.length() == 0) { r.error = "empty"; return r; }
    r.expr = e;

    Parser ps{e.c_str()};
    int sign = 1;
    if (ps.lit("-")) sign = -1;
    if (!ps.term(sign, r.groups)) { r.error = ps.err; return r; }
    for (;;) {
        ps.ws();
        if (ps.lit("+")) { if (!ps.term(1, r.groups)) { r.error = ps.err; return r; } }
        else if (ps.lit("-")) { if (!ps.term(-1, r.groups)) { r.error = ps.err; return r; } }
        else break;
    }
    ps.ws();
    if (ps.lit("vs") || ps.lit("dc") || ps.lit("dr") || ps.lit(">=")) {
        if (!ps.number(r.target)) { r.error = "target?"; return r; }
        r.hasTarget = true;
    }
    ps.ws();
    if (*ps.p) { r.error = String("unexpected '") + *ps.p + "'"; return r; }

    int d20groups = 0, d20kept = 0, nat = 0;
    for (auto& g : r.groups) {
        r.total += g.subtotal();
        if (g.sides == 0) { r.minTotal += g.sign * g.constant; r.maxTotal += g.sign * g.constant; continue; }
        int keptCount = 0;
        for (size_t i = 0; i < g.vals.size(); i++) if (g.kept[i]) keptCount++;
        int lo = keptCount, hi = keptCount * g.sides;
        if (g.sign > 0) { r.minTotal += lo; r.maxTotal += hi; } else { r.minTotal -= hi; r.maxTotal -= lo; }
        if (g.sides == 20) {
            d20groups++;
            for (size_t i = 0; i < g.vals.size(); i++) if (g.kept[i]) { d20kept++; nat = g.vals[i]; }
        }
    }
    if (d20groups == 1 && d20kept == 1) r.natural = nat;

    if (r.hasTarget) {
        if (r.natural == 20) r.degree = Degree::CritSuccess;
        else if (r.natural == 1) r.degree = Degree::CritFail;
        else r.degree = r.total >= r.target ? Degree::Success : Degree::Fail;
    }
    r.ok = true;
    return r;
}

}  // namespace dice

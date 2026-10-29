#include "encounter_gen.h"
#include "rng.h"
#include <algorithm>

namespace encounter {

const char* const HABITATS[] = {"Any", "Arctic", "Coastal", "Desert", "Dungeon", "Forest", "Grassland",
                                "Hill", "Mountain", "Planar", "Swamp", "Underdark", "Underwater", "Urban"};
const char* const TYPES[] = {"Any", "Aberration", "Beast", "Celestial", "Construct", "Dragon", "Elemental", "Fey",
                             "Fiend", "Giant", "Humanoid", "Monstrosity", "Ooze", "Plant", "Undead"};
const char* const DIFF[] = {"Low", "Moderate", "High"};
const char* const GROUPS[] = {"Random", "Solo", "Leader + minions", "Horde", "Mixed"};
const char* const GROUP_SHORT[] = {"", "Solo", "Leader", "Horde", "Mixed"};

static float crValue(const char* cr) {
    if (!strcmp(cr, "1/8")) return 0.125f;
    if (!strcmp(cr, "1/4")) return 0.25f;
    if (!strcmp(cr, "1/2")) return 0.5f;
    return atof(cr);
}
static bool typeMatch(const char* type, int t) { return t == 0 || strstr(type, TYPES[t]) != nullptr; }
static bool habitatMatch(const char* hab, int h) {
    if (!*hab || strstr(hab, "Any") || h == 0) return true;   // custom monsters have no habitat
    return strstr(hab, HABITATS[h]) != nullptr;
}

bool Generator::open() {
    if (table_.isOpen()) return true;
    return table_.open({{"/gmtools/srd/monsters", {"cr", "type", "xp", "hp", "ac", "init", "size", "habitat"}}},
                       "/gmtools/custom/monsters.txt", {"cr", "type", "xp", "hp", "ac", "init", "size", "habitat"});
}
void Generator::close() { table_.close(); cands_.clear(); }

void Generator::gather(int hab, int type) {
    cands_.clear();
    for (int i = 0; i < table_.size(); i++) {
        int xp = atoi(table_.col(i, "xp"));
        if (xp < 25) continue;   // CR 0 critters
        const char* h = table_.col(i, "habitat");
        if (!strcmp(h, "-") || !habitatMatch(h, hab) || !typeMatch(table_.col(i, "type"), type)) continue;
        cands_.push_back({i, xp, crValue(table_.col(i, "cr")), table_.col(i, "type")});
    }
}

// Random candidate with lo <= xp <= hi and cr <= maxCr; prefers sameType when any match.
const Generator::Cand* Generator::pick(int lo, int hi, float maxCr, const char* sameType) {
    std::vector<const Cand*> any, same;
    for (auto& c : cands_) {
        if (c.xp < lo || c.xp > hi || c.cr > maxCr) continue;
        any.push_back(&c);
        if (sameType && !strcmp(c.type, sameType)) same.push_back(&c);
    }
    auto& v = same.empty() ? any : same;
    return v.empty() ? nullptr : v[rng::below(v.size())];
}

void Generator::add(const Cand* c, int count) {
    if (!c || count <= 0) return;
    for (auto& m : *out_)
        if (m.name == table_.at(c->idx).name) { m.count += count; return; }
    db::Rec r;
    table_.read(c->idx, r);
    Entry en;
    en.name = table_.at(c->idx).name;
    en.count = count;
    en.xp = c->xp;
    en.hp = r.num("hp", 1);
    en.ac = r.num("ac", 10);
    en.init = r.get("init").toInt();
    en.hpdice = r.get("hpdice");
    en.cr = r.get("cr");
    out_->push_back(en);
}

int Generator::totalXp() const { int t = 0; for (auto& m : *out_) t += m.xp * m.count; return t; }
int Generator::bodies() const { int t = 0; for (auto& m : *out_) t += m.count; return t; }

// Fill the result for one group style using the current candidates.
void Generator::compose(int group) {
    const int B = budget_, lvl = o_.partyLevel;
    const int cap = std::max(4, std::min(12, o_.partySize * 3));
    out_->clear();
    switch (group) {
        case 1: {   // solo
            const Cand* c = pick(B / 2, B, 99);
            if (!c) {   // biggest thing that fits
                for (auto& k : cands_) if (k.xp <= B && (!c || k.xp > c->xp)) c = &k;
                if (c) note_ = "Nothing bigger fits - under budget";
            }
            add(c, 1);
            return;   // a solo stays solo
        }
        case 2: {   // leader + minions
            const Cand* l = pick(B * 3 / 10, B * 6 / 10, lvl + 3);
            if (!l) { compose(4); return; }
            add(l, 1);
            int rem = B - l->xp;
            const Cand* m = pick(rem / 8, rem / 2, l->cr - 0.01f, l->type);
            if (!m) m = pick(1, rem / 2, l->cr - 0.01f, l->type);
            if (m) add(m, std::max(1, std::min(cap - 1, rem / m->xp)));
            break;
        }
        case 3: {   // horde of one kind
            const Cand* m = pick(B / 12, B / 4, lvl + 1);
            if (!m) m = pick(1, B / 3, lvl + 1);
            if (m) add(m, std::max(1, std::min(cap, B / m->xp)));
            break;
        }
        default: {  // mixed: up to three kinds
            int rem = B;
            for (int k = 0; k < 3 && bodies() < cap; k++) {
                const char* like = out_->empty() ? nullptr : table_.col(table_.find((*out_)[0].name), "type");
                const Cand* c = pick(rem / 6, k == 0 ? rem / 2 : rem, lvl + 2, like);
                if (!c) break;
                int n = k == 0 ? 1 : 1 + rng::below(std::max(1, std::min(cap - bodies(), rem / c->xp)));
                add(c, n);
                rem -= c->xp * n;
            }
            break;
        }
    }
    // top up thin results with more of the cheapest creature
    if (!out_->empty()) {
        auto* low = &(*out_)[0];
        for (auto& m : *out_) if (m.xp < low->xp) low = &m;
        while (totalXp() + low->xp <= B && totalXp() < B * 6 / 10 && bodies() < cap) low->count++;
    }
}

bool Generator::generate(const GenOptions& o, GenResult& res) {
    if (!open()) return false;
    o_ = o;
    out_ = &res.monsters;
    budget_ = res.budget = budget(o.partyLevel, o.diff) * o.partySize;
    res.group = o.group ? o.group : 1 + rng::below(NGROUPS - 1);
    note_ = "";
    gather(o.habitat, o.type);
    compose(res.group);
    if (res.monsters.empty() && o.habitat) {
        gather(0, o.type);
        compose(res.group);
        if (!res.monsters.empty()) note_ = String("None fit ") + HABITATS[o.habitat] + " - any habitat used";
    }
    if (res.monsters.empty() && o.type) {
        gather(o.habitat, 0);
        compose(res.group);
        if (!res.monsters.empty()) note_ = String("No ") + TYPES[o.type] + " fits - any type used";
    }
    std::sort(res.monsters.begin(), res.monsters.end(), [](const Entry& a, const Entry& b) { return a.xp > b.xp; });
    res.note = note_;
    out_ = nullptr;
    return !res.monsters.empty();
}

}  // namespace encounter

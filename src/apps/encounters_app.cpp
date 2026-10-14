#include "db_browser.h"
#include "../core/campaign.h"
#include "../core/encounter.h"
#include "../core/rng.h"

using namespace ui;

// Encounter generator: picks SRD monsters to fit the party's XP budget for a
// habitat, creature type and group style, then sends the fight to Initiative
// (or saves it as the Monsters app's encounter plan).

namespace {

const char* const HABITATS[] = {"Any", "Arctic", "Coastal", "Desert", "Dungeon", "Forest", "Grassland",
                                "Hill", "Mountain", "Planar", "Swamp", "Underdark", "Underwater", "Urban"};
constexpr int NHAB = 14;
const char* const TYPES[] = {"Any", "Aberration", "Beast", "Celestial", "Construct", "Dragon", "Elemental", "Fey",
                             "Fiend", "Giant", "Humanoid", "Monstrosity", "Ooze", "Plant", "Undead"};
constexpr int NTYPES = 15;
const char* const DIFF[] = {"Low", "Moderate", "High"};
const char* const GROUPS[] = {"Random", "Solo", "Leader + minions", "Horde", "Mixed"};
constexpr int NGROUPS = 5;
const char* const GROUP_SHORT[] = {"", "Solo", "Leader", "Horde", "Mixed"};
const char* const ACTIVITY[] = {
    "Lying in ambush", "On patrol", "Feeding on a kill", "Resting in a rough camp", "Hunting - tracking the party",
    "Guarding something valuable", "Fighting another group", "Lost, hungry and desperate", "Arguing over loot",
    "Fleeing something worse", "Searching for something", "Building or digging", "Performing a ritual",
    "Sleeping - mostly", "Escorting a prisoner", "Returning to their lair"};
const uint16_t ACCENT = rgb(230, 110, 50);

float crValue(const char* cr) {
    if (!strcmp(cr, "1/8")) return 0.125f;
    if (!strcmp(cr, "1/4")) return 0.25f;
    if (!strcmp(cr, "1/2")) return 0.5f;
    return atof(cr);
}

class EncountersApp : public App {
public:
    const char* name() const override { return "Encounters"; }
    const char* blurb() const override { return "Random fights by habitat & level."; }
    uint16_t accent() const override { return ACCENT; }

    void drawIcon(int cx, int cy, float t) override {
        // something lurks: glowing eyes between dark pines under a moon
        gfx.fillRoundRect(cx - 19, cy - 18, 38, 36, 6, rgb(20, 28, 40));
        gfx.fillCircle(cx + 10, cy - 10, 4, rgb(230, 225, 190));
        const int8_t trees[][3] = {{-13, 12, 7}, {0, 16, 9}, {13, 10, 6}};
        for (auto& tr : trees) {
            int x = cx + tr[0], top = cy - tr[1], w = tr[2];
            gfx.fillTriangle(x, top, x - w, cy + 14, x + w, cy + 14, rgb(30, 70, 50));
        }
        gfx.drawFastHLine(cx - 19, cy + 14, 38, rgb(30, 70, 50));
        // two pairs of eyes, blinking at different times
        float ph[2] = {fmodf(t, 3.1f), fmodf(t + 1.4f, 2.6f)};
        const int8_t eyes[2][2] = {{-7, 4}, {7, 8}};
        for (int i = 0; i < 2; i++) {
            if (t > 0 && ph[i] < 0.15f) continue;
            uint16_t c = i ? C_GOLD : rgb(240, 70, 50);
            gfx.fillRect(cx + eyes[i][0] - 3, cy + eyes[i][1], 2, 2, c);
            gfx.fillRect(cx + eyes[i][0] + 2, cy + eyes[i][1], 2, 2, c);
        }
        gfx.drawRoundRect(cx - 19, cy - 18, 38, 36, 6, ACCENT);
    }

    void onEnter() override {
        input::setTextMode(false);
        plan_.load();
        table_.open({{"/gmtools/srd/monsters", {"cr", "type", "xp", "hp", "ac", "init", "size", "habitat"}}},
                    "/gmtools/custom/monsters.txt", {"cr", "type", "xp", "hp", "ac", "init", "size", "habitat"});
        view_ = OPTIONS;
    }
    void onExit() override {
        closeViewer();
        plan_.save();
        table_.close();
        result_.clear();
    }

    bool handle(const input::Event& e) override {
        if (viewer_) {
            if (!viewer_->handle(e)) closeViewer();
            return true;
        }
        return view_ == OPTIONS ? handleOptions(e) : handleResult(e);
    }

    void draw() override {
        if (viewer_) { viewer_->draw(); return; }
        if (view_ == OPTIONS) drawOptions();
        else drawResult();
    }

private:
    enum View { OPTIONS, RESULT };
    View view_ = OPTIONS;
    db::Table table_;
    encounter::Plan plan_;
    std::vector<encounter::Entry> result_;
    int optSel_ = 0, resSel_ = 0;
    int budget_ = 0, groupUsed_ = 0;
    String activity_, note_;
    std::unique_ptr<DbBrowser> viewer_;

    // ---------------------------------------------------------------- options
    static constexpr int OPT_ROWS = 7;   // size, level, habitat, difficulty, type, group, generate

    bool handleOptions(const input::Event& e) {
        int d = 0;
        if (e.key == input::K_LEFT || (e.key == input::K_CHAR && e.base == '-')) d = -1;
        if (e.key == input::K_RIGHT || (e.key == input::K_CHAR && e.base == '=')) d = 1;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: optSel_ = (optSel_ + OPT_ROWS - 1) % OPT_ROWS; sfx::click(); return true;
            case input::K_DOWN: optSel_ = (optSel_ + 1) % OPT_ROWS; sfx::click(); return true;
            case input::K_TAB: optSel_ = (optSel_ + 1) % OPT_ROWS; sfx::click(); return true;
            case input::K_ENTER: generate(); return true;
            default: break;
        }
        if (e.key == input::K_CHAR && e.base == 'r') { generate(); return true; }
        if (!d) return true;
        auto wrap = [](int v, int n) { return (v + n) % n; };
        switch (optSel_) {
            case 0: plan_.partySize = std::max(1, std::min(10, plan_.partySize + d)); break;
            case 1: plan_.partyLevel = std::max(1, std::min(20, plan_.partyLevel + d)); break;
            case 2: plan_.genHabitat = wrap(plan_.genHabitat + d, NHAB); break;
            case 3: plan_.genDiff = wrap(plan_.genDiff + d, 3); break;
            case 4: plan_.genType = wrap(plan_.genType + d, NTYPES); break;
            case 5: plan_.genGroup = wrap(plan_.genGroup + d, NGROUPS); break;
            default: return true;
        }
        sfx::click();
        return true;
    }

    void drawOptions() {
        drawBackground();
        drawHeader("Encounter Generator", ACCENT);
        gfx.setFont(&fonts::Font0);
        const int rowH = 13, y0 = HEADER_H + 4;
        for (int i = 0; i < OPT_ROWS; i++) {
            int ry = y0 + i * rowH;
            bool s = i == optSel_;
            if (s) gfx.fillRoundRect(2, ry - 2, W - 4, rowH - 1, 3, C_PANEL);
            String l, r;
            switch (i) {
                case 0: l = "Party size"; r = String(plan_.partySize); break;
                case 1: l = "Party level"; r = String(plan_.partyLevel); break;
                case 2: l = "Habitat"; r = HABITATS[plan_.genHabitat]; break;
                case 3: l = "Difficulty"; r = DIFF[plan_.genDiff]; break;
                case 4: l = "Creature type"; r = TYPES[plan_.genType]; break;
                case 5: l = "Group"; r = GROUPS[plan_.genGroup]; break;
                default: break;
            }
            gfx.setTextDatum(top_left);
            if (i == OPT_ROWS - 1) {
                gfx.setTextDatum(top_center);
                gfx.setTextColor(s ? C_GOLD : C_EMERALD);
                gfx.drawString(">> Generate encounter <<", W / 2, ry + 1);
                continue;
            }
            gfx.setTextColor(s ? C_TEXT : C_DIM);
            gfx.drawString(l, 8, ry + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(s ? C_GOLD : C_ICE);
            gfx.drawString(s ? "< " + r + " >" : r, W - 8, ry + 1);
        }
        int b = encounter::budget(plan_.partyLevel, plan_.genDiff) * plan_.partySize;
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String("XP budget ") + b, W / 2, y0 + OPT_ROWS * rowH);
        drawFooter("</> change  Ent generate  Esc exit");
    }

    // ---------------------------------------------------------------- generation
    struct Cand { int idx, xp; float cr; const char* type; };
    std::vector<Cand> cands_;

    static bool typeMatch(const char* type, int t) { return t == 0 || strstr(type, TYPES[t]) != nullptr; }
    static bool habitatMatch(const char* hab, int h) {
        if (!*hab || strstr(hab, "Any") || h == 0) return true;   // custom monsters have no habitat
        return strstr(hab, HABITATS[h]) != nullptr;
    }

    void gather(int hab, int type) {
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
    const Cand* pick(int lo, int hi, float maxCr, const char* sameType = nullptr) {
        std::vector<const Cand*> any, same;
        for (auto& c : cands_) {
            if (c.xp < lo || c.xp > hi || c.cr > maxCr) continue;
            any.push_back(&c);
            if (sameType && !strcmp(c.type, sameType)) same.push_back(&c);
        }
        auto& v = same.empty() ? any : same;
        return v.empty() ? nullptr : v[rng::below(v.size())];
    }

    void add(const Cand* c, int count) {
        if (!c || count <= 0) return;
        for (auto& m : result_)
            if (m.name == table_.at(c->idx).name) { m.count += count; return; }
        db::Rec r;
        table_.read(c->idx, r);
        encounter::Entry en;
        en.name = table_.at(c->idx).name;
        en.count = count;
        en.xp = c->xp;
        en.hp = r.num("hp", 1);
        en.ac = r.num("ac", 10);
        en.init = r.get("init").toInt();
        en.hpdice = r.get("hpdice");
        en.cr = r.get("cr");
        result_.push_back(en);
    }

    int totalXp() const { int t = 0; for (auto& m : result_) t += m.xp * m.count; return t; }
    int bodies() const { int t = 0; for (auto& m : result_) t += m.count; return t; }

    // Fill result_ for one group style using the current candidates.
    void compose(int group) {
        const int B = budget_, lvl = plan_.partyLevel;
        const int cap = std::max(4, std::min(12, plan_.partySize * 3));
        result_.clear();
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
                    const char* like = result_.empty() ? nullptr : table_.col(table_.find(result_[0].name), "type");
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
        if (!result_.empty()) {
            auto* low = &result_[0];
            for (auto& m : result_) if (m.xp < low->xp) low = &m;
            while (totalXp() + low->xp <= B && totalXp() < B * 6 / 10 && bodies() < cap) low->count++;
        }
    }

    void generate() {
        budget_ = encounter::budget(plan_.partyLevel, plan_.genDiff) * plan_.partySize;
        groupUsed_ = plan_.genGroup ? plan_.genGroup : 1 + rng::below(NGROUPS - 1);
        note_ = "";
        gather(plan_.genHabitat, plan_.genType);
        compose(groupUsed_);
        if (result_.empty() && plan_.genHabitat) {
            gather(0, plan_.genType);
            compose(groupUsed_);
            if (!result_.empty()) note_ = String("None fit ") + HABITATS[plan_.genHabitat] + " - any habitat used";
        }
        if (result_.empty() && plan_.genType) {
            gather(plan_.genHabitat, 0);
            compose(groupUsed_);
            if (!result_.empty()) note_ = String("No ") + TYPES[plan_.genType] + " fits - any type used";
        }
        if (result_.empty()) { sfx::error(); toast("No monsters fit that budget", C_BLOOD); return; }
        std::sort(result_.begin(), result_.end(), [](const encounter::Entry& a, const encounter::Entry& b) { return a.xp > b.xp; });
        activity_ = rng::pick(ACTIVITY);
        plan_.save();
        view_ = RESULT;
        resSel_ = 0;
        sfx::crit();
    }

    // ---------------------------------------------------------------- result
    // rows: monsters..., send rolled, send average, save to Monsters, add to session, reroll
    int resRows() const { return result_.size() + 5; }

    bool handleResult(const input::Event& e) {
        int n = resRows(), action = resSel_ - (int)result_.size();
        switch (e.key) {
            case input::K_BACK: view_ = OPTIONS; sfx::back(); return true;
            case input::K_UP: resSel_ = (resSel_ + n - 1) % n; sfx::click(); return true;
            case input::K_DOWN: resSel_ = (resSel_ + 1) % n; sfx::click(); return true;
            case input::K_ENTER:
                if (action < 0) viewMonster(result_[resSel_].name);
                else if (action <= 1) send(action == 0);
                else if (action == 2) saveToPlan();
                else if (action == 3) addToSession();
                else generate();
                return true;
            default: break;
        }
        if (e.key == input::K_CHAR && e.base == 'r') generate();
        return true;
    }

    void send(bool rolledHp) {
        int n = encounter::enqueue(result_, rolledHp);
        if (!n) return;
        sfx::crit();
        requestApp("Initiative");
    }

    void saveToPlan() {
        plan_.monsters = result_;
        plan_.save();
        toast("Saved as the Monsters fight (Fn+E)", C_EMERALD);
        sfx::select();
    }

    void addToSession() {
        String text = encounter::describe(result_) + " (" + HABITATS[plan_.genHabitat] + ", " + DIFF[plan_.genDiff] + ", " +
                      totalXp() + " XP)";
        String title;
        if (!campaign::addToCurrentSession("Encounters", text, title)) {
            sfx::error();
            toast("Open a session in Campaign first", C_BLOOD);
            return;
        }
        toast((String("Added to ") + title).c_str(), C_EMERALD);
        sfx::select();
    }

    void viewMonster(const String& m) {
        viewer_.reset(newMonstersBrowser());
        viewer_->closeOnBack = true;
        viewer_->onEnter();
        if (!viewer_->openByName(m)) { closeViewer(); toast("Stat block not found", C_BLOOD); }
    }
    void closeViewer() {
        if (viewer_) { viewer_->onExit(); viewer_.reset(); input::setTextMode(false); }
    }

    void drawResult() {
        drawBackground();
        drawHeader((String(HABITATS[plan_.genHabitat]) + " encounter").c_str(), ACCENT);
        gfx.setFont(&fonts::Font0);
        int xp = totalXp(), b[3];
        for (int d = 0; d < 3; d++) b[d] = encounter::budget(plan_.partyLevel, d) * plan_.partySize;
        int diff = xp <= b[0] ? 0 : xp <= b[1] ? 1 : xp <= b[2] ? 2 : 3;
        uint16_t col = diff == 0 ? C_EMERALD : diff == 1 ? C_GOLD : diff == 2 ? rgb(240, 130, 60) : C_BLOOD;
        int y = HEADER_H + 3;
        gfx.setTextDatum(top_left);
        gfx.setTextColor(col);
        gfx.drawString(String(xp) + " XP " + (diff < 3 ? DIFF[diff] : "Deadly"), 6, y);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(GROUP_SHORT[groupUsed_]) + "  " + plan_.partySize + "xL" + plan_.partyLevel, W - 6, y);
        float scale = b[2] * 1.25f;
        int bx = 6, bw = W - 12;
        gfx.fillRect(bx, y + 10, bw, 5, C_BG2);
        gfx.fillRect(bx, y + 10, std::min(bw, (int)(bw * xp / scale)), 5, col);
        for (int d = 0; d < 3; d++) gfx.drawFastVLine(bx + (int)(bw * b[d] / scale), y + 8, 9, C_TEXT);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(note_.length() ? C_BLOOD : C_ICE);
        gfx.drawString(note_.length() ? note_ : activity_, 6, y + 18);

        const int rowH = 11, y0 = y + 30;
        int rows = (H - FOOTER_H - y0) / rowH, n = resRows();
        int first = std::max(0, std::min(resSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int ry = y0 + (i - first) * rowH;
            bool s = i == resSel_;
            if (s) gfx.fillRoundRect(2, ry - 1, W - 4, rowH, 2, C_PANEL);
            int action = i - (int)result_.size();
            String l, r;
            uint16_t c = C_TEXT;
            if (action < 0) {
                auto& m = result_[i];
                l = String(m.count) + "x " + m.name;
                r = String("CR") + m.cr + " " + (m.xp * m.count);
            } else if (action == 0) { l = "Fight! Send to Initiative (roll HP)"; c = C_EMERALD; }
            else if (action == 1) { l = "Send to Initiative (average HP)"; c = C_EMERALD; }
            else if (action == 2) { l = "Save as Monsters fight plan"; c = C_ICE; }
            else if (action == 3) { l = "Add to session plan (Campaign)"; c = ACCENT; }
            else { l = "Reroll"; c = C_GOLD; }
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : c);
            gfx.drawString(l.substring(0, action < 0 ? 26 : 38), 6, ry + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(r, W - 6, ry + 1);
        }
        drawFooter("Ent stats/do  R reroll  Esc options");
    }
};

}  // namespace

App* makeEncountersApp() { return new EncountersApp(); }

#include "../core/app.h"
#include "../core/touch.h"
#include "../core/dice_expr.h"
#include "../core/rng.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/textview.h"
#include "../core/ui.h"
#include "../core/web.h"
#include "../core/widgets.h"
#include <algorithm>
#include <vector>

using namespace ui;

// Random tables. Built-ins below, SRD tables from /gmtools/srd/tables, and every
// .txt in /gmtools/tables on the SD card (yours win on a name clash).
// Entry syntax:  {name} pulls a random entry from another table,  [2d6] rolls dice,
// a leading "N<TAB>" gives the entry weight N (e.g. 20 for a d100 range 01-20).
// Tables whose name starts with '_' are helpers and are hidden from the list.

namespace {

struct BuiltinTable { const char* name; std::vector<const char*> items; };

const std::vector<BuiltinTable>& fantasyTables() {
    static const std::vector<BuiltinTable> t = {
        {"NPC", {"{_first} {_last}, a {_trait} {_job}"}},
        {"Tavern", {"The {_tavadj} {_tavnoun}"}},
        // Loot by tier (homebrew - the SRD has no treasure tables). Magic items and
        // trinkets come from the SRD tables on the SD card. "N<TAB>" weights an entry.
        {"Loot (Tier 1, levels 1-4)", {"4\t[3d6] gp", "3\t[2d6] gp and {Trinket}", "2\t[4d6] sp, [1d6] gp",
                                        "2\t[2d6] gp and {Magic Item (Common)}", "1\t{Magic Item (Uncommon)}"}},
        {"Loot (Tier 2, levels 5-10)", {"4\t[2d6]0 gp", "3\t[2d6]0 gp and {Trinket}", "3\t[1d6]0 gp and {Magic Item (Uncommon)}",
                                         "1\t{Magic Item (Rare)}"}},
        {"Loot (Tier 3, levels 11-16)", {"3\t[4d6]0 gp", "3\t[2d6]00 gp and {Magic Item (Rare)}", "2\t{Magic Item (Rare)}",
                                          "1\t{Magic Item (Very Rare)}"}},
        {"Loot (Tier 4, levels 17-20)", {"3\t[4d6]00 gp", "3\t[1d6]000 gp and {Magic Item (Very Rare)}", "2\t{Magic Item (Very Rare)}",
                                          "1\t{Magic Item (Legendary)}"}},
        {"Location", {"The {_locadj} {_locnoun}", "{_last}'s {_locnoun}", "The {_locnoun} of {_last}",
                      "The {_locadj} {_locnoun}, near {_last} Ford"}},
        {"Strong start", {"An explosion rips through the street - and the culprit is running.",
                          "A wounded messenger collapses at the party's feet clutching a sealed letter.",
                          "Ambush! Arrows thud into the cart as the road ahead is blocked.",
                          "The floor gives way, dropping the party into something old and dark.",
                          "A pickpocket grabs something precious and bolts across the rooftops.",
                          "The party wakes up bound, in a moving wagon, with no memory of the night.",
                          "A {_monster} bursts through the tavern wall mid-meal.",
                          "The bridge they are crossing starts to collapse.",
                          "A stranger presses a key into a hero's hand and whispers 'Run.'",
                          "Bells ring the alarm: the town is under attack.",
                          "A duel to the death is about to start - and one fighter is a friend.",
                          "Guards arrive to arrest the party for a crime they did not commit."}},
        {"Weather", {"Clear and cold, frost on everything", "Light drizzle, roads turn to mud",
                     "Fog so thick you can't see 30 ft", "Thunderstorm rolling in by evening",
                     "Sweltering heat, shade is precious", "Howling wind, ranged attacks suffer",
                     "Snowfall, tracks are easy to follow", "Eerie calm, no birdsong at all",
                     "Warm and bright, a perfect day", "Hail, briefly but brutally"}},
        {"Rumor", {"The miller's daughter sees the dead in her dreams.", "A dragon was spotted over the hills.",
                   "The baron is broke and selling heirlooms in secret.", "Wolves won't go near the old shrine.",
                   "A merchant pays triple for {_monster} teeth.", "The bridge toll-keeper is a {_monster} in disguise.",
                   "Someone is buying every lockpick in town.", "The river ran red for an hour last week."}},
        {"_first", {"Alda", "Bram", "Cora", "Dain", "Elsbeth", "Fenn", "Greta", "Hollis", "Isolde", "Jory", "Kestra",
                    "Lio", "Maren", "Nils", "Odile", "Pell", "Quill", "Rowan", "Sabine", "Tobin", "Una", "Vesper",
                    "Wren", "Yorick"}},
        {"_last", {"Ashdown", "Blackwater", "Crane", "Dunmore", "Emberly", "Fairweather", "Greaves", "Hollow",
                   "Ironside", "Juniper", "Kettle", "Longfellow", "Marsh", "Nettle", "Oakheart", "Pike", "Quarry",
                   "Redfern", "Stone", "Thorne"}},
        {"_trait", {"nervous", "boisterous", "sly", "kindly", "grim", "pious", "greedy", "lovesick", "paranoid",
                    "cheerful", "weary", "secretive", "pompous", "curious"}},
        {"_job", {"blacksmith", "priest", "innkeeper", "guard captain", "fence", "herbalist", "minstrel",
                  "farmer", "scribe", "sellsword", "noble's heir", "beggar", "ferryman", "alchemist"}},
        {"_tavadj", {"Prancing", "Drunken", "Gilded", "Rusty", "Sleeping", "Laughing", "Crooked", "Golden",
                     "Black", "Wandering", "Hungry", "Silver"}},
        {"_tavnoun", {"Pony", "Goblin", "Tankard", "Dragon", "Griffin", "Barrel", "Maiden", "Stag", "Lantern",
                      "Boar", "Crow", "Anchor"}},
        {"_locadj", {"Sunken", "Whispering", "Burnt", "Hollow", "Silent", "Crooked", "Drowned", "Forgotten",
                     "Shattered", "Gilded", "Thorned", "Moonlit", "Ashen", "Weeping"}},
        {"_locnoun", {"Chapel", "Mill", "Tower", "Crypt", "Bridge", "Market", "Keep", "Grove", "Mine", "Lighthouse",
                      "Barrow", "Abbey", "Docks", "Archive", "Watchpost", "Shrine"}},
        {"_monster", {"troll", "wyvern", "owlbear", "basilisk", "manticore", "hag", "ogre"}},
    };
    return t;
}

struct Table {
    String name;
    std::vector<String> items;
    std::vector<uint16_t> weight;
    uint32_t total = 0;
    bool srd = false;
    // Entries may start with "N<TAB>" (a weight, from dice ranges like 01-20).
    void finish() {
        weight.clear();
        total = 0;
        for (auto& it : items) {
            int tab = it.indexOf('\t');
            uint16_t w = 1;
            if (tab > 0 && tab < 4 && it.substring(0, tab).toInt() > 0) { w = it.substring(0, tab).toInt(); it = it.substring(tab + 1); }
            weight.push_back(w);
            total += w;
        }
    }
    bool weighted() const { return total != items.size(); }
    // Returns the entry index; `roll` gets the die value (1..total).
    int pick(int& roll) const {
        roll = rng::roll(total);
        uint32_t acc = 0;
        for (size_t i = 0; i < items.size(); i++) { acc += weight[i]; if ((uint32_t)roll <= acc) return i; }
        return items.size() - 1;
    }
};

class TablesApp : public App {
public:
    const char* name() const override { return "Random Tables"; }
    const char* blurb() const override { return "NPCs, loot, rumors & yours."; }
    uint16_t accent() const override { return C_ARCANE; }

    void drawIcon(int cx, int cy, float t) override {
        // scroll
        gfx.fillRoundRect(cx - u(14), cy - u(16), u(28), u(32), u(3), rgb(220, 200, 160));
        gfx.fillRoundRect(cx - u(17), cy - u(19), u(34), u(6), u(3), rgb(170, 140, 100));
        gfx.fillRoundRect(cx - u(17), cy + u(13), u(34), u(6), u(3), rgb(170, 140, 100));
        for (int i = 0; i < 4; i++) {
            int w = 10 + ((i * 7 + (int)(t * 3)) % 9);
            gfx.drawFastHLine(cx - u(10), cy - u(8) + i * u(6), w, rgb(90, 70, 50));
        }
    }

    void onEnter() override {
        input::setTextMode(false);
        load();
        view_ = 0;
    }

    bool handle(const input::Event& e) override {
        if (wide()) return handleWide(e);
        int n = visible_.size();
        if (view_ == 1) {
            if (e.key == input::K_BACK) { view_ = 0; sfx::back(); return true; }
            if (e.key == input::K_ENTER || (e.key == input::K_CHAR && e.base == ' ')) rollCurrent();
            else if (e.key == input::K_DOWN) scroll_++;
            else if (e.key == input::K_UP && scroll_ > 0) scroll_--;
            return true;
        }
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: if (n) { sel_ = (sel_ + n - 1) % n; sfx::click(); } break;
            case input::K_DOWN: if (n) { sel_ = (sel_ + 1) % n; sfx::click(); } break;
            case input::K_ENTER: if (n) { view_ = 1; results_.clear(); rollCurrent(); } break;
            case input::K_CHAR:
                for (int i = 1; i <= n; i++) {
                    int j = (sel_ + i) % n;
                    if (toupper(tables_[visible_[j]].name[0]) == toupper(e.ch)) { sel_ = j; sfx::click(); break; }
                }
                break;
            default: break;
        }
        return true;
    }

    void tick(uint32_t dt) override { if (reveal_ < 1) reveal_ = std::min(1.0f, reveal_ + dt * 0.0035f); }

    bool bigLayout() const override { return true; }

    void draw() override {
        drawBackground();
        drawHeader("Random Tables", C_ARCANE);
        if (wide()) drawWide();
        else if (view_ == 1) drawResult();
        else drawListView();
    }

private:
    std::vector<Table> tables_;
    std::vector<int> visible_;
    std::vector<String> results_;  // newest first
    int sel_ = 0, view_ = 0, scroll_ = 0;
    float reveal_ = 1;
    String lastRoll_;
    int lastIdx_ = -1;   // the entry rolled last, for the big screen's entry list

public:
    // The web page's tables: the list, the entries of one, and a roll.
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (tables_.empty() || c.op == "reload") load();
        if (c.op == "list" || c.op == "reload") {
            out = "[";
            bool first = true;
            for (auto i : visible_) {
                const Table& t = tables_[i];
                out += String(first ? "" : ",") + "{\"name\":" + web::js(t.name) + ",\"srd\":" + (t.srd ? "true" : "false") +
                       ",\"n\":" + t.items.size() + "}";
                first = false;
            }
            out += "]";
            return 200;
        }
        const Table* t = find(c.arg("name"));
        if (!t || t->items.empty()) { out = "No such table"; return 404; }
        if (c.op == "entries") {
            out = "[";
            for (size_t i = 0; i < t->items.size(); i++)
                out += String(i ? "," : "") + "{\"text\":" + web::js(t->items[i]) + ",\"w\":" + t->weight[i] + "}";
            out += "]";
            return 200;
        }
        if (c.op != "roll") return 404;
        int roll;
        const int idx = t->pick(roll);
        const String how = t->weighted() || t->total == 100 ? String("d") + t->total + ": " + roll : String(idx + 1) + " of " + t->items.size();
        out = String("{\"text\":") + web::js(expand(t->items[idx], 0)) + ",\"index\":" + idx + ",\"how\":" + web::js(how) + "}";
        return 200;
    }

    // load / find / expand are also used by rollRandomTable()
    void load() {
        tables_.clear();
        visible_.clear();
        const auto& b = fantasyTables();
        for (auto& t : b) {
            Table x;
            x.name = t.name;
            for (auto i : t.items) x.items.push_back(i);
            x.finish();
            tables_.push_back(x);
        }
        // SRD tables first, then the user's (which can replace any table by name)
        for (const char* dir : {"/gmtools/srd/tables", "/gmtools/tables"}) {
            std::vector<String> files;
            storage::listFiles(dir, files);
            for (auto& f : files) {
                Table x;
                x.name = f.substring(0, f.length() - 4);
                x.name.replace('_', ' ');
                if (f[0] == '_') x.name = "_" + x.name.substring(1);
                x.srd = dir[9] == 's';
                storage::readLines((String(dir) + "/" + f).c_str(), x.items, 600);
                if (x.items.empty()) continue;
                x.finish();
                bool replaced = false;
                for (auto& t : tables_) if (t.name.equalsIgnoreCase(x.name)) { t = x; replaced = true; }
                if (!replaced) tables_.push_back(x);
            }
        }
        std::sort(tables_.begin(), tables_.end(), [](const Table& a, const Table& b) { return a.name < b.name; });
        for (size_t i = 0; i < tables_.size(); i++) if (tables_[i].name[0] != '_') visible_.push_back(i);
        if (sel_ >= (int)visible_.size()) sel_ = 0;
    }

    const Table* find(const String& name) {
        for (auto& t : tables_) if (t.name.equalsIgnoreCase(name)) return &t;
        return nullptr;
    }

    String expand(const String& s, int depth) {
        if (depth > 6) return s;
        String out;
        for (int i = 0; i < (int)s.length(); i++) {
            char c = s[i];
            if (c == '{' || c == '[') {
                char close = c == '{' ? '}' : ']';
                int j = s.indexOf(close, i + 1);
                if (j > i) {
                    String inner = s.substring(i + 1, j);
                    if (c == '{') {
                        const Table* t = find(inner);
                        int r;
                        out += t && !t->items.empty() ? expand(t->items[t->pick(r)], depth + 1) : inner;
                    } else {
                        auto r = dice::roll(inner);
                        out += r.ok ? String(r.total) : inner;
                    }
                    i = j;
                    continue;
                }
            }
            out += c;
        }
        return out;
    }

    void rollCurrent() {
        const Table& t = tables_[visible_[sel_]];
        if (t.items.empty()) return;
        int roll;
        int idx = t.pick(roll);
        lastRoll_ = t.weighted() || t.total == 100 ? String("d") + t.total + ": " + (roll == 100 && t.total == 100 ? String("00") : String(roll))
                                                   : String(idx + 1) + " of " + t.items.size();
        results_.insert(results_.begin(), expand(t.items[idx], 0));
        lastIdx_ = idx;
        if (wide()) {
            history_.insert(history_.begin(), {t.name, results_[0]});
            if (history_.size() > 12) history_.pop_back();
            resPos_ = 0;
            if (showEntries_) fillEntries();
        }
        scroll_ = 0;
        if (results_.size() > 6) results_.pop_back();
        reveal_ = 0;
        sfx::diceRattle();
    }

    void drawListView() {
        int n = visible_.size();
        gfx.setFont(ui::font(&fonts::Font0));
        const int rowH = touchRow(13), pad = touchPad(13), top = HEADER_H + u(5), rows = wide() ? (H - FOOTER_H - top) / rowH : 8;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH + pad;
            bool s = i == sel_;
            const Table& t = tables_[visible_[i]];
            if (s) drawPanel(u(4), y - pad - u(2), W - u(8), rowH, C_PANEL, C_ARCANE);
            touch::row(0, y - pad - u(2), W, rowH, sel_, i);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : rgb(190, 184, 200));
            gfx.drawString(t.name, u(12), y + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            if (t.srd) { gfx.setTextColor(C_GOLD_DK); gfx.drawString("SRD", W - u(12), y + u(1)); }
            else gfx.drawString(String(t.items.size()), W - u(12), y + u(1));
        }
        drawFooter(storage::ok() ? "Ent roll  Esc back  add .txt to /gmtools/tables" : "Ent roll  Esc back  (no SD: built-ins only)");
    }

    void drawResult() {
        const Table& t = tables_[visible_[sel_]];
        gfx.setTextDatum(top_left);
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextColor(C_ARCANE);
        String title = t.name;
        while (title.length() > 3 && gfx.textWidth(title) > W - u(70)) title = title.substring(0, title.length() - 1);
        gfx.drawString(title, u(8), HEADER_H + u(3));
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(lastRoll_, W - u(8), HEADER_H + u(7));

        const int cy = HEADER_H + u(22);
        if (results_.empty()) return;
        String r = results_[0];
        // biggest font that fits; long SRD entries fall back to small text and scroll
        std::vector<String> lines;
        const lgfx::IFont* fontsToTry[] = {&fonts::FreeSerif9pt7b, &fonts::Font2, &fonts::Font0};
        int maxH = H - FOOTER_H - cy - u(4);
        int lh = u(10);
        for (auto f : fontsToTry) {
            gfx.setFont(ui::font(f));
            lines.clear();
            wrapLines(r, W - u(20), lines);
            lh = gfx.fontHeight() + 1;
            if ((int)lines.size() * lh + u(8) <= std::min(maxH, u(60)) || f == &fonts::Font0) break;
        }
        int visible = std::max(1, (maxH - u(8)) / lh);
        int maxScroll = std::max(0, (int)lines.size() - visible);
        if (scroll_ > maxScroll) scroll_ = maxScroll;
        int ch = std::min(maxH, (int)lines.size() * lh + u(8));
        drawPanel(u(4), cy, W - u(8), ch, C_PANEL, C_ARCANE);
        int chars = (int)(r.length() * easeOut(reveal_));
        int y = cy + u(4);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_TEXT);
        for (int i = 0; i < (int)lines.size(); i++) {
            const String& l = lines[i];
            if (chars <= 0) break;
            String vis = l.substring(0, std::min((int)l.length(), chars));
            chars -= l.length() + 1;
            if (i < scroll_ || i >= scroll_ + visible) continue;
            gfx.drawString(vis, u(10), y);
            y += lh;
        }
        if (maxScroll) {
            int sh = std::max(u(5), (ch - u(8)) * visible / (int)lines.size());
            gfx.fillRect(W - u(8), cy + u(4) + (ch - u(8) - sh) * scroll_ / maxScroll, u(2), sh, C_ARCANE);
        }
        // previous results, dimmer, if there's room
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextColor(C_DIM);
        y = cy + ch + u(4);
        for (size_t i = 1; i < results_.size() && y < H - FOOTER_H - u(9); i++, y += u(10)) {
            String p = results_[i];
            while (p.length() > 3 && gfx.textWidth(p) > W - u(16)) p = p.substring(0, p.length() - 4) + "...";
            gfx.drawString(p, u(8), y);
        }
        drawFooter(maxScroll ? "Ent roll again  Up/Dn scroll  Esc" : "Ent roll again  Esc back");
    }

    // ---------------------------------------------------------------- big screen
    // The tables on the left (filtered by where they come from); on the right
    // a Roll button, the result as it's written out, and the last rolls from
    // any table. "Entries" lists the whole table with its dice ranges.
    enum Filter { F_ALL, F_BUILTIN, F_SRD, F_MINE };
    int filter_ = F_ALL;
    std::vector<int> shown_;            // indexes into visible_ that pass the filter
    std::vector<std::pair<String, String>> history_;   // (table, result), newest first
    float listPos_ = 0, resPos_ = 0;
    bool showEntries_ = false;
    int followSel_ = -1;
    TextView entries_;

    bool builtin(const Table& t) const {
        for (auto& b : fantasyTables()) if (t.name == b.name) return true;
        return false;
    }
    bool passes(const Table& t) const {
        switch (filter_) {
            case F_BUILTIN: return !t.srd && builtin(t);
            case F_SRD: return t.srd;
            case F_MINE: return !t.srd && !builtin(t);
            default: return true;
        }
    }
    void refilter() {
        shown_.clear();
        for (int i = 0; i < (int)visible_.size(); i++) if (passes(tables_[visible_[i]])) shown_.push_back(i);
    }

    void choose(int i) {
        if (i != sel_) { results_.clear(); lastIdx_ = -1; }
        sel_ = i;
        if (showEntries_) fillEntries();
    }

    // The entries with their die ranges ("01-20"), the last roll picked out.
    void fillEntries() {
        const Table& t = tables_[visible_[sel_]];
        entries_.clear();
        entries_.setWidth(entriesW_);
        const bool ranged = t.weighted() || t.total == 100;
        uint32_t at = 1;
        for (size_t i = 0; i < t.items.size(); i++) {
            String r;
            if (ranged) {
                const uint32_t lo = at, hi = at + t.weight[i] - 1;
                r = lo == hi ? String(lo) : String(lo) + "-" + hi;
            } else r = String(i + 1);
            at += t.weight[i];
            entries_.add(r + "  " + t.items[i], (int)i == lastIdx_ ? TextView::ACCENT : TextView::BODY, entriesW_ - 20);
        }
    }
    int entriesW_ = 760;

    bool handleWide(const input::Event& e) {
        refilter();
        const int n = shown_.size();
        int at = 0;
        for (int k = 0; k < n; k++) if (shown_[k] == sel_) at = k;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: if (n) { choose(shown_[(at + n - 1) % n]); sfx::click(); } return true;
            case input::K_DOWN: if (n) { choose(shown_[(at + 1) % n]); sfx::click(); } return true;
            case input::K_ENTER: if (n) rollCurrent(); return true;
            case input::K_TAB: showEntries_ = !showEntries_; if (showEntries_) fillEntries(); sfx::click(); return true;
            case input::K_CHAR:
                if (e.base == ' ') { if (n) rollCurrent(); return true; }
                for (int i = 1; i <= n; i++) {
                    const int j = shown_[(at + i) % n];
                    if (toupper(tables_[visible_[j]].name[0]) == toupper(e.ch)) { choose(j); sfx::click(); break; }
                }
                return true;
            default: return true;
        }
    }

    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        refilter();
        const int LX = 16, LW = 420, RX = LX + LW + 16, RW = W - RX - 16, top = HEADER_H + 10, bottom = H - FOOTER_H - 10;

        // filters
        const char* const FN[] = {"All", "Built-in", "SRD", "Mine"};
        const int FW[] = {74, 144, 88, 90};   // fitted to the labels
        for (int f = 0, fx = LX; f < 4; fx += FW[f] + 8, f++)
            button(fx, top, FW[f], 50, FN[f], filter_ == f, C_ARCANE, [this, f] { filter_ = f; listPos_ = 0; sfx::click(); });

        // the tables
        const int ly = top + 62, lh = bottom - ly, rowH = 64, n = shown_.size();
        const float maxPos = std::max(0, n * rowH - 6 - lh);
        if (sel_ != followSel_) {   // a keyboard choice stays in view
            for (int k = 0; k < n; k++)
                if (shown_[k] == sel_) {
                    listPos_ = std::min(listPos_, (float)(k * rowH));
                    listPos_ = std::max(listPos_, (float)((k + 1) * rowH - 6 - lh));
                }
            followSel_ = sel_;
        }
        listPos_ = std::max(0.0f, std::min(listPos_, maxPos + 60));
        touch::scroller(LX, ly, LW, lh, &listPos_, maxPos);
        gfx.setClipRect(LX, ly, LW, lh);
        for (int k = 0; k < n; k++) {
            const int i = shown_[k], y = ly + k * rowH - (int)listPos_;
            if (y + rowH < ly || y > ly + lh) continue;
            const Table& t = tables_[visible_[i]];
            const bool on = i == sel_;
            gfx.fillRoundRect(LX, y, LW, rowH - 6, 10, on ? C_PANEL : C_BG2);
            gfx.drawRoundRect(LX, y, LW, rowH - 6, 10, on ? C_ARCANE : C_EDGE);
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(t.srd ? C_GOLD_DK : C_DIM);
            const String tag = t.srd ? String("SRD") : String(t.items.size());
            gfx.drawString(tag, LX + LW - 14, y + (rowH - 6) / 2);
            const int nameW = LW - 40 - gfx.textWidth(tag);
            gfx.setFont(m.body);
            if (gfx.textWidth(t.name) > nameW) gfx.setFont(m.small);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(on ? C_TEXT : rgb(190, 184, 200));
            gfx.drawString(fitText(t.name, nameW), LX + 16, y + (rowH - 6) / 2 + 1);
            const int vy = std::max(y, ly), vh = std::min(y + rowH - 6, ly + lh) - vy;
            if (vh > 0) touch::zone(LX, vy, LW, vh, [this, i] { choose(i); rollCurrent(); });
        }
        gfx.clearClipRect();
        if (!n) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString(filter_ == F_MINE ? "Add .txt files to /gmtools/tables" : "No tables here.", LX + LW / 2, ly + 20);
        }
        if (visible_.empty()) { drawFooter("Esc back"); return; }

        // the chosen table
        const Table& t = tables_[visible_[sel_]];
        button(RX + RW - 170, top, 170, 64, "Roll", true, C_GOLD, [this] { rollCurrent(); });
        button(RX + RW - 340, top, 160, 64, showEntries_ ? "Result" : "Entries", false, C_ARCANE, [this] {
            showEntries_ = !showEntries_;
            if (showEntries_) fillEntries();
            sfx::click();
        });
        gfx.setTextDatum(top_left);
        gfx.setFont(m.title);
        gfx.setTextColor(C_ARCANE);
        gfx.drawString(fitText(t.name, RW - 360), RX + 4, top + 2);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        const bool ranged = t.weighted() || t.total == 100;
        gfx.drawString(String(t.srd ? "SRD  " : builtin(t) ? "Built-in  " : "Yours  ") + t.items.size() + " entries" +
                           (ranged ? String("  d") + t.total : String("")), RX + 4, top + 42);

        const int py = top + 76, ph = bottom - py;
        if (showEntries_) {
            drawPanel(RX, py, RW, ph, C_BG2, C_EDGE);
            entriesW_ = RW - 40;
            entries_.draw(RX + 18, py + 12, ph - 24, C_ARCANE);
            drawFooter("Ent roll  Tab result  Up/Dn table  Esc back");
            return;
        }

        // the result, in the biggest font it fits
        const int histH = history_.size() > 1 ? 210 : 0;
        const int cardMax = ph - histH;
        if (results_.empty()) {
            drawPanel(RX, py, RW, cardMax - 10, C_BG2, C_EDGE);
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Tap Roll.", RX + RW / 2, py + (cardMax - 10) / 2);
        } else {
            const String& r = results_[0];
            std::vector<String> lines;
            const lgfx::IFont* fonts[] = {m.title, m.body, m.small};
            int lh = m.line;
            for (auto f : fonts) {
                gfx.setFont(f);
                lines.clear();
                wrapLines(r, RW - 48, lines);
                lh = gfx.fontHeight() + 6;
                if ((int)lines.size() * lh + 70 <= cardMax - 10 || f == m.small) break;
            }
            const int textH = (int)lines.size() * lh, ch = std::min(cardMax - 10, textH + 70);
            drawPanel(RX, py, RW, ch, C_PANEL, C_ARCANE);
            gfx.setTextDatum(top_right);
            const lgfx::IFont* used = gfx.getFont();
            gfx.setFont(m.small);
            gfx.setTextColor(C_GOLD);
            gfx.drawString(lastRoll_, RX + RW - 16, py + 12);
            gfx.setFont(used);
            const int ty = py + 46, th = ch - 56;
            const float maxR = std::max(0, textH - th);
            resPos_ = std::max(0.0f, std::min(resPos_, maxR + 40));
            if (maxR > 0) touch::scroller(RX, ty, RW, th, &resPos_, maxR);
            gfx.setClipRect(RX + 4, ty, RW - 8, th);
            int chars = (int)(r.length() * easeOut(reveal_));
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_TEXT);
            for (size_t i = 0; i < lines.size() && chars > 0; i++) {
                const String& l = lines[i];
                gfx.drawString(l.substring(0, std::min((int)l.length(), chars)), RX + 24, ty + i * lh - (int)resPos_);
                chars -= l.length() + 1;
            }
            gfx.clearClipRect();
        }

        // earlier rolls, from any table
        if (histH) {
            int hy = py + cardMax + 4;
            caption(RX, hy - 4, "EARLIER");
            hy += 24;
            for (size_t i = 1; i < history_.size() && hy + 56 <= bottom; i++, hy += 62) {
                gfx.setTextDatum(top_left);
                gfx.setFont(m.body);
                gfx.setTextColor(C_TEXT);
                gfx.drawString(fitText(history_[i].second, RW - 16), RX + 8, hy);
                gfx.setFont(m.small);
                gfx.setTextColor(C_ARCANE);
                gfx.drawString(fitText(history_[i].first, RW - 16), RX + 8, hy + 32);
            }
        }
        drawFooter("Ent roll  Tab entries  Up/Dn table  Esc back");
    }
};

}  // namespace

App* makeTablesApp() { return new TablesApp(); }

// Shared roller for other apps; the tables stay loaded until released.
static TablesApp* s_shared = nullptr;
String rollRandomTable(const String& name) {
    if (!s_shared) { s_shared = new TablesApp(); s_shared->load(); }
    return s_shared->find(name) ? s_shared->expand(String("{") + name + "}", 0) : String("");
}
void releaseRandomTables() { delete s_shared; s_shared = nullptr; }

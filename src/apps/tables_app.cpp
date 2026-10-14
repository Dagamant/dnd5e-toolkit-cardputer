#include "../core/app.h"
#include "../core/dice_expr.h"
#include "../core/rng.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
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
        gfx.fillRoundRect(cx - 14, cy - 16, 28, 32, 3, rgb(220, 200, 160));
        gfx.fillRoundRect(cx - 17, cy - 19, 34, 6, 3, rgb(170, 140, 100));
        gfx.fillRoundRect(cx - 17, cy + 13, 34, 6, 3, rgb(170, 140, 100));
        for (int i = 0; i < 4; i++) {
            int w = 10 + ((i * 7 + (int)(t * 3)) % 9);
            gfx.drawFastHLine(cx - 10, cy - 8 + i * 6, w, rgb(90, 70, 50));
        }
    }

    void onEnter() override {
        input::setTextMode(false);
        load();
        view_ = 0;
    }

    bool handle(const input::Event& e) override {
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

    void draw() override {
        drawBackground();
        drawHeader("Random Tables", C_ARCANE);
        if (view_ == 1) drawResult();
        else drawListView();
    }

private:
    std::vector<Table> tables_;
    std::vector<int> visible_;
    std::vector<String> results_;  // newest first
    int sel_ = 0, view_ = 0, scroll_ = 0;
    float reveal_ = 1;
    String lastRoll_;

public:   // load / find / expand are also used by rollRandomTable()
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
        scroll_ = 0;
        if (results_.size() > 6) results_.pop_back();
        reveal_ = 0;
        sfx::diceRattle();
    }

    void drawListView() {
        int n = visible_.size();
        gfx.setFont(&fonts::Font0);
        const int rowH = 13, top = HEADER_H + 5, rows = 8;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH;
            bool s = i == sel_;
            const Table& t = tables_[visible_[i]];
            if (s) drawPanel(4, y - 2, W - 8, rowH, C_PANEL, C_ARCANE);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : rgb(190, 184, 200));
            gfx.drawString(t.name, 12, y + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            if (t.srd) { gfx.setTextColor(C_GOLD_DK); gfx.drawString("SRD", W - 12, y + 1); }
            else gfx.drawString(String(t.items.size()), W - 12, y + 1);
        }
        drawFooter(storage::ok() ? "Ent roll  Esc back  add .txt to /gmtools/tables" : "Ent roll  Esc back  (no SD: built-ins only)");
    }

    void drawResult() {
        const Table& t = tables_[visible_[sel_]];
        gfx.setTextDatum(top_left);
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        gfx.setTextColor(C_ARCANE);
        String title = t.name;
        while (title.length() > 3 && gfx.textWidth(title) > W - 70) title = title.substring(0, title.length() - 1);
        gfx.drawString(title, 8, HEADER_H + 3);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(lastRoll_, W - 8, HEADER_H + 7);

        const int cy = HEADER_H + 22;
        if (results_.empty()) return;
        String r = results_[0];
        // biggest font that fits; long SRD entries fall back to small text and scroll
        std::vector<String> lines;
        const lgfx::IFont* fontsToTry[] = {&fonts::FreeSerif9pt7b, &fonts::Font2, &fonts::Font0};
        int maxH = H - FOOTER_H - cy - 4;
        int lh = 10;
        for (auto f : fontsToTry) {
            gfx.setFont(f);
            lines.clear();
            wrapLines(r, W - 20, lines);
            lh = gfx.fontHeight() + 1;
            if ((int)lines.size() * lh + 8 <= std::min(maxH, 60) || f == &fonts::Font0) break;
        }
        int visible = std::max(1, (maxH - 8) / lh);
        int maxScroll = std::max(0, (int)lines.size() - visible);
        if (scroll_ > maxScroll) scroll_ = maxScroll;
        int ch = std::min(maxH, (int)lines.size() * lh + 8);
        drawPanel(4, cy, W - 8, ch, C_PANEL, C_ARCANE);
        int chars = (int)(r.length() * easeOut(reveal_));
        int y = cy + 4;
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_TEXT);
        for (int i = 0; i < (int)lines.size(); i++) {
            const String& l = lines[i];
            if (chars <= 0) break;
            String vis = l.substring(0, std::min((int)l.length(), chars));
            chars -= l.length() + 1;
            if (i < scroll_ || i >= scroll_ + visible) continue;
            gfx.drawString(vis, 10, y);
            y += lh;
        }
        if (maxScroll) {
            int sh = std::max(5, (ch - 8) * visible / (int)lines.size());
            gfx.fillRect(W - 8, cy + 4 + (ch - 8 - sh) * scroll_ / maxScroll, 2, sh, C_ARCANE);
        }
        // previous results, dimmer, if there's room
        gfx.setFont(&fonts::Font0);
        gfx.setTextColor(C_DIM);
        y = cy + ch + 4;
        for (size_t i = 1; i < results_.size() && y < H - FOOTER_H - 9; i++, y += 10) {
            String p = results_[i];
            while (p.length() > 3 && gfx.textWidth(p) > W - 16) p = p.substring(0, p.length() - 4) + "...";
            gfx.drawString(p, 8, y);
        }
        drawFooter(maxScroll ? "Ent roll again  Up/Dn scroll  Esc" : "Ent roll again  Esc back");
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

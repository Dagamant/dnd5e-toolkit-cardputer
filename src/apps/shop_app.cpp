#include "../core/app.h"
#include "../core/db.h"
#include "../core/hold_menu.h"
#include "../core/touch.h"
#include "../core/prompt.h"
#include "../core/rng.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/textview.h"
#include "../core/ui.h"
#include "../core/web.h"
#include "../core/widgets.h"
#include <vector>

using namespace ui;

// Magic shop & crafting calculators from the SRD:
// Magic Item Rarities and Values, Magic Item Crafting Time and Cost,
// Spell Scroll Costs, Spellcasting Services and nonmagical crafting.
// The shop is stocked from the SRD magic items (/gmtools/srd/magic.idx) and
// spells (for Spell Scrolls), and kept on the SD card between visits.

namespace {

const char* const RARITY[] = {"Common", "Uncommon", "Rare", "Very Rare", "Legendary"};
const long VALUE[] = {100, 400, 4000, 40000, 200000};
const int CRAFT_DAYS[] = {5, 10, 50, 125, 250};
const long CRAFT_GP[] = {50, 200, 2000, 20000, 100000};
// spell levels 0..9
const int SCROLL_DAYS[] = {1, 1, 3, 5, 10, 25, 40, 50, 60, 120};
const long SCROLL_GP[] = {15, 25, 100, 150, 1000, 1500, 10000, 12500, 15000, 50000};
const int SCROLL_RARITY[] = {0, 0, 1, 1, 2, 2, 3, 3, 3, 4};
const int SCROLL_DC[] = {13, 13, 13, 15, 15, 17, 17, 18, 18, 19};
const int SCROLL_ATK[] = {5, 5, 5, 7, 7, 9, 9, 10, 10, 11};
const long SERVICE_GP[] = {30, 50, 200, 300, 2000, 2000, 20000, 20000, 20000, 100000};
const char* const SERVICE_WHERE[] = {"Village+", "Village+", "Village+", "Town+", "Town+", "Town+", "City", "City", "City", "City"};
const char* const MODES[] = {"Magic item price", "Spell scroll", "Nonmagical craft", "Random shop"};
const char* const SETTLE[] = {"Village", "Town", "City"};
// what each kind of shop sells (SRD item types), and how often a Spell Scroll
const char* const KINDS[] = {"General", "Alchemist", "Arms & Armor", "Arcanist", "Curiosities"};
const char* const KIND_TYPES[] = {"", "Potion", "Armor,Weapon", "Ring,Rod,Staff,Wand", "Wondrous Item"};
const int KIND_SCROLLS[] = {15, 0, 0, 45, 0};   // percent
constexpr int NKINDS = 5;
const char* const SHOP_FILE = "/gmtools/shop.txt";

uint16_t rarityColor(int r) {
    static const uint16_t C[] = {C_TEXT, C_EMERALD, C_ICE, C_ARCANE, C_GOLD};
    return C[r < 0 || r > 4 ? 0 : r];
}

String gp(long v) {
    String s = String(v), o;
    int n = s.length();
    for (int i = 0; i < n; i++) { o += s[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) o += ','; }
    return o + " GP";
}

// An asking price: rounded the way a shopkeeper would.
long roundPrice(long v) {
    const long step = v < 100 ? 1 : v < 1000 ? 5 : v < 10000 ? 50 : 500;
    return std::max(1L, (v + step / 2) / step * step);
}

struct Stock {
    String name, type;
    int rarity = 0;
    long price = 0;
    bool attune = false, sold = false;
    int spellLevel = -1;   // a Spell Scroll: the spell's level
};

class ShopApp : public App {
public:
    const char* name() const override { return "Magic Shop"; }
    const char* blurb() const override { return "Prices, crafting, scrolls, stock."; }
    uint16_t accent() const override { return rgb(200, 150, 255); }
    void drawIcon(int cx, int cy, float t) override {
        // coin stack + sparkle
        for (int i = 0; i < 4; i++) {
            gfx.fillEllipse(cx - u(4), cy + u(10) - i * u(5), u(12), u(4), C_GOLD_DK);
            gfx.drawEllipse(cx - u(4), cy + u(10) - i * u(5), u(12), u(4), C_GOLD);
        }
        float p = 0.5f + 0.5f * sinf(t * 5);
        int r = 3 + (int)(4 * p);
        gfx.drawLine(cx + u(12) - r, cy - u(12), cx + u(12) + r, cy - u(12), C_TEXT);
        gfx.drawLine(cx + u(12), cy - u(12) - r, cx + u(12), cy - u(12) + r, C_TEXT);
    }

    void onEnter() override {
        input::setTextMode(false);
        if (!loaded_) {
            loadStock();
            loaded_ = true;
            if (wide()) mode_ = 3;   // the big screen opens on the shelves
        }
    }

    bool handle(const input::Event& e) override {
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done()) price_ = std::max(0L, (long)prompt_.value().toInt());
            return true;
        }
        if (view_ >= 0) {
            if (e.key == input::K_BACK || e.key == input::K_ENTER) { view_ = -1; sfx::back(); }
            else if (e.key == input::K_UP) tv_.scroll(-1);
            else if (e.key == input::K_DOWN) tv_.scroll(1);
            return true;
        }
        int d = 0;
        if (e.key == input::K_LEFT || (e.key == input::K_CHAR && e.base == '-')) d = -1;
        if (e.key == input::K_RIGHT || (e.key == input::K_CHAR && e.base == '=')) d = 1;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_TAB: mode_ = (mode_ + 1) % 4; sel_ = 0; sfx::click(); return true;
            case input::K_UP: sel_ = std::max(0, sel_ - 1); sfx::click(); return true;
            case input::K_DOWN: sel_ = std::min(rows() - 1, sel_ + 1); sfx::click(); return true;
            case input::K_ENTER:
                if (mode_ == 2) prompt_.open("Item price in GP", String(price_), 1, 7);
                if (mode_ == 3) generate();
                return true;
            default: break;
        }
        if (!d) return true;
        if (mode_ == 0) {
            if (sel_ == 0) rarity_ = (rarity_ + d + 5) % 5;
            else consumable_ = !consumable_;
        } else if (mode_ == 1) level_ = (level_ + d + 10) % 10;
        else if (mode_ == 2) price_ = std::max(0L, price_ + d * 10L);
        else if (mode_ == 3) {
            if (sel_ == 0) settle_ = (settle_ + d + 3) % 3;
            else count_ = std::max(1, std::min(MAX_STOCK, count_ + d));
        }
        sfx::click();
        return true;
    }

    bool bigLayout() const override { return true; }

    void draw() override {
        drawBackground();
        drawHeader("Magic Shop", accent());
        if (wide()) { drawWide(); return; }
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(MODES[mode_]) + " (Tab)", W - u(30), HEADER_H / 2);
        y_ = HEADER_H + u(4);
        switch (mode_) {
            case 0: {
                field(0, "Rarity", String("< ") + RARITY[rarity_] + " >");
                field(1, "Consumable?", consumable_ ? "Yes (halved)" : "No");
                long v = VALUE[rarity_] / (consumable_ ? 2 : 1);
                y_ += u(4);
                result("Typical price", gp(v), C_GOLD);
                result("Craft time", String(CRAFT_DAYS[rarity_] / (consumable_ ? 2 : 1)) + " days", C_EMERALD);
                result("Craft cost", gp(CRAFT_GP[rarity_] / (consumable_ ? 2 : 1)), C_EMERALD);
                note("Crafting needs the right tool and Arcana; Spell Scrolls use their own costs (Tab).");
                break;
            }
            case 1: {
                field(0, "Spell level", level_ ? String("< ") + level_ + " >" : String("< Cantrip >"));
                y_ += u(4);
                result("Scribe time", String(SCROLL_DAYS[level_]) + (SCROLL_DAYS[level_] == 1 ? " day" : " days"), C_EMERALD);
                result("Scribe cost", gp(SCROLL_GP[level_]), C_EMERALD);
                result("Scroll value", gp(VALUE[SCROLL_RARITY[level_]] / 2), C_GOLD);
                result("Hire a caster", gp(SERVICE_GP[level_]) + " (" + SERVICE_WHERE[level_] + ")", C_ICE);
                note("Scribing needs Arcana or Calligrapher's Supplies and the spell prepared each day.");
                break;
            }
            case 2: {
                field(0, "Item price (Ent)", gp(price_));
                y_ += u(4);
                result("Raw materials", gp(price_ / 2), C_GOLD);
                long days = (price_ + 9) / 10;
                result("Crafting time", String(days) + (days == 1 ? " day" : " days") + " (8 h)", C_EMERALD);
                note("Needs proficiency with the required tool. Helpers with the same tool split the days.");
                break;
            }
            case 3: {
                field(0, "Settlement", String("< ") + SETTLE[settle_] + " >");
                field(1, "Items", String("< ") + count_ + " >");
                gfx.setTextDatum(top_left);
                gfx.setTextColor(C_DIM);
                if (stock_.empty()) gfx.drawString("Enter: stock the shelves", u(6), y_ + u(4));
                y_ += u(2);
                for (auto& s : stock_) {
                    if (y_ > H - FOOTER_H - u(10)) break;
                    String n = s.name + " - " + gp(s.price);
                    while (n.length() > 3 && gfx.textWidth(n) > W - u(12)) n = n.substring(0, n.length() - 1);
                    gfx.setTextColor(s.sold ? C_DIM : C_TEXT);
                    gfx.drawString(n, u(6), y_);
                    y_ += u(10);
                }
                break;
            }
        }
        drawFooter(mode_ == 3 ? "Ent restock  </> change  Tab mode" : mode_ == 2 ? "Ent type price  </> +-10  Tab mode" : "</> change  Tab next calculator");
        prompt_.draw("Whole GP");
    }

private:
    static constexpr int MAX_STOCK = 10;
    int mode_ = 0, sel_ = 0, rarity_ = 1, level_ = 1, settle_ = 1, count_ = 6, kind_ = 0, y_ = 0;
    int crafters_ = 1;
    bool consumable_ = false, loaded_ = false;
    long price_ = 15;
    std::vector<Stock> stock_;
    Prompt prompt_;
    int view_ = -1;   // the shelf item being read, -1 none
    TextView tv_;

    int rows() const { return mode_ == 0 || mode_ == 3 ? 2 : 1; }

    // ---- the Cardputer's one-column calculators
    void field(int i, const char* label, const String& v) {
        bool s = i == sel_;
        const int rh = touchRow(12), ty = y_ + touchPad(12);
        if (s) gfx.fillRoundRect(u(2), y_ - u(1), W - u(4), rh - u(1), u(2), C_PANEL);
        touch::field(0, y_ - u(1), W, rh, sel_, i);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(s ? C_TEXT : rgb(200, 195, 215));
        gfx.drawString(label, u(6), ty);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(v, W - u(6), ty);
        y_ += rh;
    }
    void result(const char* label, const String& v, uint16_t c) {
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString(label, u(10), y_);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(c);
        gfx.drawString(v, W - u(8), y_);
        y_ += u(11);
    }
    void note(const char* s) {
        gfx.setTextColor(C_DIM);
        y_ = textWrapped(s, u(6), y_ + u(4), W - u(12), u(9), 3);
    }

    // ---- stock
    struct Pick { String name, type; int rarity; bool attune; };

    static int rarityOf(const String& r) {
        for (int i = 0; i < 5; i++) if (r.equalsIgnoreCase(RARITY[i])) return i;
        return -1;
    }

    // The SRD magic items a shop of this kind sells, by rarity.
    void loadPool(std::vector<Pick> (&pool)[5]) {
        std::vector<String> lines;
        storage::readLines("/gmtools/srd/magic.idx", lines, 600);
        const String types = KIND_TYPES[kind_];
        for (auto& l : lines) {
            // offset, name, kind, type, rarity, attune
            String f[6];
            int at = 0;
            for (int i = 0; i < 6; i++) {
                int tab = l.indexOf('\t', at);
                f[i] = tab < 0 ? l.substring(at) : l.substring(at, tab);
                if (tab < 0) break;
                at = tab + 1;
            }
            const int r = rarityOf(f[4]);
            if (r < 0 || f[3] == "Scroll") continue;   // scrolls are made below, one per spell
            if (types.length() && (String(",") + types + ",").indexOf("," + f[3] + ",") < 0) continue;
            pool[r].push_back({f[1], f[3], r, f[5].length() > 0});
        }
    }

    // A Spell Scroll of a random spell of the right level for rarity r.
    bool scrollFor(int r, Stock& out) {
        static std::vector<String> spells;
        if (spells.empty()) storage::readLines("/gmtools/srd/spells.idx", spells, 600);
        int lv[3], n = 0;
        for (int l = 0; l < 10; l++) if (SCROLL_RARITY[l] == r && n < 3) lv[n++] = l;
        if (!n || spells.empty()) return false;
        const int want = lv[rng::below(n)];
        std::vector<int> fit;
        for (size_t i = 0; i < spells.size(); i++) {
            const String& l = spells[i];
            int a = l.indexOf('\t'), b = l.indexOf('\t', a + 1), c = l.indexOf('\t', b + 1);
            if (a < 0 || b < 0) continue;
            if (l.substring(b + 1, c < 0 ? l.length() : c).toInt() == want) fit.push_back(i);
        }
        if (fit.empty()) return false;
        const String& l = spells[fit[rng::below(fit.size())]];
        int a = l.indexOf('\t'), b = l.indexOf('\t', a + 1);
        out.name = "Spell Scroll (" + l.substring(a + 1, b) + ")";
        out.type = "Scroll";
        out.rarity = r;
        out.attune = false;
        out.spellLevel = want;
        return true;
    }

    static bool consumable(const Stock& s) {
        return s.type == "Potion" || s.type == "Scroll" || s.name.startsWith("Ammunition") || s.name.startsWith("Oil");
    }

    // One item for the shelf, not already on it.
    bool rollItem(std::vector<Pick> (&pool)[5], Stock& out) {
        // village: Common and Uncommon; town: up to Rare; city: up to Very Rare
        static const int weights[3][5] = {{6, 4, 0, 0, 0}, {3, 5, 2, 0, 0}, {2, 4, 3, 1, 0}};
        const int scrollPct = KIND_SCROLLS[kind_];
        for (int tries = 0; tries < 12; tries++) {
            int w[5], total = 0;
            for (int r = 0; r < 5; r++) {
                const bool any = !pool[r].empty() || scrollPct;   // scrolls come in every rarity
                w[r] = any ? weights[settle_][r] : 0;
                total += w[r];
            }
            if (!total) return false;
            int roll = rng::below(total), r = 0;
            while (roll >= w[r]) roll -= w[r++];
            Stock s;
            const bool scroll = scrollPct && ((int)rng::below(100) < scrollPct || pool[r].empty());
            if (scroll) {
                if (!scrollFor(r, s)) continue;
            } else {
                if (pool[r].empty()) continue;
                const Pick& p = pool[r][rng::below(pool[r].size())];
                s.name = p.name;
                s.type = p.type;
                s.rarity = p.rarity;
                s.attune = p.attune;
            }
            bool dup = false;
            for (auto& x : stock_) dup |= x.name == s.name;
            if (dup) continue;
            // asking 75%-150% of the typical value; consumables half
            long v = VALUE[s.rarity] * (75 + rng::below(76)) / 100;
            if (consumable(s)) v /= 2;
            s.price = roundPrice(v);
            out = s;
            return true;
        }
        return false;
    }

    // The web page's shop: the shelves, restocking, and the price guides.
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (!loaded_) { loadStock(); loaded_ = true; }
        const int i = c.num("i", -1);
        const bool valid = i >= 0 && i < (int)stock_.size() && stock_[i].name == c.arg("name");
        if (c.op == "set") {
            settle_ = std::max(0, std::min(2, c.num("settle", settle_)));
            kind_ = std::max(0, std::min(NKINDS - 1, c.num("kind", kind_)));
            count_ = std::max(1, std::min(MAX_STOCK, c.num("count", count_)));
            saveStock();
        } else if (c.op == "restock") {
            generate();
            if (stock_.empty()) { out = "No SRD magic items on the SD card"; return 400; }
        } else if (c.op == "swap" || c.op == "sold") {
            if (!valid) { out = "That item changed - reload"; return 409; }
            if (c.op == "swap") swapItem(i);
            else { stock_[i].sold = !stock_[i].sold; saveStock(); }
        } else if (c.op != "state") return 404;
        auto list = [](const char* const* v, int n) {
            String o = "[";
            for (int k = 0; k < n; k++) o += String(k ? "," : "") + web::js(v[k]);
            return o + "]";
        };
        auto nums = [](const long* v, int n) {
            String o = "[";
            for (int k = 0; k < n; k++) o += String(k ? "," : "") + v[k];
            return o + "]";
        };
        auto ints = [](const int* v, int n) {
            String o = "[";
            for (int k = 0; k < n; k++) o += String(k ? "," : "") + v[k];
            return o + "]";
        };
        out = String("{\"settle\":") + settle_ + ",\"kind\":" + kind_ + ",\"count\":" + count_ + ",\"max\":" + MAX_STOCK +
              ",\"settles\":" + list(SETTLE, 3) + ",\"kinds\":" + list(KINDS, NKINDS) + ",\"rarities\":" + list(RARITY, 5) +
              ",\"value\":" + nums(VALUE, 5) + ",\"craftDays\":" + ints(CRAFT_DAYS, 5) + ",\"craftGp\":" + nums(CRAFT_GP, 5) +
              ",\"scrollDays\":" + ints(SCROLL_DAYS, 10) + ",\"scrollGp\":" + nums(SCROLL_GP, 10) + ",\"scrollDc\":" + ints(SCROLL_DC, 10) +
              ",\"scrollAtk\":" + ints(SCROLL_ATK, 10) + ",\"serviceGp\":" + nums(SERVICE_GP, 10) + ",\"serviceWhere\":" + list(SERVICE_WHERE, 10) +
              ",\"stock\":[";
        for (size_t k = 0; k < stock_.size(); k++) {
            const Stock& st = stock_[k];
            out += String(k ? "," : "") + "{\"name\":" + web::js(st.name) + ",\"type\":" + web::js(st.type) + ",\"rarity\":" + st.rarity +
                   ",\"price\":" + st.price + ",\"attune\":" + (st.attune ? "true" : "false") + ",\"sold\":" + (st.sold ? "true" : "false") +
                   ",\"spell\":" + st.spellLevel + "}";
        }
        out += "]}";
        return 200;
    }

    void generate() {
        std::vector<Pick> pool[5];
        loadPool(pool);
        stock_.clear();
        for (int i = 0; i < count_; i++) {
            Stock s;
            if (rollItem(pool, s)) stock_.push_back(s);
        }
        if (stock_.empty()) { sfx::error(); toast("No SRD magic items on the SD card", C_BLOOD); return; }
        // cheapest first, as on a shelf
        std::sort(stock_.begin(), stock_.end(), [](const Stock& a, const Stock& b) { return a.price < b.price; });
        saveStock();
        sfx::diceRattle();
    }

    void swapItem(int i) {
        std::vector<Pick> pool[5];
        loadPool(pool);
        Stock s;
        if (!rollItem(pool, s)) { sfx::error(); return; }
        stock_[i] = s;
        saveStock();
        sfx::select();
    }

    // settle kind count, then one item a line: name|type|rarity|price|attune|sold|spell level
    void saveStock() {
        if (!storage::ok()) return;
        String out = String(settle_) + " " + kind_ + " " + count_ + "\n";
        for (auto& s : stock_)
            out += s.name + "|" + s.type + "|" + s.rarity + "|" + s.price + "|" + (s.attune ? 1 : 0) + "|" + (s.sold ? 1 : 0) +
                   "|" + s.spellLevel + "\n";
        storage::writeText(SHOP_FILE, out);
    }

    void loadStock() {
        std::vector<String> lines;
        if (!storage::readLines(SHOP_FILE, lines, MAX_STOCK + 1) || lines.empty()) return;
        sscanf(lines[0].c_str(), "%d %d %d", &settle_, &kind_, &count_);
        settle_ = std::max(0, std::min(2, settle_));
        kind_ = std::max(0, std::min(NKINDS - 1, kind_));
        count_ = std::max(1, std::min(MAX_STOCK, count_));
        for (size_t i = 1; i < lines.size(); i++) {
            String f[7];
            int at = 0;
            for (int k = 0; k < 7; k++) {
                int bar = lines[i].indexOf('|', at);
                f[k] = bar < 0 ? lines[i].substring(at) : lines[i].substring(at, bar);
                if (bar < 0) break;
                at = bar + 1;
            }
            if (!f[0].length()) continue;
            Stock s;
            s.name = f[0];
            s.type = f[1];
            s.rarity = std::max(0, std::min(4, (int)f[2].toInt()));
            s.price = f[3].toInt();
            s.attune = f[4] == "1";
            s.sold = f[5] == "1";
            s.spellLevel = f[6].length() ? f[6].toInt() : -1;
            stock_.push_back(s);
        }
    }

    // ---- the item being read: the SRD text
    void openItem(int i) {
        const Stock& s = stock_[i];
        tv_.clear();
        tv_.setWidth(INFO_W - 48);
        db::Rec r;
        if (s.spellLevel >= 0) {
            const int lv = s.spellLevel;
            const String spell = s.name.substring(s.name.indexOf('(') + 1, s.name.lastIndexOf(')'));
            tv_.add(String("Scroll, ") + RARITY[s.rarity] + "   " + (lv ? String("Level ") + lv : String("Cantrip")) +
                        "   Save DC " + SCROLL_DC[lv] + "   Attack +" + SCROLL_ATK[lv],
                    TextView::ACCENT);
            if (db::findRecord("/gmtools/srd/spells", spell, r)) {
                tv_.add(r.get("school") + "   " + r.get("time") + "   " + r.get("range") + "   " + r.get("duration"), TextView::DIM);
                tv_.gap();
                tv_.add(db::unescape(r.get("text")));
            }
        } else if (db::findRecord("/gmtools/srd/magic", s.name, r)) {
            tv_.add(r.get("typeline") + (s.attune ? " (requires attunement)" : ""), TextView::ACCENT);
            tv_.gap();
            tv_.add(db::unescape(r.get("text")));
        } else {
            tv_.add(String(RARITY[s.rarity]) + " " + s.type, TextView::ACCENT);
        }
        view_ = i;
        sfx::select();
    }

    void holdItem(int i) {
        const Stock& s = stock_[i];
        holdmenu::open(shown(s), {{"Read about it", [this, i] { openItem(i); }},
                                {s.sold ? "Back on the shelf" : "Sold", [this, i] {
                                     stock_[i].sold = !stock_[i].sold;
                                     saveStock();
                                     sfx::select();
                                 }, s.sold ? 0 : C_EMERALD},
                                {"Haggled: 10% off", [this, i] {
                                     stock_[i].price = roundPrice(stock_[i].price * 9 / 10);
                                     saveStock();
                                     sfx::click();
                                 }},
                                {"Swap for another", [this, i] { swapItem(i); }}});
    }

    // ---- big screen
    static constexpr int INFO_W = 1000;

    void drawWide() {
        using namespace widgets;
        // tabs: the shop first
        static const int ORDER[] = {3, 0, 1, 2};
        static const char* const TABS[] = {"Shop", "Item prices", "Spell scrolls", "Crafting"};
        const int tw = (W - 32 - 3 * 10) / 4, ty = HEADER_H + 10;
        for (int i = 0; i < 4; i++) {
            const int md = ORDER[i];
            button(16 + i * (tw + 10), ty, tw, 54, TABS[i], mode_ == md, accent(), [this, md] { mode_ = md; sel_ = 0; sfx::click(); });
        }
        const int top = ty + 54 + 16;
        switch (mode_) {
            case 0: drawPrices(top); break;
            case 1: drawScrolls(top); break;
            case 2: drawCrafting(top); break;
            default: drawShop(top); break;
        }
        drawFooter(mode_ == 3 ? "Tab next  Esc back  hold an item for more" : "Tab next  Esc back");
        if (view_ >= 0) drawItem();
        prompt_.draw("Whole GP");
    }

    void drawShop(int top) {
        using namespace widgets;
        const Metrics& m = metrics();
        const int LX = 16, LW = 380;
        int y = top;
        caption(LX, y, "SETTLEMENT");
        y += 26;
        const int sw = (LW - 2 * 8) / 3;
        for (int i = 0; i < 3; i++)
            button(LX + i * (sw + 8), y, sw, 54, SETTLE[i], settle_ == i, accent(), [this, i] { settle_ = i; sfx::click(); });
        y += 54 + 14;
        caption(LX, y, "SHOP");
        y += 26;
        const int kw = (LW - 8) / 2;
        for (int i = 0; i < NKINDS; i++)
            button(LX + (i % 2) * (kw + 8), y + (i / 2) * 58, kw, 50, KINDS[i], kind_ == i, accent(), [this, i] { kind_ = i; sfx::click(); });
        y += 3 * 58 + 6;
        stepper(LX, y, LW, 54, "Items", String(count_), [this](int d) { count_ = std::max(1, std::min(MAX_STOCK, count_ + d)); },
                0, nullptr, 110);
        y += 54 + 14;
        const int bh = H - FOOTER_H - 12 - y;
        gfx.fillRoundRect(LX, y, LW, bh, 14, C_BLOOD_DK);
        gfx.drawRoundRect(LX, y, LW, bh, 14, C_GOLD);
        gfx.setFont(m.title);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(stock_.empty() ? "Stock the shelves" : "Restock", LX + LW / 2, y + bh / 2);
        touch::zone(LX, y, LW, bh, [this] { generate(); });

        // the shelves
        const int RX = LX + LW + 16, RW = W - RX - 16, RY = top, RH = H - FOOTER_H - 10 - RY;
        drawPanel(RX, RY, RW, RH, C_BG2, C_EDGE);
        if (stock_.empty()) {
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Empty shelves.", RX + RW / 2, RY + RH / 2 - 20);
            gfx.drawString("Pick a settlement and a shop, then stock it.", RX + RW / 2, RY + RH / 2 + 20);
            return;
        }
        long left = 0;
        int sold = 0;
        for (auto& s : stock_) {
            if (s.sold) sold++;
            else left += s.price;
        }
        gfx.setFont(m.bold);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(accent());
        gfx.drawString(String(SETTLE[settle_]) + " " + KINDS[kind_], RX + 16, RY + 12);
        gfx.setFont(m.small);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(stock_.size() - sold) + " for sale, " + gp(left) + (sold ? String(", ") + sold + " sold" : String("")),
                       RX + RW - 16, RY + 16);
        const int gy = RY + 50, cols = 2, rowsN = (MAX_STOCK + 1) / cols;
        const int cw = (RW - 24 - 10) / cols, chh = (RY + RH - 12 - gy - (rowsN - 1) * 8) / rowsN;
        for (size_t i = 0; i < stock_.size(); i++) {
            const int cx = RX + 12 + (i % cols) * (cw + 10), cy = gy + (i / cols) * (chh + 8);
            drawCard(stock_[i], cx, cy, cw, chh);
            touch::zone(cx, cy, cw, chh, [this, i] { openItem(i); });
            touch::onHold(cx, cy, cw, chh, [this, i] { holdItem(i); });
        }
    }

    // "Spell Scroll (Fireball)" reads as "Scroll of Fireball" on a card
    static String shown(const Stock& s) {
        if (s.spellLevel < 0) return s.name;
        return "Scroll of " + s.name.substring(s.name.indexOf('(') + 1, s.name.lastIndexOf(')'));
    }

    void drawCard(const Stock& s, int x, int y, int w, int h) {
        const Metrics& m = metrics();
        const uint16_t rc = s.sold ? C_EDGE : rarityColor(s.rarity);
        gfx.fillRoundRect(x, y, w, h, 10, C_BG);
        gfx.drawRoundRect(x, y, w, h, 10, s.sold ? C_BG2 : C_EDGE);
        gfx.fillRoundRect(x + 6, y + 8, 6, h - 16, 3, rc);   // rarity stripe
        gfx.setFont(m.bold);
        // a long name: plainer and then smaller before it is cut short
        if (gfx.textWidth(shown(s)) > w - 34) gfx.setFont(m.body);
        if (gfx.textWidth(shown(s)) > w - 34) gfx.setFont(m.small);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(s.sold ? C_EDGE : C_TEXT);
        gfx.drawString(fitText(shown(s), w - 34), x + 22, y + 8);
        // rarity and kind on the left of the bottom line, the price on the right
        gfx.setFont(m.small);
        gfx.setTextDatum(bottom_right);
        gfx.setTextColor(s.sold ? C_EDGE : C_GOLD);
        const String price = s.sold ? String("SOLD") : gp(s.price);
        gfx.drawString(price, x + w - 12, y + h - 8);
        String line = String(RARITY[s.rarity]) + " " + (s.spellLevel >= 0 ? (s.spellLevel ? String("level ") + s.spellLevel : String("cantrip")) : s.type);
        if (s.attune) line += ", attune";
        gfx.setTextDatum(bottom_left);
        gfx.setTextColor(s.sold ? C_EDGE : rc);
        gfx.drawString(fitText(line, w - 50 - gfx.textWidth(price)), x + 22, y + h - 8);
    }

    void drawItem() {
        const Metrics& m = metrics();
        const Stock& s = stock_[view_];
        const int w = INFO_W, h = H - HEADER_H - FOOTER_H - 30, x = (W - w) / 2, y = HEADER_H + 15;
        touch::modal(x, y, w, h);
        gfx.fillRoundRect(x + 8, y + 10, w, h, 14, C_BG);
        drawPanel(x, y, w, h, C_BG2, rarityColor(s.rarity));
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(rarityColor(s.rarity));
        gfx.drawString(fitText(shown(s), w - 300), x + 24, y + 16);
        gfx.setFont(m.bold);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(s.sold ? C_DIM : C_GOLD);
        gfx.drawString(s.sold ? String("Sold") : gp(s.price), x + w - 24, y + 22);
        gfx.drawFastHLine(x + 16, y + 68, w - 32, C_EDGE);
        tv_.draw(x + 24, y + 80, h - 80 - 84, rarityColor(s.rarity));
        const int bw = 220, by = y + h - 70;
        const int i = view_;
        widgets::button(x + w - 24 - bw, by, bw, 54, "Close", false, C_DIM, [this] { view_ = -1; sfx::back(); });
        widgets::button(x + w - 24 - 2 * bw - 10, by, bw, 54, s.sold ? "Back on shelf" : "Sold", false, C_EMERALD, [this, i] {
            stock_[i].sold = !stock_[i].sold;
            saveStock();
            view_ = -1;
            sfx::select();
        });
    }

    void drawPrices(int top) {
        using namespace widgets;
        const int X = 16, WW = W - 32;
        int y = top;
        caption(X, y, "RARITY");
        y += 26;
        const int rw = (WW - 4 * 10) / 5;
        for (int i = 0; i < 5; i++)
            button(X + i * (rw + 10), y, rw, 64, RARITY[i], rarity_ == i, rarityColor(i), [this, i] { rarity_ = i; sfx::click(); });
        y += 64 + 14;
        button(X, y, 520, 56, consumable_ ? "Consumable: yes (half)" : "Consumable: no", consumable_, C_GOLD,
               [this] { consumable_ = !consumable_; sfx::click(); });
        y += 56 + 16;
        const int k = consumable_ ? 2 : 1, cw = (WW - 2 * 10) / 3, ch = 170;
        stat(X, y, cw, ch, "TYPICAL PRICE", gp(VALUE[rarity_] / k), C_GOLD, "to buy or sell");
        stat(X + cw + 10, y, cw, ch, "CRAFT TIME", String(CRAFT_DAYS[rarity_] / k) + " days", C_EMERALD, "8 hours a day");
        stat(X + 2 * (cw + 10), y, cw, ch, "CRAFT COST", gp(CRAFT_GP[rarity_] / k), C_EMERALD, "in raw materials");
        y += ch + 16;
        gfx.setFont(metrics().body);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        textWrapped("Crafting a magic item needs Arcana proficiency and the tool for the item, and a formula. Potions and "
                    "Spell Scrolls are consumables: half the time and cost. Spell Scrolls have their own tab.",
                    X + 4, y, WW - 8, metrics().line + 6, 3);
    }

    void drawScrolls(int top) {
        using namespace widgets;
        const int X = 16, WW = W - 32;
        int y = top;
        caption(X, y, "SPELL LEVEL");
        y += 26;
        const int bw = (WW - 9 * 8) / 10;
        for (int i = 0; i < 10; i++)
            button(X + i * (bw + 8), y, bw, 64, i ? String(i) : String("Cantrip"), level_ == i, accent(), [this, i] { level_ = i; sfx::click(); });
        y += 64 + 16;
        const int cw = (WW - 3 * 10) / 4, ch = 170, r = SCROLL_RARITY[level_];
        stat(X, y, cw, ch, "SCRIBE", String(SCROLL_DAYS[level_]) + (SCROLL_DAYS[level_] == 1 ? " day" : " days"), C_EMERALD, gp(SCROLL_GP[level_]));
        stat(X + cw + 10, y, cw, ch, "SCROLL", RARITY[r], rarityColor(r), String("worth ") + gp(VALUE[r] / 2));
        stat(X + 2 * (cw + 10), y, cw, ch, "SAVE DC", String(SCROLL_DC[level_]), C_ICE, String("attack +") + SCROLL_ATK[level_]);
        stat(X + 3 * (cw + 10), y, cw, ch, "HIRE A CASTER", gp(SERVICE_GP[level_]), C_GOLD, SERVICE_WHERE[level_]);
        y += ch + 16;
        gfx.setFont(metrics().body);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        textWrapped("Scribing needs Arcana or Calligrapher's Supplies, and the spell prepared on each day of work. A "
                    "caster for hire casts the spell for you, plus the cost of any consumed material component.",
                    X + 4, y, WW - 8, metrics().line + 6, 3);
    }

    void drawCrafting(int top) {
        using namespace widgets;
        const int X = 16, WW = W - 32;
        int y = top;
        stepper(X, y, 820, 60, "Item price", gp(price_), [this](int d) {
            price_ = std::max(0L, price_ + (d == 1 || d == -1 ? d * 10L : (long)d));
        }, 100, [this] { prompt_.open("Item price in GP", String(price_), 1, 7); }, 220);
        y += 60 + 12;
        stepper(X, y, 820, 60, "Crafters", String(crafters_), [this](int d) { crafters_ = std::max(1, std::min(10, crafters_ + d)); },
                0, nullptr, 220);
        y += 60 + 20;
        const long days = (price_ + 9) / 10, split = (days + crafters_ - 1) / crafters_;
        const int cw = (WW - 2 * 10) / 3, ch = 170;
        stat(X, y, cw, ch, "RAW MATERIALS", gp(price_ / 2), C_GOLD, "half the price");
        stat(X + cw + 10, y, cw, ch, "TIME", String(split) + (split == 1 ? " day" : " days"), C_EMERALD,
             crafters_ > 1 ? String(crafters_) + " crafters" : String("8 hours a day"));
        stat(X + 2 * (cw + 10), y, cw, ch, "ALONE", String(days) + (days == 1 ? " day" : " days"), C_DIM, "10 GP of work a day");
        y += ch + 16;
        gfx.setFont(metrics().body);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        textWrapped("Nonmagical items: each crafter needs proficiency with the tool the item calls for. Helpers with the "
                    "same tool split the days.", X + 4, y, WW - 8, metrics().line + 6, 3);
    }
};

}  // namespace

App* makeShopApp() { return new ShopApp(); }

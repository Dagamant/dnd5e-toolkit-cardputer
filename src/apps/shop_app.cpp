#include "../core/app.h"
#include "../core/prompt.h"
#include "../core/rng.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include <vector>

using namespace ui;

// Magic shop & crafting calculators from the SRD:
// Magic Item Rarities and Values, Magic Item Crafting Time and Cost,
// Spell Scroll Costs, Spellcasting Services and nonmagical crafting.

namespace {

const char* const RARITY[] = {"Common", "Uncommon", "Rare", "Very Rare", "Legendary"};
const long VALUE[] = {100, 400, 4000, 40000, 200000};
const int CRAFT_DAYS[] = {5, 10, 50, 125, 250};
const long CRAFT_GP[] = {50, 200, 2000, 20000, 100000};
// spell levels 0..9
const int SCROLL_DAYS[] = {1, 1, 3, 5, 10, 25, 40, 50, 60, 120};
const long SCROLL_GP[] = {15, 25, 100, 150, 1000, 1500, 10000, 12500, 15000, 50000};
const long SERVICE_GP[] = {30, 50, 200, 300, 2000, 2000, 20000, 20000, 20000, 100000};
const char* const SERVICE_WHERE[] = {"Village+", "Village+", "Village+", "Town+", "Town+", "Town+", "City", "City", "City", "City"};
const char* const MODES[] = {"Magic item price", "Spell scroll", "Nonmagical craft", "Random shop"};
const char* const SETTLE[] = {"Village", "Town", "City"};

String gp(long v) {
    String s = String(v), o;
    int n = s.length();
    for (int i = 0; i < n; i++) { o += s[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) o += ','; }
    return o + " GP";
}

class ShopApp : public App {
public:
    const char* name() const override { return "Magic Shop"; }
    const char* blurb() const override { return "Prices, crafting, scrolls, stock."; }
    uint16_t accent() const override { return rgb(200, 150, 255); }
    void drawIcon(int cx, int cy, float t) override {
        // coin stack + sparkle
        for (int i = 0; i < 4; i++) {
            gfx.fillEllipse(cx - 4, cy + 10 - i * 5, 12, 4, C_GOLD_DK);
            gfx.drawEllipse(cx - 4, cy + 10 - i * 5, 12, 4, C_GOLD);
        }
        float p = 0.5f + 0.5f * sinf(t * 5);
        int r = 3 + (int)(4 * p);
        gfx.drawLine(cx + 12 - r, cy - 12, cx + 12 + r, cy - 12, C_TEXT);
        gfx.drawLine(cx + 12, cy - 12 - r, cx + 12, cy - 12 + r, C_TEXT);
    }

    void onEnter() override { input::setTextMode(false); }

    bool handle(const input::Event& e) override {
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done()) price_ = std::max(0L, (long)prompt_.value().toInt());
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
            else count_ = std::max(1, std::min(10, count_ + d));
        }
        sfx::click();
        return true;
    }

    void draw() override {
        drawBackground();
        drawHeader("Magic Shop", accent());
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(C_DIM);
        gfx.drawString(String(MODES[mode_]) + " (Tab)", W - 30, HEADER_H / 2);
        y_ = HEADER_H + 4;
        switch (mode_) {
            case 0: {
                field(0, "Rarity", String("< ") + RARITY[rarity_] + " >");
                field(1, "Consumable?", consumable_ ? "Yes (halved)" : "No");
                long v = VALUE[rarity_] / (consumable_ ? 2 : 1);
                y_ += 4;
                result("Typical price", gp(v), C_GOLD);
                result("Craft time", String(CRAFT_DAYS[rarity_] / (consumable_ ? 2 : 1)) + " days", C_EMERALD);
                result("Craft cost", gp(CRAFT_GP[rarity_] / (consumable_ ? 2 : 1)), C_EMERALD);
                note("Crafting needs the right tool and Arcana; Spell Scrolls use their own costs (Tab).");
                break;
            }
            case 1: {
                field(0, "Spell level", level_ ? String("< ") + level_ + " >" : String("< Cantrip >"));
                y_ += 4;
                result("Scribe time", String(SCROLL_DAYS[level_]) + (SCROLL_DAYS[level_] == 1 ? " day" : " days"), C_EMERALD);
                result("Scribe cost", gp(SCROLL_GP[level_]), C_EMERALD);
                result("Scroll value", gp(SCROLL_GP[level_] * 2), C_GOLD);
                result("Hire a caster", gp(SERVICE_GP[level_]) + " (" + SERVICE_WHERE[level_] + ")", C_ICE);
                note("Scribing needs Arcana or Calligrapher's Supplies and the spell prepared each day.");
                break;
            }
            case 2: {
                field(0, "Item price (Ent)", gp(price_));
                y_ += 4;
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
                if (stock_.empty()) gfx.drawString("Enter: stock the shelves", 6, y_ + 4);
                y_ += 2;
                for (auto& s : stock_) {
                    if (y_ > H - FOOTER_H - 10) break;
                    String n = s;
                    while (n.length() > 3 && gfx.textWidth(n) > W - 12) n = n.substring(0, n.length() - 1);
                    gfx.setTextColor(C_TEXT);
                    gfx.drawString(n, 6, y_);
                    y_ += 10;
                }
                break;
            }
        }
        drawFooter(mode_ == 3 ? "Ent restock  </> change  Tab mode" : mode_ == 2 ? "Ent type price  </> +-10  Tab mode" : "</> change  Tab next calculator");
        prompt_.draw("Whole GP");
    }

private:
    int mode_ = 0, sel_ = 0, rarity_ = 1, level_ = 1, settle_ = 1, count_ = 6, y_ = 0;
    bool consumable_ = false;
    long price_ = 15;
    std::vector<String> stock_;
    Prompt prompt_;

    int rows() const { return mode_ == 0 || mode_ == 3 ? 2 : 1; }

    void field(int i, const char* label, const String& v) {
        bool s = i == sel_;
        if (s) gfx.fillRoundRect(2, y_ - 1, W - 4, 11, 2, C_PANEL);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(s ? C_TEXT : rgb(200, 195, 215));
        gfx.drawString(label, 6, y_);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(v, W - 6, y_);
        y_ += 12;
    }
    void result(const char* label, const String& v, uint16_t c) {
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString(label, 10, y_);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(c);
        gfx.drawString(v, W - 8, y_);
        y_ += 11;
    }
    void note(const char* s) {
        gfx.setTextColor(C_DIM);
        y_ = textWrapped(s, 6, y_ + 4, W - 12, 9, 3);
    }

    void generate() {
        // village: Common; town: up to Uncommon (some Rare); city: up to Rare (some Very Rare)
        static const int weights[3][5] = {{8, 2, 0, 0, 0}, {5, 4, 1, 0, 0}, {3, 4, 3, 1, 0}};
        stock_.clear();
        std::vector<String> lists[5];
        for (int r = 0; r < 5; r++) {
            String path = String("/gmtools/srd/tables/Magic_Item_(") + RARITY[r] + ").txt";
            storage::readLines(path.c_str(), lists[r], 120);
            for (auto& l : lists[r]) { int tab = l.indexOf('\t'); if (tab >= 0) l = l.substring(tab + 1); }
        }
        int total = 0;
        for (int r = 0; r < 5; r++) total += weights[settle_][r];
        for (int i = 0; i < count_; i++) {
            int roll = rng::below(total), r = 0;
            while (roll >= weights[settle_][r]) roll -= weights[settle_][r++];
            if (lists[r].empty()) { stock_.push_back(String(RARITY[r]) + " item (no SRD data)"); continue; }
            String it = lists[r][rng::below(lists[r].size())];
            bool dup = false;
            for (auto& x : stock_) if (x.startsWith(it.substring(0, it.indexOf(" (~")) + " -")) dup = true;
            if (dup && lists[r].size() > (size_t)count_) { i--; continue; }
            // vary the asking price: 75%-150% of the typical value
            long v = VALUE[r] * (75 + rng::below(76)) / 100;
            if (it.startsWith("Potion") || it.startsWith("Oil") || it.startsWith("Ammunition")) v /= 2;
            int paren = it.indexOf(" (~");
            if (paren > 0) it = it.substring(0, paren);
            stock_.push_back(it + " - " + gp(v));
        }
        sfx::diceRattle();
    }
};

}  // namespace

App* makeShopApp() { return new ShopApp(); }

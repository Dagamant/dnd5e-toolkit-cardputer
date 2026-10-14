#include "db_browser.h"

using namespace ui;

// Read-only SRD browsers: the Rules Glossary and the Gameplay Toolbox hazards
// (traps, poisons, contagions, environmental hazards).

namespace {

const char* const RULE_TAGS[] = {"All rules", "Condition", "Action", "Area of Effect", "Hazard", "Attitude", "Rule"};

class RulesBrowser : public DbBrowser {
public:
    const char* name() const override { return "Rules"; }
    const char* blurb() const override { return "SRD rules glossary & conditions."; }
    uint16_t accent() const override { return C_ICE; }
    void drawIcon(int cx, int cy, float t) override {
        // scales of judgement
        gfx.drawFastVLine(cx, cy - 16, 30, C_TEXT);
        gfx.fillRect(cx - 8, cy + 13, 16, 3, C_TEXT);
        float tilt = sinf(t * 2) * 3;
        int ly = cy - 12 + (int)tilt, ry = cy - 12 - (int)tilt;
        gfx.drawLine(cx - 16, ly, cx + 16, ry, C_GOLD);
        gfx.fillTriangle(cx - 22, ly + 10, cx - 10, ly + 10, cx - 16, ly, C_ICE);
        gfx.fillTriangle(cx + 10, ry + 10, cx + 22, ry + 10, cx + 16, ry, C_ICE);
    }

protected:
    int tag_ = 0;
    const char* title() const override { return "Rules Glossary"; }
    bool canCreate() const override { return false; }
    const char* detailFooter() const override { return "Up/Dn scroll  Space page  Esc back"; }
    void openTable() override {
        table_.open({{"/gmtools/srd/glossary", {"tag"}}}, nullptr, {"tag"});
    }
    bool passFilter(int i) const override { return tag_ == 0 || !strcmp(table_.col(i, "tag"), RULE_TAGS[tag_]); }
    String filterLabel() const override { return tag_ == 0 ? String(RULE_TAGS[0]) : String(RULE_TAGS[tag_]) + "s"; }
    void cycleFilter(int d) override { tag_ = (tag_ + d + 7) % 7; }
    void drawRow(int i, int y, bool s) override {
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(s ? C_TEXT : rgb(200, 205, 215));
        gfx.drawString(table_.at(i).name, 8, y + 1);
        const char* tg = table_.col(i, "tag");
        if (strcmp(tg, "Rule")) {
            gfx.setTextDatum(top_right);
            gfx.setTextColor(!strcmp(tg, "Condition") ? C_GOLD : C_DIM);
            gfx.drawString(tg, W - 8, y + 1);
        }
    }
    void buildDetail() override {
        if (cur_.get("tag") != "Rule") tv_.add(cur_.get("tag"), TextView::ACCENT);
        for (auto& para : splitParas(db::unescape(cur_.get("text")))) tv_.add(para);
    }
    void rollCurrent() override { tv_.page(1); }
    static std::vector<String> splitParas(const String& s) {
        std::vector<String> v;
        int start = 0;
        while (start <= (int)s.length()) {
            int nl = s.indexOf('\n', start);
            v.push_back(nl < 0 ? s.substring(start) : s.substring(start, nl));
            if (nl < 0) break;
            start = nl + 1;
        }
        return v;
    }
};

const char* const HAZ_CATS[] = {"All hazards", "Trap", "Poison", "Contagion", "Hazard"};

class HazardsBrowser : public DbBrowser {
public:
    const char* name() const override { return "Hazards"; }
    const char* blurb() const override { return "Traps, poisons, disease, weather."; }
    uint16_t accent() const override { return rgb(130, 200, 90); }
    void drawIcon(int cx, int cy, float t) override {
        // poison flask with bubbles
        uint16_t g = rgb(110, 210, 90);
        gfx.fillCircle(cx, cy + 6, 13, rgb(40, 90, 40));
        gfx.drawCircle(cx, cy + 6, 13, C_TEXT);
        gfx.fillRect(cx - 4, cy - 16, 8, 10, rgb(40, 90, 40));
        gfx.drawRect(cx - 4, cy - 16, 8, 10, C_TEXT);
        for (int i = 0; i < 3; i++) {
            int by = cy + 8 - ((int)(t * 20 + i * 7) % 18);
            gfx.drawCircle(cx - 5 + i * 5, by, 2, g);
        }
    }

protected:
    int cat_ = 0;
    const char* title() const override { return "Hazards"; }
    bool canCreate() const override { return false; }
    const char* detailFooter() const override { return "Ent roll damage  Up/Dn scroll  Esc"; }
    void openTable() override {
        table_.open({{"/gmtools/srd/toolbox", {"category", "subtype"}}}, nullptr, {"category", "subtype"});
    }
    bool passFilter(int i) const override { return cat_ == 0 || !strcmp(table_.col(i, "category"), HAZ_CATS[cat_]); }
    String filterLabel() const override { return cat_ == 0 ? String(HAZ_CATS[0]) : String(HAZ_CATS[cat_]) + "s"; }
    void cycleFilter(int d) override { cat_ = (cat_ + d + 5) % 5; }
    void drawRow(int i, int y, bool s) override {
        const char* c = table_.col(i, "category");
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        uint16_t dot = !strcmp(c, "Trap") ? C_BLOOD : !strcmp(c, "Poison") ? rgb(120, 210, 90) : !strcmp(c, "Contagion") ? C_GOLD : C_ICE;
        gfx.fillCircle(8, y + 4, 2, dot);
        gfx.setTextColor(s ? C_TEXT : rgb(200, 205, 195));
        gfx.drawString(table_.at(i).name, 14, y + 1);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(C_DIM);
        String sub = table_.col(i, "subtype");
        if (sub.length() > 18) sub = sub.substring(0, 17) + ".";
        gfx.drawString(sub, W - 8, y + 1);
    }
    void buildDetail() override {
        tv_.add(cur_.get("subtype"), TextView::ACCENT);
        String key;
        if (cur_.has("save")) key += cur_.get("save");
        if (cur_.has("damage")) key += String(key.length() ? "  " : "") + cur_.get("damage") + " " + cur_.get("dtype");
        if (key.length()) tv_.add(key, TextView::GOOD);
        if (cur_.has("detect")) tv_.add("Spot it: " + cur_.get("detect"), TextView::DIM);
        if (cur_.has("cost")) tv_.add("Cost: " + cur_.get("cost"), TextView::DIM);
        tv_.gap();
        String t = db::unescape(cur_.get("text"));
        int start = 0;
        while (start <= (int)t.length()) {
            int nl = t.indexOf('\n', start);
            tv_.add(nl < 0 ? t.substring(start) : t.substring(start, nl));
            if (nl < 0) break;
            start = nl + 1;
        }
    }
    void rollCurrent() override {
        if (!cur_.has("damage")) { toast(cur_.has("save") ? cur_.get("save").c_str() : "Nothing to roll", C_DIM); return; }
        rollui::show(cur_.name() + (cur_.has("save") ? " - " + cur_.get("save") : String("")),
                     {{"Damage", cur_.get("damage"), cur_.get("dtype")}});
    }
};

}  // namespace

App* makeRulesApp() { return new RulesBrowser(); }
App* makeHazardsApp() { return new HazardsBrowser(); }
DbBrowser* newRulesBrowser() { return new RulesBrowser(); }

#include "db_browser.h"
#include "items_app.h"

using namespace ui;

// Item Database: SRD equipment + magic items + custom items (/gmtools/custom/items.txt).

namespace {

const char* const FILTERS[] = {"All items", "Weapons", "Armor", "Gear", "Tools", "Magic", "Custom"};
const char* const DTYPES[] = {"Bludgeoning", "Piercing", "Slashing", "Acid", "Cold", "Fire", "Force",
                              "Lightning", "Necrotic", "Poison", "Psychic", "Radiant", "Thunder"};
const char* const RARITY[] = {"Common", "Uncommon", "Rare", "Very Rare", "Legendary", "Artifact"};

uint16_t rarityColor(const char* r) {
    if (!*r) return C_DIM;
    if (!strcmp(r, "Uncommon")) return C_EMERALD;
    if (!strcmp(r, "Rare")) return rgb(90, 150, 255);
    if (!strcmp(r, "Very Rare")) return rgb(190, 110, 255);
    if (!strcmp(r, "Legendary")) return C_GOLD;
    if (!strcmp(r, "Artifact")) return C_BLOOD;
    return C_TEXT;
}

std::vector<String> list(const char* const* a, int n) {
    std::vector<String> v;
    for (int i = 0; i < n; i++) v.push_back(a[i]);
    return v;
}

}  // namespace

const char* ItemsBrowser::title() const { return onPick ? "Add item" : "Items"; }

void ItemsBrowser::drawIcon(int cx, int cy, float t) {
    // treasure chest
    gfx.fillRoundRect(cx - 18, cy - 6, 36, 22, 2, rgb(120, 72, 36));
    gfx.fillRoundRect(cx - 18, cy - 16, 36, 12, 5, rgb(150, 92, 46));
    gfx.drawRoundRect(cx - 18, cy - 16, 36, 32, 3, C_GOLD);
    gfx.drawFastHLine(cx - 18, cy - 5, 36, C_GOLD);
    gfx.fillRect(cx - 3, cy - 8, 6, 8, C_GOLD);
    float p = 0.5f + 0.5f * sinf(t * 4);
    gfx.fillCircle(cx + 10, cy - 18, 1 + (int)(2 * p), C_TEXT);
}

void ItemsBrowser::openTable() {
    table_.open({{"/gmtools/srd/items", {"kind", "category", "cost", "weight"}},
                 {"/gmtools/srd/magic", {"kind", "type", "rarity", "attune"}}},
                "/gmtools/custom/items.txt", {"kind", "category", "cost", "weight", "type", "rarity", "attune"});
}

bool ItemsBrowser::passFilter(int i) const {
    const char* k = table_.col(i, "kind");
    switch (filter_) {
        case 1: return !strcmp(k, "weapon");
        case 2: return !strcmp(k, "armor") || !strcmp(k, "shield");
        case 3: return !strcmp(k, "gear");
        case 4: return !strcmp(k, "tool");
        case 5: return !strcmp(k, "magic") || *table_.col(i, "rarity");
        case 6: return table_.isCustom(i);
        default: return true;
    }
}

String ItemsBrowser::filterLabel() const { return FILTERS[filter_]; }
void ItemsBrowser::cycleFilter(int d) { filter_ = (filter_ + d + 7) % 7; }

void ItemsBrowser::drawRow(int i, int y, bool s) {
    const char* kind = table_.col(i, "kind");
    const char* rar = table_.col(i, "rarity");
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(top_left);
    uint16_t dot = !strcmp(kind, "weapon") ? C_BLOOD : !strcmp(kind, "armor") || !strcmp(kind, "shield") ? C_ICE :
                   !strcmp(kind, "tool") ? C_GOLD_DK : *rar ? rarityColor(rar) : C_DIM;
    gfx.fillCircle(8, y + 4, 2, dot);
    gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 210));
    String n = table_.at(i).name;
    if (n.length() > 27) n = n.substring(0, 26) + ".";
    gfx.drawString(n, 14, y + 1);
    gfx.setTextDatum(top_right);
    String right = *rar ? String(rar) : String(table_.col(i, "cost"));
    if (right == "Very Rare") right = "V.Rare";
    if (right == "Legendary") right = "Legend";
    gfx.setTextColor(*rar ? rarityColor(rar) : C_DIM);
    gfx.drawString(right, W - 8, y + 1);
    if (table_.isCustom(i)) { gfx.setTextColor(C_EMERALD); gfx.drawString("*", W - 52, y + 1); }
}

void ItemsBrowser::buildDetail() {
    const db::Rec& r = cur_;
    String kind = r.get("kind");
    auto line = [&](const char* label, const String& v, TextView::Style st = TextView::BODY) {
        if (v.length()) tv_.add(String(label) + v, st);
    };
    if (kind == "weapon") {
        tv_.add(r.get("category", "Custom") + " weapon" + (r.has("bonus") ? String("  +") + r.get("bonus") : String("")), TextView::ACCENT);
        String dmg = r.get("damage") + " " + r.get("dtype");
        if (r.has("versatile")) dmg += "  (two hands: " + r.get("versatile") + ")";
        line("Damage: ", dmg, TextView::GOOD);
        line("Properties: ", r.get("props"));
        line("Mastery: ", r.get("mastery"));
    } else if (kind == "armor" || kind == "shield") {
        tv_.add(kind == "shield" ? String("Shield") : r.get("category") + " armor", TextView::ACCENT);
        String ac = r.get("ac");
        if (!ac.length()) ac = r.get("ac_base") + (r.get("dex") == "yes" ? " + Dex" : r.get("dex") == "max2" ? " + Dex (max 2)" : "");
        if (r.has("acbonus")) ac += String("  (+") + r.get("acbonus") + " magic)";
        line("AC: ", ac, TextView::GOOD);
        line("Strength: ", r.get("str"));
        if (r.has("stealth")) tv_.add("Disadvantage on Stealth", TextView::BAD);
    } else if (kind == "tool") {
        tv_.add("Tool", TextView::ACCENT);
        line("Ability: ", r.get("ability"));
        line("Use: ", r.get("utilize"));
        line("Craft: ", r.get("craft"), TextView::DIM);
    } else if (kind == "magic") {
        tv_.add(r.get("typeline"), TextView::ACCENT);
    } else {
        tv_.add(kind == "consumable" ? "Consumable" : "Adventuring gear", TextView::ACCENT);
    }
    if (kind != "magic" && r.has("rarity")) line("Rarity: ", r.get("rarity") + (r.has("attune") ? " (attunement)" : ""), TextView::ACCENT);
    if (r.has("charges")) line("Charges: ", r.get("charges"));
    if (r.has("heal")) line("Heals: ", r.get("heal"), TextView::GOOD);
    if (r.has("damage") && kind != "weapon") line("Damage: ", r.get("damage") + " " + r.get("dtype"), TextView::GOOD);
    if (r.has("roll")) line(r.has("rolllabel") ? (r.get("rolllabel") + ": ").c_str() : "Roll: ", r.get("roll"), TextView::GOOD);
    String wc;
    if (r.has("weight")) wc += "Weight " + r.get("weight");
    if (r.has("cost")) wc += String(wc.length() ? "   " : "") + "Cost " + r.get("cost");
    if (wc.length()) tv_.add(wc, TextView::DIM);
    if (r.has("text")) { tv_.gap(); tv_.add(db::unescape(r.get("text"))); }
}

void ItemsBrowser::rollCurrent() {
    const db::Rec& r = cur_;
    String kind = r.get("kind");
    if (kind == "weapon") {
        int b = r.num("bonus");
        String bon = b ? String("+") + b : String("");
        std::vector<rollui::Line> lines = {{"Attack", "d20" + bon, "+ your bonus"}, {"Damage", r.get("damage") + bon, r.get("dtype")}};
        if (r.has("versatile")) lines.push_back({"Two-handed", r.get("versatile") + bon, r.get("dtype")});
        rollui::show(r.name(), lines);
    } else if (r.has("heal")) rollui::show(r.name(), {{"Healing", r.get("heal"), "HP"}});
    else if (r.has("damage")) rollui::show(r.name(), {{"Damage", r.get("damage"), r.get("dtype")}});
    else if (r.has("roll")) rollui::show(r.name(), {{r.get("rolllabel", "Roll"), r.get("roll"), ""}});
    else toast("Nothing to roll", C_DIM);
}

// ---------------------------------------------------------------- wizard
std::vector<Wizard::Step> ItemsBrowser::wizardSteps() {
    using W_ = Wizard;
    auto kindIs = [](const char* k) { return [=](const Wizard& w) { return w.is("kind", k); }; };
    auto kindIn = [](std::vector<const char*> ks) {
        return [=](const Wizard& w) { for (auto k : ks) if (w.is("kind", k)) return true; return false; };
    };
    return {
        {"name", "Item name", W_::TEXT, {}, "", false, 32},
        {"kind", "What kind of item?", W_::CHOICE, {"Weapon", "Armor", "Shield", "Consumable", "Magic item", "Gear", "Tool"}},
        // weapon
        {"category", "Weapon category", W_::CHOICE, {"Simple Melee", "Simple Ranged", "Martial Melee", "Martial Ranged"}, "", false, 32, kindIs("Weapon")},
        {"damage", "Damage dice", W_::DICE, {}, "e.g. 1d8 or 2d6", false, 12, kindIs("Weapon")},
        {"dtype", "Damage type", W_::CHOICE, list(DTYPES, 13), "", false, 32, kindIn({"Weapon"})},
        {"props", "Properties", W_::MULTI, {"Ammunition", "Finesse", "Heavy", "Light", "Loading", "Reach", "Thrown", "Two-Handed", "Versatile"},
         "Space to toggle. Pick none for a plain weapon.", true, 32, kindIs("Weapon")},
        {"versatile", "Two-handed damage", W_::DICE, {}, "Versatile damage, e.g. 1d10", false, 12,
         [](const Wizard& w) { return w.is("kind", "Weapon") && w.contains("props", "Versatile"); }},
        {"range", "Range (normal/long)", W_::TEXT, {}, "e.g. 20/60 or 80/320", false, 12,
         [](const Wizard& w) { return w.is("kind", "Weapon") && (w.contains("props", "Thrown") || w.contains("props", "Ammunition") || w.get("category").endsWith("Ranged")); }},
        {"mastery", "Weapon mastery", W_::CHOICE, {"None", "Cleave", "Graze", "Nick", "Push", "Sap", "Slow", "Topple", "Vex"}, "", false, 32, kindIs("Weapon")},
        // armor / shield
        {"acat", "Armor category", W_::CHOICE, {"Light", "Medium", "Heavy"}, "Light adds full Dex, Medium up to +2, Heavy none.", false, 32, kindIs("Armor")},
        {"ac_base", "Base AC", W_::NUMBER, {}, "e.g. 11 for leather, 16 for chain mail", false, 3, kindIs("Armor")},
        {"str", "Strength requirement", W_::NUMBER, {}, "Blank for none", true, 3, kindIs("Armor")},
        {"stealth", "Disadvantage on Stealth?", W_::YESNO, {}, "", false, 32, kindIs("Armor")},
        {"shield_ac", "Shield AC bonus", W_::NUMBER, {}, "Normally 2", false, 2, kindIs("Shield")},
        {"bonus", "Magic bonus", W_::CHOICE, {"None", "+1", "+2", "+3"}, "Added to attack & damage (weapons) or AC (armor, shields).", false, 32,
         kindIn({"Weapon", "Armor", "Shield"})},
        // consumables & magic items
        {"mtype", "Magic item type", W_::CHOICE, {"Wondrous Item", "Ring", "Rod", "Staff", "Wand", "Potion", "Scroll"}, "", false, 32, kindIs("Magic item")},
        {"effect", "What does it roll?", W_::CHOICE, {"Healing", "Damage", "Something else", "Nothing"}, "", false, 32, kindIn({"Consumable", "Magic item"})},
        {"rolldice", "Dice", W_::DICE, {}, "e.g. 2d4+2", false, 16,
         [](const Wizard& w) { return (w.is("kind", "Consumable") || w.is("kind", "Magic item")) && !w.is("effect", "Nothing"); }},
        {"rdtype", "Damage type", W_::CHOICE, list(DTYPES, 13), "", false, 32,
         [](const Wizard& w) { return (w.is("kind", "Consumable") || w.is("kind", "Magic item")) && w.is("effect", "Damage"); }},
        {"rolllabel", "Name for the roll", W_::TEXT, {}, "e.g. Lightning burst", false, 20,
         [](const Wizard& w) { return (w.is("kind", "Consumable") || w.is("kind", "Magic item")) && w.is("effect", "Something else"); }},
        {"charges", "Charges / uses", W_::NUMBER, {}, "Blank for none", true, 3, kindIn({"Consumable", "Magic item"})},
        {"rarity", "Rarity", W_::CHOICE, list(RARITY, 6), "", false, 32,
         [](const Wizard& w) { return w.is("kind", "Magic item") || (!w.is("bonus", "None") && w.has("bonus")); }},
        {"attune", "Requires attunement?", W_::YESNO, {}, "", false, 32,
         [](const Wizard& w) { return w.is("kind", "Magic item") || (!w.is("bonus", "None") && w.has("bonus")); }},
        // tool
        {"ability", "Tool ability", W_::CHOICE, {"Strength", "Dexterity", "Constitution", "Intelligence", "Wisdom", "Charisma"}, "", false, 32, kindIs("Tool")},
        {"utilize", "What can it do?", W_::TEXT, {}, "e.g. Pick a lock (DC 15)", true, 60, kindIs("Tool")},
        // common
        {"weight", "Weight", W_::TEXT, {}, "e.g. 3 lb. Blank if negligible.", true, 10},
        {"cost", "Cost", W_::TEXT, {}, "e.g. 15 GP", true, 12},
        {"text", "Description", W_::LONGTEXT, {}, "Optional notes and rules text.", true},
    };
}

void ItemsBrowser::presetWizard(const db::Rec& r) {
    String kind = r.get("kind");
    String wk = kind == "weapon" ? "Weapon" : kind == "armor" ? "Armor" : kind == "shield" ? "Shield" : kind == "tool" ? "Tool" :
                kind == "magic" ? "Magic item" : kind == "consumable" ? "Consumable" : "Gear";
    wiz_.preset("name", r.name());
    wiz_.preset("kind", wk);
    wiz_.preset("category", r.get("category"));
    wiz_.preset("damage", r.get("damage"));
    wiz_.preset("dtype", r.get("dtype"));
    String props = r.get("props");
    String sel;
    for (const char* p : {"Ammunition", "Finesse", "Heavy", "Light", "Loading", "Reach", "Thrown", "Two-Handed", "Versatile"})
        if (props.indexOf(p) >= 0) sel += String(sel.length() ? ", " : "") + p;
    wiz_.preset("props", sel);
    wiz_.preset("versatile", r.get("versatile"));
    wiz_.preset("range", r.get("range"));
    wiz_.preset("mastery", r.get("mastery", "None"));
    wiz_.preset("acat", r.get("category"));
    wiz_.preset("ac_base", r.get("ac_base"));
    wiz_.preset("str", r.get("str"));
    wiz_.preset("stealth", r.has("stealth") ? "Yes" : "No");
    wiz_.preset("shield_ac", r.get("ac_base", "2"));
    String b = r.has("bonus") ? r.get("bonus") : r.get("acbonus");
    wiz_.preset("bonus", b.length() ? "+" + b : String("None"));
    wiz_.preset("mtype", r.get("type"));
    String eff = r.has("heal") ? "Healing" : r.has("damage") && kind != "weapon" ? "Damage" : r.has("roll") ? "Something else" : "Nothing";
    wiz_.preset("effect", eff);
    wiz_.preset("rolldice", r.has("heal") ? r.get("heal") : r.has("roll") ? r.get("roll") : kind != "weapon" ? r.get("damage") : String(""));
    wiz_.preset("rdtype", r.get("dtype"));
    wiz_.preset("rolllabel", r.get("rolllabel"));
    wiz_.preset("charges", r.get("charges"));
    wiz_.preset("rarity", r.get("rarity"));
    wiz_.preset("attune", r.has("attune") ? "Yes" : "No");
    wiz_.preset("ability", r.get("ability"));
    wiz_.preset("utilize", r.get("utilize"));
    wiz_.preset("weight", r.get("weight"));
    wiz_.preset("cost", r.get("cost"));
    wiz_.preset("text", db::unescape(r.get("text")));
}

db::Rec ItemsBrowser::fromWizard() {
    const Wizard& w = wiz_;
    db::Rec r;
    String k = w.get("kind");
    r.set("name", w.get("name"));
    String bonus = w.is("bonus", "None") ? String("") : w.get("bonus").substring(1);
    if (k == "Weapon") {
        r.set("kind", "weapon");
        r.set("category", w.get("category"));
        r.set("damage", w.get("damage"));
        r.set("dtype", w.get("dtype"));
        String props = w.get("props");
        if (w.has("range")) props.replace("Thrown", "Thrown (Range " + w.get("range") + ")");
        if (w.contains("props", "Versatile")) props.replace("Versatile", "Versatile (" + w.get("versatile") + ")");
        r.set("props", props);
        r.set("versatile", w.contains("props", "Versatile") ? w.get("versatile") : String(""));
        r.set("range", w.get("range"));
        if (!w.is("mastery", "None")) r.set("mastery", w.get("mastery"));
        r.set("bonus", bonus);
    } else if (k == "Armor") {
        r.set("kind", "armor");
        String cat = w.get("acat");
        r.set("category", cat);
        r.set("ac_base", w.get("ac_base"));
        r.set("dex", cat == "Light" ? "yes" : cat == "Medium" ? "max2" : "no");
        r.set("str", w.get("str"));
        if (w.yes("stealth")) r.set("stealth", "disadvantage");
        r.set("acbonus", bonus);
    } else if (k == "Shield") {
        r.set("kind", "shield");
        r.set("category", "Shield");
        r.set("ac_base", w.get("shield_ac"));
        r.set("acbonus", bonus);
    } else if (k == "Tool") {
        r.set("kind", "tool");
        r.set("ability", w.get("ability"));
        r.set("utilize", w.get("utilize"));
    } else {
        r.set("kind", k == "Magic item" ? "magic" : k == "Consumable" ? "consumable" : "gear");
        if (k == "Magic item") {
            r.set("type", w.get("mtype"));
            String tl = w.get("mtype") + ", " + w.get("rarity") + (w.yes("attune") ? " (Requires Attunement)" : "");
            r.set("typeline", tl);
        }
        String eff = w.get("effect");
        if (eff == "Healing") r.set("heal", w.get("rolldice"));
        else if (eff == "Damage") { r.set("damage", w.get("rolldice")); r.set("dtype", w.get("rdtype")); }
        else if (eff == "Something else") { r.set("roll", w.get("rolldice")); r.set("rolllabel", w.get("rolllabel")); }
        r.set("charges", w.get("charges"));
    }
    if (w.has("rarity") && (k == "Magic item" || bonus.length())) {
        r.set("rarity", w.get("rarity"));
        if (w.yes("attune")) r.set("attune", "yes");
    }
    r.set("weight", w.get("weight"));
    r.set("cost", w.get("cost"));
    r.set("text", w.get("text"));
    return r;
}

App* makeItemsApp() { return new ItemsBrowser(); }
DbBrowser* newItemsBrowser() { return new ItemsBrowser(); }

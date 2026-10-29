#ifdef BOARD_TAB5
#include "ddb_import.h"
#include "character.h"
#include "db.h"
#include "storage.h"
#include "web.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

void watchdogPause(bool paused);   // main.cpp

using namespace dnd;

namespace ddb {

namespace {

const char* const ABIL_SLUG[6] = {"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"};
const char* const ALIGN[10] = {"", "Lawful Good", "Neutral Good", "Chaotic Good", "Lawful Neutral", "Neutral",
                               "Chaotic Neutral", "Lawful Evil", "Neutral Evil", "Chaotic Evil"};
const char* const TOOL_WORDS[] = {"Tools", "Supplies", "Kit", "Set", "Utensils", "Instrument", "Vehicles", "Bagpipes",
                                  "Drum", "Dulcimer", "Flute", "Lute", "Lyre", "Horn", "Shawm", "Viol"};

// Only what the import reads, so the 300+ KB answer parses into a few KB.
void buildFilter(JsonDocument& f) {
    f["success"] = true;
    f["message"] = true;
    JsonObject d = f["data"].to<JsonObject>();
    for (const char* k : {"name", "baseHitPoints", "bonusHitPoints", "overrideHitPoints", "removedHitPoints",
                          "temporaryHitPoints", "inspiration", "alignmentId", "currencies", "deathSaves"})
        d[k] = true;
    for (const char* k : {"stats", "bonusStats", "overrideStats"}) {
        d[k][0]["id"] = true;
        d[k][0]["value"] = true;
    }
    d["background"]["definition"]["name"] = true;
    JsonObject race = d["race"].to<JsonObject>();
    race["fullName"] = true;
    race["baseRaceName"] = true;
    race["weightSpeeds"]["normal"]["walk"] = true;
    JsonObject cl = d["classes"][0].to<JsonObject>();
    cl["level"] = true;
    cl["hitDiceUsed"] = true;
    cl["isStartingClass"] = true;
    cl["definition"]["name"] = true;
    cl["subclassDefinition"]["name"] = true;
    d["feats"][0]["definition"]["name"] = true;
    d["feats"][0]["definition"]["categories"][0]["tagName"] = true;
    for (const char* k : {"race", "class", "background", "feat", "condition"}) {
        JsonObject m = d["modifiers"][k][0].to<JsonObject>();
        m["type"] = true;
        m["subType"] = true;
        m["value"] = true;
        m["friendlySubtypeName"] = true;
    }
    for (const char* k : {"race", "class", "background", "feat"}) {
        d["spells"][k][0]["definition"]["name"] = true;
        d["spells"][k][0]["spellCastingAbilityId"] = true;
        d["spells"][k][0]["limitedUse"]["numberUsed"] = true;
    }
    d["classSpells"][0]["spells"][0]["definition"]["name"] = true;
    for (const char* k : {"spellSlots", "pactMagic"}) {
        d[k][0]["level"] = true;
        d[k][0]["used"] = true;
    }
    for (const char* k : {"race", "class", "feat"}) {
        d["actions"][k][0]["name"] = true;
        d["actions"][k][0]["limitedUse"]["numberUsed"] = true;
        d["actions"][k][0]["limitedUse"]["maxUses"] = true;
        d["actions"][k][0]["limitedUse"]["resetType"] = true;
        d["actions"][k][0]["limitedUse"]["useProficiencyBonus"] = true;
        d["actions"][k][0]["limitedUse"]["statModifierUsesId"] = true;
    }
    JsonObject inv = d["inventory"][0].to<JsonObject>();
    inv["quantity"] = true;
    inv["equipped"] = true;
    inv["isAttuned"] = true;
    inv["limitedUse"]["maxUses"] = true;
    inv["limitedUse"]["numberUsed"] = true;
    JsonObject def = inv["definition"].to<JsonObject>();
    for (const char* k : {"name", "filterType", "magic", "rarity", "canAttune", "attunementDescription", "armorClass",
                          "armorTypeId", "damageType", "range", "longRange", "categoryId", "attackType"})
        def[k] = true;
    def["damage"]["diceString"] = true;
    def["properties"][0]["name"] = true;
    for (const char* k : {"personalityTraits", "ideals", "bonds", "flaws", "appearance"}) d["traits"][k] = true;
    for (const char* k : {"backstory", "allies", "enemies", "organizations", "otherNotes"}) d["notes"][k] = true;
}

bool download(const String& id, JsonDocument& doc, String& err) {
    if (WiFi.status() != WL_CONNECTED || web::apMode()) {
        err = "Needs the internet: start web access on your home WiFi";
        return false;
    }
    NetworkClientSecure client;
    client.setInsecure();   // public sheet data only; nothing secret is sent
    HTTPClient http;
    http.useHTTP10(true);   // a plain (unchunked) stream for the parser
    http.setTimeout(15000);
    if (!http.begin(client, "https://character-service.dndbeyond.com/character/v5/character/" + id)) {
        err = "Couldn't reach D&D Beyond";
        return false;
    }
    http.setUserAgent("dnd5e-toolkit");
    watchdogPause(true);   // a big download over TLS takes a few seconds
    const int code = http.GET();
    DeserializationError e = DeserializationError::Ok;
    if (code == 200) {
        JsonDocument filter;
        buildFilter(filter);
        e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    }
    http.end();
    watchdogPause(false);
    if (code == 403 || code == 401) { err = "That character is private: set its privacy to Public on D&D Beyond"; return false; }
    if (code == 404 || code == 409) { err = "D&D Beyond couldn't find that character - check the link, and that it's Public"; return false; }
    if (code != 200) { err = code < 0 ? String("Couldn't reach D&D Beyond (") + http.errorToString(code) + ")" : String("D&D Beyond answered ") + code; return false; }
    if (e) { err = String("Couldn't read D&D Beyond's answer (") + e.c_str() + ")"; return false; }
    if (!doc["success"].as<bool>()) {
        err = doc["message"] | "D&D Beyond refused";
        if (err.indexOf("rivate") >= 0 || err.indexOf("ermission") >= 0) err = "That character is private: set its privacy to Public on D&D Beyond";
        return false;
    }
    return true;
}

void addUnique(std::vector<String>& v, const String& x) { if (x.length() && !listHas(v, x)) v.push_back(x); }

// An inventory entry: the SRD's own record when the name matches, else built
// from D&D Beyond's description of it.
Item makeItem(JsonObject inv, db::Table& srd) {
    JsonObject d = inv["definition"];
    const String name = d["name"] | "Item";
    const int qty = std::max(1, inv["quantity"] | 1);
    Item it;
    int idx = srd.find(name);
    if (idx < 0 && name.endsWith("s")) idx = srd.find(name.substring(0, name.length() - 1));
    db::Rec r;
    if (idx >= 0 && srd.read(idx, r)) {
        it = Item::fromRec(r, qty);
        it.name = name;
    } else {
        it.name = name;
        it.qty = qty;
        const String ft = d["filterType"] | "";
        if (ft == "Weapon") {
            it.kind = "weapon";
            it.category = String((d["categoryId"] | 1) == 2 ? "Martial" : "Simple") + ((d["attackType"] | 1) == 2 ? " Ranged" : " Melee");
            it.damage = d["damage"]["diceString"] | "";
            it.dtype = d["damageType"] | "";
            String props;
            for (JsonObject p : d["properties"].as<JsonArray>()) props += String(props.length() ? ", " : "") + (p["name"] | "");
            it.props = props;
            const int rn = d["range"] | 0, lr = d["longRange"] | 0;
            if (rn > 5 && lr > rn) it.range = String(rn) + "/" + lr;
        } else if (ft == "Armor") {
            const int t = d["armorTypeId"] | 0;   // 1 light, 2 medium, 3 heavy, 4 shield
            it.kind = t == 4 ? "shield" : "armor";
            it.acBase = d["armorClass"] | (t == 4 ? 2 : 10);
            if (t != 4) {
                it.category = t == 1 ? "Light" : t == 2 ? "Medium" : "Heavy";
                it.dex = t == 1 ? "yes" : t == 2 ? "max2" : "no";
            }
        } else it.kind = (d["magic"] | false) ? "magic" : "gear";
        if (d["magic"] | false) it.rarity = d["rarity"] | "";
        if (d["canAttune"] | false) it.attune = String(d["attunementDescription"] | "").length() ? String(d["attunementDescription"] | "") : String("yes");
    }
    it.equipped = inv["equipped"] | false;
    it.attuned = inv["isAttuned"] | false;
    const int maxUses = inv["limitedUse"]["maxUses"] | 0;
    if (maxUses > 0) {
        it.chargesMax = maxUses;
        it.charges = std::max(0, maxUses - (inv["limitedUse"]["numberUsed"] | 0));
    }
    return it;
}

// D&D Beyond's character -> ours. Notes are left alone (the caller decides).
void convert(JsonObject d, Character& c, String& warn) {
    c.name = d["name"] | "Hero";

    // class: ours holds one; a multiclass keeps its biggest class and the total level
    JsonArray classes = d["classes"];
    JsonObject main;
    int total = 0, hdUsed = 0;
    for (JsonObject k : classes) {
        total += k["level"] | 0;
        hdUsed += k["hitDiceUsed"] | 0;
        if (main.isNull() || (k["level"] | 0) > (main["level"] | 0)) main = k;
    }
    c.cls = main["definition"]["name"] | "";
    c.subclass = main["subclassDefinition"]["name"] | "";
    c.level = std::max(1, std::min(20, total));
    c.hitDiceUsed = std::min(hdUsed, c.level);
    if (classes.size() > 1) warn += "Multiclass: tracked as a level " + String(c.level) + " " + c.cls + ". ";
    c.ci.load(c.cls);
    c.weapons = c.ci.rec.get("weapons");
    c.armor = c.ci.rec.get("armor");

    // origin
    c.species = d["race"]["baseRaceName"] | "";
    const String full = d["race"]["fullName"] | "";
    c.speciesOpt = full != c.species ? full : String("");
    c.background = d["background"]["definition"]["name"] | "";
    const int al = d["alignmentId"] | 0;
    c.alignment = al > 0 && al < 10 ? ALIGN[al] : "";
    c.speed = d["race"]["weightSpeeds"]["normal"]["walk"] | 30;
    c.size = "Medium";

    // ability scores: base, then bonuses (an override replaces both)
    int base[6] = {10, 10, 10, 10, 10, 10}, over[6] = {0};
    for (JsonObject s : d["stats"].as<JsonArray>()) { int i = (s["id"] | 0) - 1; if (i >= 0 && i < 6) base[i] = s["value"] | 10; }
    for (JsonObject s : d["bonusStats"].as<JsonArray>()) { int i = (s["id"] | 0) - 1; if (i >= 0 && i < 6) base[i] += s["value"] | 0; }
    for (JsonObject s : d["overrideStats"].as<JsonArray>()) { int i = (s["id"] | 0) - 1; if (i >= 0 && i < 6) over[i] = s["value"] | 0; }
    int setTo[6] = {0};
    int hpPerLevel = 0;

    // everything else D&D Beyond grants comes as modifiers (items' are left
    // out: our items carry their own effects)
    for (JsonPair cat : d["modifiers"].as<JsonObject>()) {
        for (JsonObject m : cat.value().as<JsonArray>()) {
            const String type = m["type"] | "", sub = m["subType"] | "", friendly = m["friendlySubtypeName"] | "";
            const int v = m["value"] | 0;
            for (int a = 0; a < 6; a++) {
                if (sub != String(ABIL_SLUG[a]) + "-score") continue;
                if (type == "bonus") base[a] += v;
                else if (type == "set") setTo[a] = std::max(setTo[a], v);
            }
            if (type == "proficiency" || type == "expertise") {
                bool done = false;
                for (auto& sk : SKILLS)
                    if (friendly.equalsIgnoreCase(sk.name)) {
                        addUnique(type == "expertise" ? c.expertise : c.skillProf, sk.name);
                        done = true;
                    }
                for (int a = 0; a < 6 && !done; a++)
                    if (sub == String(ABIL_SLUG[a]) + "-saving-throws") { addUnique(c.saveProf, ABIL_NAMES[a]); done = true; }
                if (!done && type == "proficiency")
                    for (auto w : TOOL_WORDS)
                        if (friendly.indexOf(w) >= 0) { addUnique(c.toolProf, friendly); break; }
            } else if (type == "set-base" && sub == "darkvision") c.darkvision = std::max(c.darkvision, v);
            else if (type == "bonus" && sub == "speed") c.speed += v;
            else if (type == "bonus" && sub == "hit-points-per-level") hpPerLevel += v;
            else if (type == "size") c.size = friendly.length() ? friendly : c.size;
        }
    }
    for (int a = 0; a < 6; a++) c.score[a] = over[a] ? over[a] : std::max(std::min(base[a], 30), setTo[a]);

    // hit points
    const int over_hp = d["overrideHitPoints"] | 0;
    c.hpMax = over_hp ? over_hp : (d["baseHitPoints"] | 0) + (d["bonusHitPoints"] | 0) + c.level * (c.mod(CON) + hpPerLevel);
    c.hpMax = std::max(1, c.hpMax);
    c.hp = std::max(0, c.hpMax - (d["removedHitPoints"] | 0));
    c.hpTemp = d["temporaryHitPoints"] | 0;
    c.deathOk = std::min(3, (int)(d["deathSaves"]["successCount"] | 0));
    c.deathFail = std::min(3, (int)(d["deathSaves"]["failCount"] | 0));
    c.inspiration = d["inspiration"] | false;
    const char* const COIN[5] = {"cp", "sp", "ep", "gp", "pp"};
    for (int i = 0; i < 5; i++) c.coins[i] = d["currencies"][COIN[i]] | 0;

    // feats (D&D Beyond's bookkeeping ones are tagged "__...")
    for (JsonObject f : d["feats"].as<JsonArray>()) {
        bool hidden = false;
        for (JsonObject t : f["definition"]["categories"].as<JsonArray>()) if (String(t["tagName"] | "").startsWith("__")) hidden = true;
        if (!hidden) addUnique(c.feats, f["definition"]["name"] | "");
    }

    // spells: the class's, then the granted ones (species, feats), which have free
    // casts and their own ability. The sheet works out the SRD species' spells
    // itself; other species' spells are listed with the class's.
    c.freeUsed.clear();
    auto spentFree = [&](JsonObject s, const String& n) {
        for (int i = s["limitedUse"]["numberUsed"] | 0; i > 0; i--) c.freeUsed.push_back(n);
    };
    for (JsonObject cs : d["classSpells"].as<JsonArray>())
        for (JsonObject s : cs["spells"].as<JsonArray>()) addUnique(c.spells, s["definition"]["name"] | "");
    for (const char* k : {"class", "background"})
        for (JsonObject s : d["spells"][k].as<JsonArray>()) addUnique(c.spells, s["definition"]["name"] | "");
    std::vector<String> raceSpells;
    for (JsonObject s : d["spells"]["race"].as<JsonArray>()) raceSpells.push_back(s["definition"]["name"] | "");
    if (!c.speciesOpt.length()) c.speciesOpt = Character::guessLineage(c.species, raceSpells);
    const auto granted = c.speciesSpells();
    for (JsonObject s : d["spells"]["race"].as<JsonArray>()) {
        const String n = s["definition"]["name"] | "";
        bool ours = false;
        for (auto& g : granted) ours |= g.name.equalsIgnoreCase(n);
        if (!ours) { addUnique(c.spells, n); continue; }
        spentFree(s, n);
        const int ab = (s["spellCastingAbilityId"] | 0) - 1;
        if (ab >= 0 && ab < 6 && !c.spAbility.length()) c.spAbility = ABIL_NAMES[ab];
    }
    if (!granted.empty() && !c.spAbility.length()) c.spAbility = "Intelligence";
    for (JsonObject s : d["spells"]["feat"].as<JsonArray>()) {
        const String n = s["definition"]["name"] | "";
        addUnique(c.miSpells, n);
        spentFree(s, n);
        const int ab = (s["spellCastingAbilityId"] | 0) - 1;
        if (ab >= 0 && ab < 6 && !c.miAbility.length()) c.miAbility = ABIL_NAMES[ab];
    }
    if (c.miSpells.size() && !c.miAbility.length()) c.miAbility = "Intelligence";
    for (JsonObject s : d["spellSlots"].as<JsonArray>()) { int l = s["level"] | 0; if (l >= 1 && l <= 9) c.slotsUsed[l] = s["used"] | 0; }
    for (JsonObject s : d["pactMagic"].as<JsonArray>()) { int l = s["level"] | 0, u = s["used"] | 0; if (l >= 1 && l <= 9 && u) c.slotsUsed[l] = u; }

    // resources: ours from the SRD, uses spent from D&D Beyond's actions (matched by
    // name or by the feature's name: their "Rage" is our "Rages")
    c.res.clear();
    c.syncResources();
    for (const char* k : {"class", "race", "feat"})
        for (JsonObject a : d["actions"][k].as<JsonArray>()) {
            const String an = a["name"] | "";
            JsonObject lu = a["limitedUse"];
            if (!an.length() || lu.isNull()) continue;
            const int used = lu["numberUsed"] | 0;
            bool known = false;
            for (auto& r : c.res)
                if (r.name.equalsIgnoreCase(an) || r.from.equalsIgnoreCase(an)) { r.cur = std::max(0, r.max - used); known = true; }
            if (known || !strcmp(k, "class")) continue;
            // a feat's or species' limited use we have no rules for: keep it as a counter
            int mx = lu["maxUses"] | 0;
            if (lu["useProficiencyBonus"] | false) mx = std::max(mx, c.prof());
            const int stat = lu["statModifierUsesId"] | 0;
            if (stat >= 1 && stat <= 6) mx = std::max(mx, std::max(1, c.mod(stat - 1)));
            if (mx <= 0 || c.resIndex(an) >= 0) continue;
            Resource r;
            r.name = an;
            r.max = mx;
            r.cur = std::max(0, mx - used);
            r.rest = (lu["resetType"] | 0) == 1 ? 'S' : 'L';   // D&D Beyond: 1 Short Rest, 2 Long Rest
            r.custom = true;
            c.res.push_back(r);
        }

    // inventory, matched to the SRD where it can be
    db::Table srd;
    srd.open({{"/gmtools/srd/items", {"kind", "category", "cost", "weight"}},
              {"/gmtools/srd/magic", {"kind", "type", "rarity", "attune"}}},
             "/gmtools/custom/items.txt", {"kind"});
    c.items.clear();
    for (JsonObject inv : d["inventory"].as<JsonArray>()) {
        Item it = makeItem(inv, srd);
        bool merged = false;
        for (auto& e : c.items)
            if (e.name == it.name && e.equipped == it.equipped && !e.chargesMax && !it.chargesMax) { e.qty += it.qty; merged = true; break; }
        if (!merged) c.items.push_back(it);
    }
    srd.close();
}

// D&D Beyond's backstory and traits, for a new character's notes.
String notesFrom(JsonObject d) {
    String o;
    auto add = [&](const char* label, const char* v) { if (v && *v) o += String(o.length() ? "\n\n" : "") + label + ": " + v; };
    add("Personality", d["traits"]["personalityTraits"].as<const char*>());
    add("Ideals", d["traits"]["ideals"].as<const char*>());
    add("Bonds", d["traits"]["bonds"].as<const char*>());
    add("Flaws", d["traits"]["flaws"].as<const char*>());
    add("Appearance", d["traits"]["appearance"].as<const char*>());
    add("Backstory", d["notes"]["backstory"].as<const char*>());
    add("Allies", d["notes"]["allies"].as<const char*>());
    add("Enemies", d["notes"]["enemies"].as<const char*>());
    add("Organizations", d["notes"]["organizations"].as<const char*>());
    add("Notes", d["notes"]["otherNotes"].as<const char*>());
    return o;
}

// The file of a character imported before from this id, or "".
String existingFile(const String& id) {
    std::vector<String> files;
    storage::listFiles("/gmtools/characters", files);
    for (auto& f : files) {
        const String p = String("/gmtools/characters/") + f;
        String head;
        storage::readText(p.c_str(), head, 600);
        for (auto& l : splitList(head, '\n')) {
            if (!l.startsWith("ddb: ")) continue;
            String other;
            if (parseLink(l.substring(5), other) && other == id) return p;
        }
    }
    return "";
}

}  // namespace

bool parseLink(const String& link, String& id) {
    String s = link;
    s.trim();
    const int at = s.indexOf("/characters/");
    if (at >= 0) s = s.substring(at + 12);
    id = "";
    for (size_t i = 0; i < s.length() && isdigit((unsigned char)s[i]); i++) id += s[i];
    return id.length() >= 3;
}

bool import(const String& link, String& path, String& msg) {
    String id;
    if (!parseLink(link, id)) { msg = "That isn't a D&D Beyond character link"; return false; }
    if (!storage::ok()) { msg = "No SD card"; return false; }
    JsonDocument doc;
    if (!download(id, doc, msg)) return false;
    JsonObject d = doc["data"];

    Character c;
    const String old = existingFile(id);
    String notes, oldLink;
    if (old.length() && c.load(old)) notes = c.notes, oldLink = c.ddb;   // an update keeps the notes taken here
    else notes = notesFrom(d);
    c = Character();
    String warn;
    convert(d, c, warn);
    if (!c.cls.length() || !c.ci.loaded) warn += "Class \"" + c.cls + "\" isn't in the SRD, so class features won't show. ";
    c.notes = notes;
    String share = link;
    share.trim();
    c.ddb = share.startsWith("http") ? share : oldLink.length() ? oldLink : String("https://www.dndbeyond.com/characters/") + id;
    c.path = old.length() ? old : Character::pathFor(c.name);
    if (!c.save()) { msg = "Couldn't save (SD card?)"; return false; }
    path = c.path;
    msg = String(old.length() ? "Updated " : "Imported ") + c.name + " (" + c.levelLine() + ")" + (warn.length() ? ". " + warn : String(""));
    msg.trim();
    return true;
}

}  // namespace ddb
#endif

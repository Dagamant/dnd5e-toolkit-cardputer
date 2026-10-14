#include "encounter.h"
#include "character.h"
#include "db.h"
#include "dice_expr.h"
#include "storage.h"

namespace encounter {

std::vector<Pending> queue;

static const int BUDGET[20][3] = {
    {50, 75, 100}, {100, 150, 200}, {150, 225, 400}, {250, 375, 500}, {500, 750, 1100},
    {600, 1000, 1400}, {750, 1300, 1700}, {1000, 1700, 2100}, {1300, 2000, 2600}, {1600, 2300, 3100},
    {1900, 2900, 4100}, {2200, 3700, 4700}, {2600, 4200, 5400}, {2900, 4900, 6200}, {3300, 5400, 7800},
    {3800, 6100, 9800}, {4500, 7200, 11700}, {5000, 8700, 14200}, {5500, 10700, 17200}, {6400, 13200, 22000}};

int budget(int level, int d) {
    level = std::max(1, std::min(20, level));
    return BUDGET[level - 1][std::max(0, std::min(2, d))];
}

int Plan::totalXp() const {
    int t = 0;
    for (auto& m : monsters) t += m.xp * m.count;
    return t;
}

static const char* PATH = "/gmtools/encounter_plan.txt";

bool Plan::load() {
    monsters.clear();
    String txt;
    if (!storage::readText(PATH, txt, 8192)) return false;
    for (auto& line : dnd::splitList(txt, '\n')) {
        auto f = dnd::splitList(line, '|');
        if (f.size() >= 3 && f[0] == "party") { partySize = f[1].toInt(); partyLevel = f[2].toInt(); continue; }
        if (f.size() >= 5 && f[0] == "gen") {
            genHabitat = f[1].toInt(); genType = f[2].toInt(); genDiff = f[3].toInt(); genGroup = f[4].toInt();
            continue;
        }
        if (f.size() >= 8 && f[0] == "m") {
            Entry e;
            e.name = f[1]; e.count = f[2].toInt(); e.xp = f[3].toInt(); e.hp = f[4].toInt();
            e.ac = f[5].toInt(); e.init = f[6].toInt(); e.hpdice = f[7]; e.cr = f.size() > 8 ? f[8] : String("");
            monsters.push_back(e);
        }
    }
    partySize = std::max(1, std::min(10, partySize));
    partyLevel = std::max(1, std::min(20, partyLevel));
    return true;
}

bool Plan::save() const {
    String s = String("party|") + partySize + "|" + partyLevel + "\n";
    s += String("gen|") + genHabitat + "|" + genType + "|" + genDiff + "|" + genGroup + "\n";
    for (auto& m : monsters) {
        String nm = m.name;
        nm.replace("|", "/");
        s += String("m|") + nm + "|" + m.count + "|" + m.xp + "|" + m.hp + "|" + m.ac + "|" + m.init + "|" +
             (m.hpdice.length() ? m.hpdice : String("-")) + "|" + m.cr + "\n";
    }
    return storage::writeText(PATH, s);
}

int enqueue(const std::vector<Entry>& monsters, bool rolledHp) {
    int added = 0;
    for (auto& m : monsters) {
        for (int k = 1; k <= m.count; k++) {
            Pending p;
            p.name = m.count > 1 ? m.name + " " + k : m.name;
            p.monster = m.name;
            p.init = dice::roll(String("d20") + dnd::fmtMod(m.init)).total;
            int hp = m.hp;
            if (rolledHp && m.hpdice.length() && m.hpdice != "-") {
                auto r = dice::roll(m.hpdice);
                if (r.ok) hp = std::max(1, r.total);
            }
            p.hp = p.maxHp = hp;
            p.ac = m.ac;
            queue.push_back(p);
            added++;
        }
    }
    return added;
}

static const std::vector<const char*> COLS = {"cr", "xp", "hp", "ac", "init"};

static bool lookupIn(db::Table& t, String name, Entry& out) {
    name.trim();
    int i = t.find(name);
    // plurals: "Goblin Warriors", "Swarms of Insects", "Wolves"
    if (i < 0 && name.endsWith("s")) i = t.find(name.substring(0, name.length() - 1));
    if (i < 0 && name.endsWith("es")) i = t.find(name.substring(0, name.length() - 2));
    if (i < 0 && name.endsWith("ves")) i = t.find(name.substring(0, name.length() - 3) + "f");
    if (i < 0 && name.startsWith("Swarms ")) i = t.find("Swarm " + name.substring(7));
    if (i < 0) return false;
    db::Rec r;
    if (!t.read(i, r)) return false;
    out.name = t.at(i).name;
    out.xp = r.num("xp");
    out.hp = r.num("hp", 1);
    out.ac = r.num("ac", 10);
    out.init = r.get("init").toInt();
    out.hpdice = r.get("hpdice");
    out.cr = r.get("cr");
    return true;
}

static void openMonsters(db::Table& t) {
    t.open({{"/gmtools/srd/monsters", {"cr", "type", "xp", "hp", "ac", "init"}}}, "/gmtools/custom/monsters.txt", COLS);
}

bool lookup(const String& name, Entry& out) {
    db::Table t;
    openMonsters(t);
    return lookupIn(t, name, out);
}

std::vector<Entry> parseList(const String& textIn, String& unknown) {
    std::vector<Entry> out;
    unknown = "";
    // drop notes in parentheses, split on , ; + and " and "
    String text;
    int depth = 0;
    for (size_t i = 0; i < textIn.length(); i++) {
        char c = textIn[i];
        if (c == '(') depth++;
        else if (c == ')') depth = std::max(0, depth - 1);
        else if (!depth) text += (c == ';' || c == '+') ? ',' : c;
    }
    text.replace(" and ", ",");
    db::Table t;
    openMonsters(t);
    for (auto part : dnd::splitList(text, ',')) {
        int count = 1, k = 0;
        // leading count: "3x Goblin", "3 x Goblin", "3 Goblins", "a Goblin"
        while (k < (int)part.length() && isdigit((unsigned char)part[k])) k++;
        if (k) {
            count = part.substring(0, k).toInt();
            part = part.substring(k);
            part.trim();
            if (part.startsWith("x ") || part.startsWith("X ")) part = part.substring(2);
            else if ((part.startsWith("x") || part.startsWith("X")) && part.length() > 1 && isupper((unsigned char)part[1])) part = part.substring(1);
        } else if (part.startsWith("a ") || part.startsWith("A ")) part = part.substring(2);
        else if (part.startsWith("an ") || part.startsWith("An ")) part = part.substring(3);
        else {
            // trailing count: "Goblin x3"
            int x = part.lastIndexOf(" x");
            if (x > 0 && part.substring(x + 2).toInt() > 0) { count = part.substring(x + 2).toInt(); part = part.substring(0, x); }
        }
        part.trim();
        if (!part.length()) continue;
        Entry e;
        if (!lookupIn(t, part, e)) { unknown += (unknown.length() ? ", " : "") + part; continue; }
        e.count = std::max(1, std::min(20, count));
        bool merged = false;
        for (auto& m : out) if (m.name == e.name) { m.count += e.count; merged = true; }
        if (!merged) out.push_back(e);
    }
    return out;
}

String describe(const std::vector<Entry>& monsters) {
    String s;
    for (auto& m : monsters) s += String(s.length() ? ", " : "") + m.count + "x " + m.name;
    return s;
}

}  // namespace encounter

#include "campaign.h"
#include "db.h"
#include "storage.h"

namespace campaign {

static const char* const ROOT = "/gmtools/campaigns";

static const char* const CAMPAIGN_SECTIONS[] = {"Pitch & notes", "Threads & quests", "NPCs", "Locations",
                                                "Factions", "Party"};
static const char* const SESSION_SECTIONS[] = {"Recap", "Spotlight", "Strong start", "Scenes", "Secrets & clues",
                                               "Locations", "NPCs", "Encounters", "Treasure", "Notes"};

struct Hint { const char* section; const char* hint; const char* check; };
static const Hint HINTS[] = {
    {"Pitch & notes", "The campaign in a sentence, tone, themes", "done"},
    {"Threads & quests", "Open plot threads and quests", "resolved"},
    {"NPCs", "Who matters, and what do they want?", "met"},
    {"Locations", "Places the heroes might go", "visited"},
    {"Factions", "Groups, goals and their next move", "done"},
    {"Party", "Each hero's goals and hooks (G adds the party)", "done"},
    {"Recap", "What happened last time", "told"},
    {"Spotlight", "A moment for each hero (G adds the party)", "done"},
    {"Strong start", "Open with action (G rolls one)", "used"},
    {"Scenes", "A few likely scenes - not a script", "played"},
    {"Secrets & clues", "10 things they could learn, any order", "revealed"},
    {"Encounters", "e.g. 3x Goblin Warrior - R runs it", "run"},
    {"Treasure", "Rewards worth finding (G rolls loot)", "given"},
    {"Notes", "What actually happened (becomes next recap)", "done"},
};

static const Hint* hintFor(const String& s) {
    for (auto& h : HINTS) if (s == h.section) return &h;
    return nullptr;
}
const char* sectionHint(const String& s) { auto h = hintFor(s); return h ? h->hint : "A to add"; }
const char* checkLabel(const String& s) { auto h = hintFor(s); return h ? h->check : "done"; }

void Doc::init(Kind k) {
    kind = k;
    sections.clear();
    if (k == CAMPAIGN) for (auto n : CAMPAIGN_SECTIONS) sections.push_back({n, {}});
    else for (auto n : SESSION_SECTIONS) sections.push_back({n, {}});
}

Section* Doc::find(const String& name) {
    for (auto& s : sections) if (s.name.equalsIgnoreCase(name)) return &s;
    return nullptr;
}

bool Doc::load(const String& p, Kind k) {
    init(k);
    path = p;
    String txt;
    if (!storage::readText(p.c_str(), txt, 32768)) return false;
    Section* cur = nullptr;
    int start = 0;
    while (start < (int)txt.length()) {
        int nl = txt.indexOf('\n', start);
        String l = nl < 0 ? txt.substring(start) : txt.substring(start, nl);
        start = nl < 0 ? txt.length() : nl + 1;
        l.trim();
        if (!l.length()) continue;
        if (l.startsWith("## ")) {
            String n = l.substring(3);
            cur = find(n);
            if (!cur) { sections.push_back({n, {}}); cur = &sections.back(); }   // user-added section
        } else if (cur && l.startsWith("[") && l.length() >= 3 && l[2] == ']') {
            String t = l.substring(3);
            t.trim();
            cur->items.push_back({db::unescape(t), l[1] == 'x' || l[1] == 'X'});
        } else if (!cur && l.startsWith("title: ")) title = l.substring(7);
        else if (!cur && l.startsWith("number: ")) number = l.substring(8).toInt();
        else if (!cur && l.startsWith("status: ")) played = l.substring(8) == "played";
    }
    return true;
}

bool Doc::save() const {
    String s = "title: " + title + "\n";
    if (kind == SESSION) s += String("number: ") + number + "\nstatus: " + (played ? "played" : "planned") + "\n";
    for (auto& sec : sections) {
        s += "\n## " + sec.name + "\n";
        for (auto& it : sec.items) s += String(it.done ? "[x] " : "[ ] ") + db::escape(it.text) + "\n";
    }
    return storage::writeText(path.c_str(), s);
}

// ---------------------------------------------------------------- campaigns

String dirOf(const String& id) { return String(ROOT) + "/" + id; }

static String headerValue(const String& path, const char* key) {
    String txt;
    storage::readText(path.c_str(), txt, 160);
    String k = String(key) + ": ";
    int i = txt.indexOf(k);
    if (i < 0) return "";
    int nl = txt.indexOf('\n', i);
    return txt.substring(i + k.length(), nl < 0 ? txt.length() : nl);
}

std::vector<Info> campaigns() {
    std::vector<Info> out;
    std::vector<String> dirs;
    storage::listDirs(ROOT, dirs);
    for (auto& d : dirs) {
        Info in;
        in.id = d;
        in.title = headerValue(dirOf(d) + "/campaign.txt", "title");
        if (!in.title.length()) in.title = d;
        std::vector<String> files;
        storage::listFiles(dirOf(d).c_str(), files);
        for (auto& f : files) if (f.startsWith("session_")) in.sessions++;
        out.push_back(in);
    }
    return out;
}

String create(const String& title) {
    String id;
    for (size_t i = 0; i < title.length() && id.length() < 24; i++)
        id += isalnum((unsigned char)title[i]) ? (char)tolower(title[i]) : '_';
    while (id.indexOf("__") >= 0) id.replace("__", "_");
    if (!id.length() || id == "_") id = "campaign";
    String base = id;
    for (int i = 2; storage::exists(dirOf(id).c_str()) && i < 100; i++) id = base + "_" + i;
    storage::mkdir(ROOT);
    if (!storage::mkdir(dirOf(id).c_str())) return "";
    Doc d;
    d.init(CAMPAIGN);
    d.title = title;
    d.path = dirOf(id) + "/campaign.txt";
    return d.save() ? id : String("");
}

bool removeCampaign(const String& id) {
    String cid, cpath;
    if (current(cid, cpath) && cid == id) storage::remove((String(ROOT) + "/current.txt").c_str());
    return storage::removeDir(dirOf(id).c_str());
}

std::vector<SessionInfo> sessions(const String& id) {
    std::vector<SessionInfo> out;
    std::vector<String> files;
    storage::listFiles(dirOf(id).c_str(), files);
    for (auto& f : files) {
        if (!f.startsWith("session_")) continue;
        SessionInfo s;
        s.path = dirOf(id) + "/" + f;
        String head;
        storage::readText(s.path.c_str(), head, 200);
        Doc d;
        d.kind = SESSION;
        // header lines only
        int cut = head.indexOf("\n##");
        if (cut >= 0) head.remove(cut);
        for (auto key : {"title: ", "number: ", "status: "}) {
            int i = head.indexOf(key);
            if (i < 0) continue;
            int nl = head.indexOf('\n', i);
            String v = head.substring(i + strlen(key), nl < 0 ? head.length() : nl);
            v.trim();
            if (key[0] == 't') s.title = v;
            else if (key[0] == 'n') s.number = v.toInt();
            else s.played = v == "played";
        }
        if (!s.number) s.number = f.substring(8).toInt();
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const SessionInfo& a, const SessionInfo& b) { return a.number < b.number; });
    return out;
}

String newSession(const String& id, const String& title, String& report) {
    auto list = sessions(id);
    int number = list.empty() ? 1 : list.back().number + 1;
    Doc d;
    d.init(SESSION);
    d.number = number;
    d.title = title.length() ? title : String("Session ") + number;
    char name[32];
    snprintf(name, sizeof(name), "/session_%03d.txt", number);
    d.path = dirOf(id) + name;
    report = "";
    if (!list.empty()) {
        Doc prev;
        if (prev.load(list.back().path, SESSION)) {
            int secrets = 0, recap = 0;
            if (auto from = prev.find("Secrets & clues"))
                for (auto& it : from->items) if (!it.done) { d.find("Secrets & clues")->items.push_back({it.text, false}); secrets++; }
            if (auto from = prev.find("Notes"))
                for (auto& it : from->items) { d.find("Recap")->items.push_back({it.text, false}); recap++; }
            if (secrets) report = String(secrets) + " secret" + (secrets > 1 ? "s" : "") + " carried";
            if (recap) report += String(report.length() ? ", " : "") + "recap from S" + prev.number;
        }
    }
    if (!d.save()) return "";
    setCurrent(id, d.path);
    return d.path;
}

// ---------------------------------------------------------------- current plan

void setCurrent(const String& id, const String& sessionPath) {
    storage::writeText((String(ROOT) + "/current.txt").c_str(), id + "\n" + sessionPath + "\n");
}

bool current(String& id, String& sessionPath) {
    String txt;
    if (!storage::readText((String(ROOT) + "/current.txt").c_str(), txt, 256)) return false;
    int nl = txt.indexOf('\n');
    if (nl < 0) return false;
    id = txt.substring(0, nl);
    sessionPath = txt.substring(nl + 1);
    id.trim();
    sessionPath.trim();
    return id.length() && sessionPath.length() && storage::exists(sessionPath.c_str());
}

bool addToCurrentSession(const char* section, const String& text, String& sessionTitle) {
    String id, path;
    if (!current(id, path)) return false;
    Doc d;
    if (!d.load(path, SESSION)) return false;
    Section* s = d.find(section);
    if (!s) return false;
    s->items.push_back({text, false});
    sessionTitle = String("S") + d.number + " " + d.title;
    return d.save();
}

}  // namespace campaign

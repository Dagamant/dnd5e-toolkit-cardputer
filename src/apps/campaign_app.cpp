#include "tables_api.h"
#include "../core/app.h"
#include "../core/campaign.h"
#include "../core/character.h"
#include "../core/encounter.h"
#include "../core/prompt.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"

using namespace ui;

// Campaign & session planner. Campaign notes (threads, NPCs, locations,
// factions, party) plus session plans laid out like the "Lazy DM" checklist:
// recap, spotlight, strong start, scenes, secrets & clues, locations, NPCs,
// encounters, treasure and notes. Items can be ticked (revealed, run, given...).
// New sessions carry over unrevealed secrets and turn the last notes into a recap.

namespace {

const uint16_t ACCENT = rgb(120, 200, 160);
constexpr int ITEM_MAX = 200;

enum PromptTag { P_NEW_CAMPAIGN = 1, P_RENAME_CAMPAIGN, P_NEW_SESSION, P_ADD_ITEM, P_EDIT_ITEM, P_RENAME_DOC };

class CampaignApp : public App {
public:
    const char* name() const override { return "Campaign"; }
    const char* blurb() const override { return "Campaign notes & session prep."; }
    uint16_t accent() const override { return ACCENT; }

    void drawIcon(int cx, int cy, float t) override {
        // open book with a ribbon bookmark and a page turning
        gfx.fillTriangle(cx - 18, cy - 12, cx, cy - 9, cx - 18, cy + 13, rgb(220, 205, 170));
        gfx.fillTriangle(cx, cy - 9, cx, cy + 16, cx - 18, cy + 13, rgb(220, 205, 170));
        gfx.fillTriangle(cx + 18, cy - 12, cx, cy - 9, cx + 18, cy + 13, rgb(235, 222, 190));
        gfx.fillTriangle(cx, cy - 9, cx, cy + 16, cx + 18, cy + 13, rgb(235, 222, 190));
        gfx.drawLine(cx, cy - 9, cx, cy + 16, rgb(120, 90, 60));
        for (int i = 0; i < 4; i++) {
            gfx.drawLine(cx - 15, cy - 6 + i * 5, cx - 4, cy - 4 + i * 5, rgb(150, 130, 110));
            gfx.drawLine(cx + 4, cy - 4 + i * 5, cx + 15, cy - 6 + i * 5, rgb(150, 130, 110));
        }
        gfx.fillRect(cx + 8, cy - 12, 4, 20, ACCENT);
        gfx.fillTriangle(cx + 8, cy + 8, cx + 12, cy + 8, cx + 10, cy + 5, rgb(235, 222, 190));
        float p = fmodf(t * 0.6f, 1.0f);   // a page lifting
        if (t > 0 && p < 0.5f) {
            int lift = (int)(sinf(p * 2 * (float)M_PI) * 8);
            gfx.drawLine(cx, cy - 9, cx + 14 - lift, cy - 12 - lift, C_GOLD);
            gfx.drawLine(cx, cy + 16, cx + 14 - lift, cy + 13 - lift, C_GOLD);
        }
    }

    void onEnter() override {
        input::setTextMode(false);
        loadCampaigns();
        view_ = LIST;
        // start on the campaign that's currently being planned
        String cid, cpath;
        if (campaign::current(cid, cpath))
            for (size_t i = 0; i < camps_.size(); i++) if (camps_[i].id == cid) listSel_ = i;
        if (!storage::ok()) toast("No SD card - see Settings", C_BLOOD);
    }
    void onExit() override {
        if (view_ == DOC) doc_.save();
        releaseRandomTables();
        input::setTextMode(false);
    }

    bool handle(const input::Event& e) override {
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done()) onPrompt(prompt_.tag(), prompt_.value());
            return true;
        }
        switch (view_) {
            case LIST: return handleList(e);
            case HOME: return handleHome(e);
            default: return handleDoc(e);
        }
    }

    void draw() override {
        drawBackground();
        if (view_ == LIST) drawList();
        else if (view_ == HOME) drawHome();
        else drawDoc();
        prompt_.draw();
    }

private:
    enum View { LIST, HOME, DOC };
    View view_ = LIST;
    Prompt prompt_;
    bool confirm_ = false;

    // ---------------------------------------------------------------- campaigns
    std::vector<campaign::Info> camps_;
    int listSel_ = 0;

    void loadCampaigns() {
        camps_ = campaign::campaigns();
        if (listSel_ > (int)camps_.size()) listSel_ = 0;
    }

    bool handleList(const input::Event& e) {
        int n = camps_.size() + 1;   // last row = new campaign
        if (!(e.key == input::K_CHAR && e.fn && e.base == 'x')) confirm_ = false;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: listSel_ = (listSel_ + n - 1) % n; sfx::click(); return true;
            case input::K_DOWN: listSel_ = (listSel_ + 1) % n; sfx::click(); return true;
            case input::K_ENTER:
                if (listSel_ < (int)camps_.size()) openCampaign(camps_[listSel_].id);
                else newCampaign();
                return true;
            case input::K_CHAR:
                if (e.fn && e.base == 'x' && listSel_ < (int)camps_.size()) {
                    if (!confirm_) { confirm_ = true; toast("Fn+X again: delete campaign + sessions", C_BLOOD); return true; }
                    confirm_ = false;
                    campaign::removeCampaign(camps_[listSel_].id);
                    loadCampaigns();
                    toast("Campaign deleted", C_DIM);
                    sfx::back();
                } else if (!e.fn && e.base == 'n') newCampaign();
                return true;
            default: return true;
        }
    }

    void newCampaign() {
        if (!storage::ok()) { sfx::error(); toast("No SD card", C_BLOOD); return; }
        prompt_.open("Campaign name", "", P_NEW_CAMPAIGN, 32);
    }

    void drawList() {
        drawHeader("Campaigns", ACCENT);
        gfx.setFont(&fonts::Font0);
        const int rowH = 14, y0 = HEADER_H + 4;
        int n = camps_.size() + 1, rows = (H - FOOTER_H - y0) / rowH;
        int first = std::max(0, std::min(listSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = y0 + (i - first) * rowH;
            bool s = i == listSel_;
            if (s) drawPanel(3, y - 2, W - 6, rowH - 1, C_PANEL, ACCENT);
            gfx.setTextDatum(top_left);
            if (i == (int)camps_.size()) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString("+ New campaign", 10, y + 1);
                continue;
            }
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            String t = camps_[i].title;
            if (t.length() > 27) t = t.substring(0, 26) + ".";
            gfx.drawString(t, 10, y + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(camps_[i].sessions) + (camps_[i].sessions == 1 ? " session" : " sessions"), W - 9, y + 1);
        }
        if (camps_.empty()) {
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Plan a campaign and its sessions:", W / 2, 60);
            gfx.drawString("threads, NPCs, secrets, encounters.", W / 2, 72);
        }
        drawFooter("Ent open  N new  FnX delete  Esc back");
    }

    // ---------------------------------------------------------------- campaign home
    String id_, campTitle_;
    std::vector<campaign::SessionInfo> sess_;
    int homeSel_ = 0;
    String currentPath_;

    void openCampaign(const String& id) {
        id_ = id;
        campTitle_ = id;
        for (auto& c : camps_) if (c.id == id) campTitle_ = c.title;
        loadSessions();
        homeSel_ = 0;
        // select the session being planned
        for (int i = 0; i < (int)sess_.size(); i++) if (sess_[i].path == currentPath_) homeSel_ = 2 + i;
        view_ = HOME;
        sfx::select();
    }

    void loadSessions() {
        sess_ = campaign::sessions(id_);
        std::reverse(sess_.begin(), sess_.end());   // newest first
        String cid;
        if (!campaign::current(cid, currentPath_) || cid != id_) currentPath_ = "";
    }

    // rows: 0 campaign notes, 1 new session, 2.. sessions (newest first)
    bool handleHome(const input::Event& e) {
        int n = 2 + sess_.size();
        bool onSession = homeSel_ >= 2;
        if (!(e.key == input::K_CHAR && e.fn && e.base == 'x')) confirm_ = false;
        switch (e.key) {
            case input::K_BACK: view_ = LIST; loadCampaigns(); sfx::back(); return true;
            case input::K_UP: homeSel_ = (homeSel_ + n - 1) % n; sfx::click(); return true;
            case input::K_DOWN: homeSel_ = (homeSel_ + 1) % n; sfx::click(); return true;
            case input::K_ENTER:
                if (homeSel_ == 0) openDoc(campaign::dirOf(id_) + "/campaign.txt", campaign::CAMPAIGN);
                else if (homeSel_ == 1) newSession();
                else openDoc(sess_[homeSel_ - 2].path, campaign::SESSION);
                return true;
            case input::K_CHAR:
                if (e.fn && e.base == 'x' && onSession) {
                    if (!confirm_) { confirm_ = true; toast("Fn+X again to delete session", C_BLOOD); return true; }
                    confirm_ = false;
                    storage::remove(sess_[homeSel_ - 2].path.c_str());
                    loadSessions();
                    homeSel_ = std::min(homeSel_, 1 + (int)sess_.size());
                    toast("Session deleted", C_DIM);
                } else if (!e.fn && e.base == 'n') newSession();
                else if (!e.fn && e.base == 't') prompt_.open("Campaign name", campTitle_, P_RENAME_CAMPAIGN, 32);
                return true;
            default: return true;
        }
    }

    void newSession() {
        int next = sess_.empty() ? 1 : sess_.front().number + 1;
        prompt_.open(String("Session ") + next + " title", "", P_NEW_SESSION, 32);
    }

    void drawHome() {
        drawHeader(campTitle_.c_str(), ACCENT);
        gfx.setFont(&fonts::Font0);
        const int rowH = 13, y0 = HEADER_H + 4;
        int n = 2 + sess_.size(), rows = (H - FOOTER_H - y0) / rowH;
        int first = std::max(0, std::min(homeSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = y0 + (i - first) * rowH;
            bool s = i == homeSel_;
            if (s) drawPanel(3, y - 2, W - 6, rowH - 1, C_PANEL, ACCENT);
            gfx.setTextDatum(top_left);
            if (i == 0) { gfx.setTextColor(ACCENT); gfx.drawString("Campaign notes", 10, y + 1); continue; }
            if (i == 1) { gfx.setTextColor(C_GOLD); gfx.drawString("+ New session", 10, y + 1); continue; }
            auto& se = sess_[i - 2];
            bool cur = se.path == currentPath_;
            gfx.setTextColor(C_DIM);
            gfx.drawString(String("S") + se.number, 10, y + 1);
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            String st = se.title.length() > 22 ? se.title.substring(0, 21) + "." : se.title;
            gfx.drawString(st, 34, y + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(se.played ? C_EMERALD : C_GOLD);
            gfx.drawString(se.played ? "played" : cur ? "next *" : "planned", W - 9, y + 1);
        }
        drawFooter("Ent open  N new session  T rename  FnX del");
    }

    // ---------------------------------------------------------------- outline
    campaign::Doc doc_;
    struct Row { int sec, item; };   // item -1 = section header
    std::vector<Row> rows_;
    std::vector<bool> folded_;
    int docSel_ = 0, docTop_ = 0;

    void openDoc(const String& path, campaign::Kind k) {
        if (!doc_.load(path, k)) {
            // the campaign file may be missing (e.g. copied by hand); start one
            doc_.init(k);
            doc_.path = path;
            doc_.title = k == campaign::CAMPAIGN ? campTitle_ : String("Session");
        }
        if (k == campaign::SESSION) { campaign::setCurrent(id_, path); currentPath_ = path; }
        folded_.assign(doc_.sections.size(), false);
        docSel_ = docTop_ = 0;
        rebuild();
        view_ = DOC;
        sfx::select();
    }

    void rebuild() {
        rows_.clear();
        folded_.resize(doc_.sections.size(), false);
        for (int s = 0; s < (int)doc_.sections.size(); s++) {
            rows_.push_back({s, -1});
            if (!folded_[s])
                for (int i = 0; i < (int)doc_.sections[s].items.size(); i++) rows_.push_back({s, i});
        }
        docSel_ = std::max(0, std::min(docSel_, (int)rows_.size() - 1));
    }

    void selectRow(int sec, int item) {
        for (int r = 0; r < (int)rows_.size(); r++) if (rows_[r].sec == sec && rows_[r].item == item) { docSel_ = r; return; }
    }

    void changed() { doc_.save(); rebuild(); }

    bool handleDoc(const input::Event& e) {
        int n = rows_.size();
        Row r = rows_[docSel_];
        campaign::Section& sec = doc_.sections[r.sec];
        bool onItem = r.item >= 0;
        if (!(e.key == input::K_CHAR && e.base == 'x')) confirm_ = false;
        switch (e.key) {
            case input::K_BACK:
                doc_.save();
                loadSessions();
                view_ = HOME;
                sfx::back();
                return true;
            case input::K_UP: docSel_ = (docSel_ + n - 1) % n; sfx::click(); return true;
            case input::K_DOWN: docSel_ = (docSel_ + 1) % n; sfx::click(); return true;
            case input::K_LEFT: case input::K_RIGHT: {
                // jump between section headers
                int d = e.key == input::K_LEFT ? -1 : 1;
                int target = (r.sec + d + doc_.sections.size()) % doc_.sections.size();
                if (d < 0 && onItem) target = r.sec;
                selectRow(target, -1);
                sfx::click();
                return true;
            }
            case input::K_ENTER:
                if (onItem) prompt_.open(sec.name, sec.items[r.item].text, P_EDIT_ITEM, ITEM_MAX);
                else { folded_[r.sec] = !folded_[r.sec]; rebuild(); selectRow(r.sec, -1); sfx::click(); }
                return true;
            case input::K_TAB: addItem(); return true;
            default: break;
        }
        if (e.key != input::K_CHAR) return true;
        switch (e.base) {
            case 'a': addItem(); break;
            case ' ':
                if (onItem) {
                    auto& it = sec.items[r.item];
                    it.done = !it.done;
                    changed();
                    toast(it.done ? (String("Marked ") + campaign::checkLabel(sec.name)).c_str() : "Unmarked", it.done ? C_EMERALD : C_DIM);
                    sfx::click();
                }
                break;
            case 'x':
                if (!onItem) break;
                if (!confirm_) { confirm_ = true; toast("X again to delete item", C_BLOOD); break; }
                confirm_ = false;
                sec.items.erase(sec.items.begin() + r.item);
                changed();
                sfx::back();
                break;
            case '-': case '=': {
                if (!onItem) break;
                int to = r.item + (e.base == '-' ? -1 : 1);
                if (to < 0 || to >= (int)sec.items.size()) break;
                std::swap(sec.items[r.item], sec.items[to]);
                changed();
                selectRow(r.sec, to);
                sfx::click();
                break;
            }
            case 'g': generate(r.sec); break;
            case 'r': if (onItem && sec.name == "Encounters") runEncounter(sec.items[r.item]); break;
            case 'p':
                if (doc_.kind == campaign::SESSION) {
                    doc_.played = !doc_.played;
                    changed();
                    toast(doc_.played ? "Session marked played" : "Session marked planned", doc_.played ? C_EMERALD : C_GOLD);
                }
                break;
            case 't': prompt_.open("Title", doc_.title, P_RENAME_DOC, 32); break;
        }
        return true;
    }

    void addItem() {
        Row r = rows_[docSel_];
        if (folded_[r.sec]) { folded_[r.sec] = false; rebuild(); }
        addSec_ = r.sec;
        addAfter_ = r.item;   // -1 = append
        prompt_.open(String("Add to ") + doc_.sections[r.sec].name, "", P_ADD_ITEM, ITEM_MAX);
    }
    int addSec_ = 0, addAfter_ = -1;

    void insertItem(int s, int after, const String& text) {
        auto& items = doc_.sections[s].items;
        int at = after >= 0 ? after + 1 : items.size();
        items.insert(items.begin() + at, {text, false});
        folded_[s] = false;
        changed();
        selectRow(s, at);
    }

    void onPrompt(int tag, String v) {
        v.trim();
        switch (tag) {
            case P_NEW_CAMPAIGN: {
                if (!v.length()) return;
                String id = campaign::create(v);
                if (!id.length()) { sfx::error(); toast("Couldn't create (SD?)", C_BLOOD); return; }
                loadCampaigns();
                for (size_t i = 0; i < camps_.size(); i++) if (camps_[i].id == id) listSel_ = i;
                openCampaign(id);
                break;
            }
            case P_RENAME_CAMPAIGN: {
                if (!v.length()) return;
                campaign::Doc d;
                if (!d.load(campaign::dirOf(id_) + "/campaign.txt", campaign::CAMPAIGN)) {
                    d.init(campaign::CAMPAIGN);
                    d.path = campaign::dirOf(id_) + "/campaign.txt";
                }
                d.title = v;
                d.save();
                campTitle_ = v;
                break;
            }
            case P_NEW_SESSION: {
                String report;
                String path = campaign::newSession(id_, v, report);
                if (!path.length()) { sfx::error(); toast("Couldn't create (SD?)", C_BLOOD); return; }
                loadSessions();
                openDoc(path, campaign::SESSION);
                if (report.length()) toast(report.c_str(), C_ICE);
                break;
            }
            case P_ADD_ITEM:
                if (v.length()) insertItem(addSec_, addAfter_, v);
                break;
            case P_EDIT_ITEM: {
                Row r = rows_[docSel_];
                if (r.item < 0) return;
                auto& items = doc_.sections[r.sec].items;
                if (v.length()) items[r.item].text = v;
                else items.erase(items.begin() + r.item);   // cleared = removed
                changed();
                break;
            }
            case P_RENAME_DOC:
                if (v.length()) { doc_.title = v; changed(); }
                break;
        }
    }

    // ---------------------------------------------------------------- generators
    void generate(int s) {
        String name = doc_.sections[s].name;
        String table;
        if (name == "NPCs") table = "NPC";
        else if (name == "Locations") table = "Location";
        else if (name == "Strong start") table = "Strong start";
        else if (name == "Secrets & clues" || name == "Threads & quests") table = "Rumor";
        else if (name == "Treasure") {
            encounter::Plan plan;
            plan.load();
            int lv = plan.partyLevel;
            table = lv <= 4 ? "Loot (Tier 1, levels 1-4)" : lv <= 10 ? "Loot (Tier 2, levels 5-10)"
                  : lv <= 16 ? "Loot (Tier 3, levels 11-16)" : "Loot (Tier 4, levels 17-20)";
        } else if (name == "Spotlight" || name == "Party") { addParty(s); return; }
        else if (name == "Encounters" && doc_.kind == campaign::SESSION) {
            doc_.save();
            campaign::setCurrent(id_, doc_.path);
            toast("Use 'Add to session plan' there", C_ICE);
            requestApp("Encounters");
            return;
        }
        if (!table.length()) { sfx::error(); toast("Nothing to roll here - A adds", C_DIM); return; }
        String v = rollRandomTable(table);
        if (!v.length()) { sfx::error(); toast("Table missing (SD card?)", C_BLOOD); return; }
        Row r = rows_[docSel_];
        insertItem(s, r.sec == s ? r.item : -1, v);
        sfx::diceRattle();
    }

    void addParty(int s) {
        std::vector<String> files;
        storage::listFiles("/gmtools/characters", files);
        int added = 0;
        for (auto& f : files) {
            dnd::Character ch;
            if (!ch.load(String("/gmtools/characters/") + f)) continue;
            bool listed = false;
            for (auto& it : doc_.sections[s].items) if (it.text.startsWith(ch.name)) listed = true;
            if (listed) continue;
            doc_.sections[s].items.push_back({ch.name + " (" + ch.cls + " " + ch.level + "): ", false});
            added++;
        }
        folded_[s] = false;
        changed();
        toast(added ? (String("Added ") + added + " heroes - Enter to write").c_str() : "No new saved characters", added ? C_EMERALD : C_DIM);
    }

    void runEncounter(campaign::Item& it) {
        String unknown;
        auto list = encounter::parseList(it.text, unknown);
        if (list.empty()) { sfx::error(); toast("No monsters found (e.g. 3x Goblin Warrior)", C_BLOOD); return; }
        int n = encounter::enqueue(list, true);
        it.done = true;
        doc_.save();
        if (unknown.length()) toast((String("Unknown: ") + unknown).c_str(), C_GOLD);
        sfx::crit();
        if (n) requestApp("Initiative");
    }

    // ---------------------------------------------------------------- draw
    void drawDoc() {
        String t = doc_.kind == campaign::SESSION ? String("S") + doc_.number + " " + doc_.title : doc_.title;
        drawHeader(t.c_str(), ACCENT, doc_.kind == campaign::SESSION ? 46 : 0);
        gfx.setFont(&fonts::Font0);
        if (doc_.kind == campaign::SESSION) {
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(doc_.played ? C_EMERALD : C_GOLD);
            gfx.drawString(doc_.played ? "played" : "planned", W - 30, HEADER_H / 2);
        }
        const int rowH = 11, y0 = HEADER_H + 3, detailH = 22;
        int rows = (H - FOOTER_H - detailH - y0) / rowH, n = rows_.size();
        if (docSel_ < docTop_) docTop_ = docSel_;
        if (docSel_ >= docTop_ + rows) docTop_ = docSel_ - rows + 1;
        for (int i = docTop_; i < n && i < docTop_ + rows; i++) {
            const Row& r = rows_[i];
            const campaign::Section& sec = doc_.sections[r.sec];
            int y = y0 + (i - docTop_) * rowH;
            bool s = i == docSel_;
            if (s) gfx.fillRoundRect(2, y - 1, W - 4, rowH, 2, C_PANEL);
            gfx.setTextDatum(top_left);
            if (r.item < 0) {
                int x = 5, my = y + 4;
                if (folded_[r.sec]) gfx.fillTriangle(x, my - 3, x, my + 3, x + 4, my, ACCENT);
                else gfx.fillTriangle(x - 1, my - 2, x + 5, my - 2, x + 2, my + 2, ACCENT);
                gfx.setTextColor(ACCENT);
                gfx.drawString(sec.name, 13, y + 1);
                int done = 0;
                for (auto& it : sec.items) done += it.done;
                gfx.setTextDatum(top_right);
                gfx.setTextColor(C_DIM);
                gfx.drawString(sec.items.empty() ? String("-") : done ? String(done) + "/" + sec.items.size()
                                                                       : String(sec.items.size()), W - 6, y + 1);
                continue;
            }
            const campaign::Item& it = sec.items[r.item];
            gfx.drawRect(14, y + 1, 7, 7, it.done ? C_EMERALD : C_EDGE);
            if (it.done) { gfx.drawLine(15, y + 4, 17, y + 6, C_EMERALD); gfx.drawLine(17, y + 6, 20, y + 1, C_EMERALD); }
            String txt = it.text.length() > 34 ? it.text.substring(0, 33) + "." : it.text;
            gfx.setTextColor(it.done ? C_DIM : s ? C_TEXT : rgb(205, 198, 186));
            gfx.drawString(txt, 25, y + 1);
        }
        // detail strip: full text of the selected item, or the section's hint
        int dy = H - FOOTER_H - detailH + 1;
        gfx.drawFastHLine(4, dy - 2, W - 8, C_EDGE);
        const Row& r = rows_[docSel_];
        const campaign::Section& sec = doc_.sections[r.sec];
        gfx.setTextColor(r.item >= 0 ? C_ICE : C_DIM);
        textWrapped(r.item >= 0 ? sec.items[r.item].text.c_str() : campaign::sectionHint(sec.name), 6, dy, W - 12, 10, 2);
        const char* foot;
        if (r.item < 0) foot = "Ent fold  A add  G roll  </> section";
        else if (sec.name == "Encounters") foot = "R run  Spc tick  Ent edit  A add  XX";
        else foot = "Ent edit  Spc tick  A add  XX del  -= move";
        drawFooter(foot);
    }
};

}  // namespace

App* makeCampaignApp() { return new CampaignApp(); }

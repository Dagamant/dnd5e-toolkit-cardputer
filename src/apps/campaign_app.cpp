#include "tables_api.h"
#include "../core/touch.h"
#include "../core/app.h"
#include "../core/campaign.h"
#include "../core/hold_menu.h"
#include "../core/character.h"
#include "../core/encounter.h"
#include "../core/prompt.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/widgets.h"

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

    // A long-press menu item does what its key does.
    void typeKey(char base, bool fn = false) {
        input::Event e;
        e.key = input::K_CHAR;
        e.ch = e.base = base;
        e.fn = fn;
        handle(e);
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

    bool bigLayout() const override { return true; }

    void draw() override {
        drawBackground();
        if (wide()) view_ == LIST ? drawListWide() : drawWorkspace();
        else if (view_ == LIST) drawList();
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
        latest_.clear();
        if (wide())
            for (auto& c : camps_) {
                auto ss = campaign::sessions(c.id);
                latest_.push_back(ss.empty() ? String("") : String("Latest: S") + ss.back().number + " " + ss.back().title +
                                                                (ss.back().played ? " (played)" : ""));
            }
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
        gfx.setFont(ui::font(&fonts::Font0));
        const int rowH = touchRow(14), pad = touchPad(14), y0 = HEADER_H + u(4);
        int n = camps_.size() + 1, rows = (H - FOOTER_H - y0) / rowH;
        int first = std::max(0, std::min(listSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = y0 + (i - first) * rowH + pad;
            bool s = i == listSel_;
            if (s) drawPanel(u(3), y - pad - u(2), W - u(6), rowH - u(1), C_PANEL, ACCENT);
            touch::row(0, y - pad - u(2), W, rowH, listSel_, i);
            if (i < (int)camps_.size())
                touch::onHold(0, y - pad - u(2), W, rowH, [this, i] {
                    listSel_ = i;
                    holdmenu::open(camps_[i].title, {{"Open", [this, i] { openCampaign(camps_[i].id); }},
                                                     {"Delete with its sessions", [this] { confirm_ = true; typeKey('x', true); }, C_BLOOD, true}});
                });
            gfx.setTextDatum(top_left);
            if (i == (int)camps_.size()) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString("+ New campaign", u(10), y + u(1));
                continue;
            }
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            String t = camps_[i].title;
            if (wide()) t = fitText(t, W - u(80));
            else if (t.length() > 27) t = t.substring(0, 26) + ".";
            gfx.drawString(t, u(10), y + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(camps_[i].sessions) + (camps_[i].sessions == 1 ? " session" : " sessions"), W - u(9), y + u(1));
        }
        if (camps_.empty()) {
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Plan a campaign and its sessions:", W / 2, u(60));
            gfx.drawString("threads, NPCs, secrets, encounters.", W / 2, u(72));
        }
        drawFooter(wide() ? "Ent open  N new  hold for more  Esc back" : "Ent open  N new  FnX delete  Esc back");
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
        if (wide()) {
            // straight into the session being planned, or the campaign notes
            bool cur = false;
            for (auto& se : sess_) cur |= se.path == currentPath_;
            railPos_ = 0;
            openDoc(cur ? currentPath_ : campaign::dirOf(id_) + "/campaign.txt", cur ? campaign::SESSION : campaign::CAMPAIGN);
            return;
        }
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
        gfx.setFont(ui::font(&fonts::Font0));
        const int rowH = touchRow(13), pad = touchPad(13), y0 = HEADER_H + u(4);
        int n = 2 + sess_.size(), rows = (H - FOOTER_H - y0) / rowH;
        int first = std::max(0, std::min(homeSel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = y0 + (i - first) * rowH + pad;
            bool s = i == homeSel_;
            if (s) drawPanel(u(3), y - pad - u(2), W - u(6), rowH - u(1), C_PANEL, ACCENT);
            touch::row(0, y - pad - u(2), W, rowH, homeSel_, i);
            touch::onHold(0, y - pad - u(2), W, rowH, [this, i] {
                homeSel_ = i;
                std::vector<holdmenu::Item> items;
                if (i >= 2) {
                    items.push_back({"Open", [this, i] { openDoc(sess_[i - 2].path, campaign::SESSION); }});
                    items.push_back({"Delete session", [this] { confirm_ = true; typeKey('x', true); }, C_BLOOD, true});
                }
                items.push_back({"New session", [this] { newSession(); }});
                items.push_back({"Rename campaign", [this] { typeKey('t'); }});
                holdmenu::open(i >= 2 ? sess_[i - 2].title : campTitle_, std::move(items));
            });
            gfx.setTextDatum(top_left);
            if (i == 0) { gfx.setTextColor(ACCENT); gfx.drawString("Campaign notes", u(10), y + u(1)); continue; }
            if (i == 1) { gfx.setTextColor(C_GOLD); gfx.drawString("+ New session", u(10), y + u(1)); continue; }
            auto& se = sess_[i - 2];
            bool cur = se.path == currentPath_;
            gfx.setTextColor(C_DIM);
            gfx.drawString(String("S") + se.number, u(10), y + u(1));
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            String st = wide() ? fitText(se.title, W - u(90)) : se.title.length() > 22 ? se.title.substring(0, 21) + "." : se.title;
            gfx.drawString(st, u(34), y + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(se.played ? C_EMERALD : C_GOLD);
            gfx.drawString(se.played ? "played" : cur ? "next *" : "planned", W - u(9), y + u(1));
        }
        drawFooter(wide() ? "Ent open  N new session  hold for more" : "Ent open  N new session  T rename  FnX del");
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
        itemPos_ = 0;
        rebuild();
        view_ = DOC;
        sfx::select();
    }

    void rebuild() {
        rows_.clear();
        folded_.resize(doc_.sections.size(), false);
        for (int s = 0; s < (int)doc_.sections.size(); s++) {
            rows_.push_back({s, -1});
            if (wide() || !folded_[s])   // the big screen shows one section at a time instead
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
                if (wide()) { view_ = LIST; loadCampaigns(); }
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
                else if (wide()) addItem();
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
                if (wide()) stayIn(r);
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
                    loadSessions();
                    toast(doc_.played ? "Session marked played" : "Session marked planned", doc_.played ? C_EMERALD : C_GOLD);
                }
                break;
            case 't': prompt_.open("Title", doc_.title, P_RENAME_DOC, 32); break;
        }
        return true;
    }

    void holdOutline(int i) {
        docSel_ = i;
        const Row r = rows_[i];
        const campaign::Section& sec = doc_.sections[r.sec];
        std::vector<holdmenu::Item> items;
        if (r.item >= 0) {
            const bool done = sec.items[r.item].done;
            if (sec.name == "Encounters") items.push_back({"Run encounter", [this] { typeKey('r'); }});
            items.push_back({"Edit", [this] {
                input::Event e;
                e.key = input::K_ENTER;
                handle(e);
            }});
            items.push_back({done ? "Unmark" : String("Mark ") + campaign::checkLabel(sec.name), [this] { typeKey(' '); }});
            items.push_back({"Add below", [this] { addItem(); }});
            if (r.item > 0) items.push_back({"Move up", [this] { typeKey('-'); }});
            if (r.item + 1 < (int)sec.items.size()) items.push_back({"Move down", [this] { typeKey('='); }});
            items.push_back({"Delete", [this] { confirm_ = true; typeKey('x'); }, C_BLOOD, true});
        } else {
            items.push_back({"Add item", [this] { addItem(); }});
            items.push_back({"Roll ideas", [this] { typeKey('g'); }});
        }
        if (doc_.kind == campaign::SESSION) items.push_back({doc_.played ? "Mark planned" : "Mark played", [this] { typeKey('p'); }});
        items.push_back({"Rename", [this] { typeKey('t'); }});
        holdmenu::open(r.item >= 0 ? sec.items[r.item].text : sec.name, std::move(items));
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
                if (view_ == DOC && doc_.kind == campaign::CAMPAIGN) doc_.title = v;   // or saving it would undo this
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
                if (wide()) stayIn(r);
                break;
            }
            case P_RENAME_DOC:
                if (!v.length()) break;
                doc_.title = v;
                changed();
                if (doc_.kind == campaign::CAMPAIGN) campTitle_ = v;
                loadSessions();
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
        drawHeader(t.c_str(), ACCENT, doc_.kind == campaign::SESSION ? u(46) : 0);
        gfx.setFont(ui::font(&fonts::Font0));
        if (doc_.kind == campaign::SESSION) {
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(doc_.played ? C_EMERALD : C_GOLD);
            gfx.drawString(doc_.played ? "played" : "planned", W - u(30), HEADER_H / 2);
        }
        // big screen: the outline on the left, the selected item's full text on the right
        const int listW = wide() ? W * 3 / 5 : W;
        const int rowH = touchRow(11), pad = touchPad(11), y0 = HEADER_H + u(3), detailH = wide() ? 0 : 22;
        int rows = (H - FOOTER_H - detailH - y0) / rowH, n = rows_.size();
        if (docSel_ < docTop_) docTop_ = docSel_;
        if (docSel_ >= docTop_ + rows) docTop_ = docSel_ - rows + 1;
        for (int i = docTop_; i < n && i < docTop_ + rows; i++) {
            const Row& r = rows_[i];
            const campaign::Section& sec = doc_.sections[r.sec];
            int y = y0 + (i - docTop_) * rowH + pad;
            bool s = i == docSel_;
            if (s) gfx.fillRoundRect(u(2), y - pad - u(1), listW - u(4), rowH, u(2), C_PANEL);
            touch::pick(0, y - pad - u(1), listW, rowH, docSel_, i);
            touch::onHold(0, y - pad - u(1), listW, rowH, [this, i] { holdOutline(i); });
            gfx.setTextDatum(top_left);
            if (r.item < 0) {
                int x = u(5), my = y + u(4);
                if (folded_[r.sec]) gfx.fillTriangle(x, my - u(3), x, my + u(3), x + u(4), my, ACCENT);
                else gfx.fillTriangle(x - u(1), my - u(2), x + u(5), my - u(2), x + u(2), my + u(2), ACCENT);
                gfx.setTextColor(ACCENT);
                gfx.drawString(sec.name, u(13), y + u(1));
                int done = 0;
                for (auto& it : sec.items) done += it.done;
                gfx.setTextDatum(top_right);
                gfx.setTextColor(C_DIM);
                gfx.drawString(sec.items.empty() ? String("-") : done ? String(done) + "/" + sec.items.size()
                                                                       : String(sec.items.size()), listW - u(6), y + u(1));
                continue;
            }
            const campaign::Item& it = sec.items[r.item];
            gfx.drawRect(u(14), y + u(1), u(7), u(7), it.done ? C_EMERALD : C_EDGE);
            if (it.done) { gfx.drawLine(u(15), y + u(4), u(17), y + u(6), C_EMERALD); gfx.drawLine(u(17), y + u(6), u(20), y + u(1), C_EMERALD); }
            String txt = wide() ? fitText(it.text, listW - u(30)) : it.text.length() > 34 ? it.text.substring(0, 33) + "." : it.text;
            gfx.setTextColor(it.done ? C_DIM : s ? C_TEXT : rgb(205, 198, 186));
            gfx.drawString(txt, u(25), y + u(1));
        }
        // detail strip: full text of the selected item, or the section's hint
        if (wide()) {
            const Row& r = rows_[docSel_];
            const campaign::Section& sec = doc_.sections[r.sec];
            int px = listW + u(2), py = y0 - u(1), pw = W - px - u(4), ph = H - FOOTER_H - py - u(3);
            drawPanel(px, py, pw, ph, C_BG2, C_EDGE);
            gfx.setFont(metrics().title);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(ACCENT);
            gfx.drawString(fitText(sec.name, pw - u(12)), px + u(6), py + u(4));
            gfx.setFont(metrics().body);
            gfx.setTextColor(r.item >= 0 ? C_TEXT : C_DIM);
            textWrapped(r.item >= 0 ? sec.items[r.item].text.c_str() : campaign::sectionHint(sec.name), px + u(6),
                        py + u(20), pw - u(12), metrics().line, (ph - u(24)) / metrics().line);
        }
        int dy = H - FOOTER_H - detailH + 1;
        if (!wide()) gfx.drawFastHLine(4, dy - 2, W - 8, C_EDGE);
        const Row& r = rows_[docSel_];
        const campaign::Section& sec = doc_.sections[r.sec];
        gfx.setTextColor(r.item >= 0 ? C_ICE : C_DIM);
        if (!wide()) textWrapped(r.item >= 0 ? sec.items[r.item].text.c_str() : campaign::sectionHint(sec.name), 6, dy, W - 12, 10, 2);
        const char* foot;
        if (wide()) foot = r.item < 0 ? "Ent fold  hold for more  </> section" : "Ent edit  Spc tick  hold for more";
        else if (r.item < 0) foot = "Ent fold  A add  G roll  </> section";
        else if (sec.name == "Encounters") foot = "R run  Spc tick  Ent edit  A add  XX";
        else foot = "Ent edit  Spc tick  A add  XX del  -= move";
        drawFooter(foot);
    }

    // ---------------------------------------------------------------- big screen
    // Campaigns as cards; inside one, three columns: the sessions, the open
    // plan's sections, and the chosen section's items as cards with a box to
    // tick. Tapping an item edits it; holding one opens its menu.
    std::vector<String> latest_;   // per campaign: its newest session, for the cards
    float listPos_ = 0, railPos_ = 0, itemPos_ = 0;
    int shownSec_ = -1, shownSel_ = -1;

    // After an item is removed: its neighbour, so the section stays on screen.
    void stayIn(Row r) {
        const int n = doc_.sections[r.sec].items.size();
        selectRow(r.sec, n ? std::min(r.item, n - 1) : -1);
    }

    int rowOf(int sec, int item) const {
        for (int r = 0; r < (int)rows_.size(); r++) if (rows_[r].sec == sec && rows_[r].item == item) return r;
        return 0;
    }

    // Sections with something to roll or fetch, and what the button says.
    const char* rollLabel(const String& n) const {
        if (n == "Spotlight" || n == "Party") return "Add party";
        if (n == "Encounters") return doc_.kind == campaign::SESSION ? "Plan fight" : nullptr;
        if (n == "NPCs" || n == "Locations" || n == "Strong start" || n == "Secrets & clues" || n == "Threads & quests" ||
            n == "Treasure")
            return "Roll idea";
        return nullptr;
    }

    // The section hint without its key hints ("(G rolls one)", "- R runs it").
    static String shortHint(const String& n) {
        String h = campaign::sectionHint(n);
        if (h == "A to add") return "";
        int p = h.indexOf(" (");
        if (p < 0) p = h.indexOf(" - R");
        return p < 0 ? h : h.substring(0, p);
    }

    void switchDoc(const String& path, campaign::Kind k) {
        if (view_ == DOC) {
            if (path == doc_.path) return;
            doc_.save();
        }
        openDoc(path, k);
    }

    void togglePlayed(const String& path) {
        if (path == doc_.path) doc_.played = !doc_.played, doc_.save();
        else {
            campaign::Doc d;
            if (!d.load(path, campaign::SESSION)) return;
            d.played = !d.played;
            d.save();
        }
        loadSessions();
        sfx::click();
    }

    void deleteSession(const String& path) {
        storage::remove(path.c_str());
        // the open plan is gone: show the campaign notes (without saving it back)
        if (path == doc_.path) openDoc(campaign::dirOf(id_) + "/campaign.txt", campaign::CAMPAIGN);
        loadSessions();
        toast("Session deleted", C_DIM);
        sfx::back();
    }

    void drawListWide() {
        const Metrics& m = metrics();
        drawHeader("Campaigns", ACCENT);
        const int top = HEADER_H + 10, bottom = H - FOOTER_H - 10, n = camps_.size();
        widgets::button(16, top, 380, 56, "+ New campaign", listSel_ == n, C_GOLD, [this] { newCampaign(); });
        const int gy = top + 70, gh = bottom - gy, cw = (W - 48) / 2, ch = 132, gap = 14;
        const float maxPos = std::max(0, (n + 1) / 2 * (ch + gap) - gap - gh);
        listPos_ = std::max(0.0f, std::min(listPos_, maxPos + 60));
        touch::scroller(16, gy, W - 32, gh, &listPos_, maxPos);
        gfx.setClipRect(16, gy, W - 32, gh);
        for (int i = 0; i < n; i++) {
            const int x = 16 + (i % 2) * (cw + 16), y = gy + (i / 2) * (ch + gap) - (int)listPos_;
            if (y + ch < gy || y > gy + gh) continue;
            const bool on = i == listSel_;
            gfx.fillRoundRect(x, y, cw, ch, 12, on ? C_PANEL : C_BG2);
            gfx.drawRoundRect(x, y, cw, ch, 12, on ? ACCENT : C_EDGE);
            gfx.fillRoundRect(x, y, 8, ch, 4, ACCENT);
            gfx.setTextDatum(top_left);
            gfx.setFont(m.title);
            gfx.setTextColor(on ? ACCENT : C_TEXT);
            gfx.drawString(fitText(camps_[i].title, cw - 44), x + 24, y + 14);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            const int ns = camps_[i].sessions;
            gfx.drawString(ns ? String(ns) + (ns == 1 ? " session" : " sessions") : String("No sessions yet"), x + 24, y + 64);
            if (i < (int)latest_.size() && latest_[i].length()) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString(fitText(latest_[i], cw - 44), x + 24, y + 94);
            }
            const int vy = std::max(y, gy), vh = std::min(y + ch, gy + gh) - vy;
            if (vh <= 0) continue;
            touch::zone(x, vy, cw, vh, [this, i] { listSel_ = i; openCampaign(camps_[i].id); });
            touch::onHold(x, vy, cw, vh, [this, i] {
                listSel_ = i;
                holdmenu::open(camps_[i].title, {{"Open", [this, i] { openCampaign(camps_[i].id); }},
                                                 {"Delete with its sessions", [this] { confirm_ = true; typeKey('x', true); }, C_BLOOD, true}});
            });
        }
        gfx.clearClipRect();
        if (!n) {
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Plan a campaign and its sessions:", W / 2, gy + gh / 2 - 20);
            gfx.drawString("threads, NPCs, secrets, encounters.", W / 2, gy + gh / 2 + 20);
        }
        drawFooter("Ent open  N new  hold for more  Esc back");
    }

    void drawWorkspace() {
        drawHeader(campTitle_.c_str(), ACCENT);
        const int top = HEADER_H + 10, h = H - FOOTER_H - 10 - top;
        const int AX = 16, AW = 270, SX = AX + AW + 12, SW = 280, IX = SX + SW + 12, IW = W - IX - 16;
        drawRail(AX, top, AW, h);
        if (rows_.empty()) return;
        drawSections(SX, top, SW, h);
        drawItems(IX, top, IW, h);
        drawFooter(rows_[docSel_].item < 0 ? "Ent add  hold for more  </> section  Esc back"
                                           : "Ent edit  Spc tick  hold for more  Esc back");
    }

    // The campaign notes, a new session, and the sessions, newest first.
    void drawRail(int x, int y, int w, int h) {
        const Metrics& m = metrics();
        widgets::button(x, y, w, 56, "Campaign notes", doc_.kind == campaign::CAMPAIGN, ACCENT,
                        [this] { switchDoc(campaign::dirOf(id_) + "/campaign.txt", campaign::CAMPAIGN); });
        widgets::button(x, y + 66, w, 56, "+ New session", false, C_GOLD, [this] { newSession(); });
        const int ly = y + 134, lh = y + h - ly, rowH = 84, n = sess_.size();
        const float maxPos = std::max(0, n * rowH - 8 - lh);
        railPos_ = std::max(0.0f, std::min(railPos_, maxPos + 60));
        touch::scroller(x, ly, w, lh, &railPos_, maxPos);
        gfx.setClipRect(x, ly, w, lh);
        for (int i = 0; i < n; i++) {
            const int cy = ly + i * rowH - (int)railPos_;
            if (cy + rowH < ly || cy > ly + lh) continue;
            const campaign::SessionInfo& se = sess_[i];
            const bool on = doc_.kind == campaign::SESSION && se.path == doc_.path;
            gfx.fillRoundRect(x, cy, w, rowH - 8, 10, on ? C_PANEL : C_BG2);
            gfx.drawRoundRect(x, cy, w, rowH - 8, 10, on ? ACCENT : C_EDGE);
            gfx.setTextDatum(top_left);
            gfx.setFont(m.bold);
            gfx.setTextColor(on ? ACCENT : C_TEXT);
            gfx.drawString(fitText(se.title, w - 28), x + 14, cy + 10);
            const String num = String("S") + se.number;
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String("Session ") + se.number, x + 14, cy + 46);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(se.played ? C_EMERALD : se.path == currentPath_ ? C_GOLD : C_DIM);
            gfx.drawString(se.played ? "played" : se.path == currentPath_ ? "current" : "planned", x + w - 14, cy + 46);
            const int vy = std::max(cy, ly), vh = std::min(cy + rowH - 8, ly + lh) - vy;
            if (vh <= 0) continue;
            const String path = se.path;
            touch::zone(x, vy, w, vh, [this, path] { switchDoc(path, campaign::SESSION); });
            touch::onHold(x, vy, w, vh, [this, path, num, se] {
                holdmenu::open(num + " " + se.title,
                               {{"Open", [this, path] { switchDoc(path, campaign::SESSION); }},
                                {se.played ? "Mark planned" : "Mark played", [this, path] { togglePlayed(path); }},
                                {"Delete session", [this, path] { deleteSession(path); }, C_BLOOD, true}});
            });
        }
        gfx.clearClipRect();
        if (!n) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("No sessions yet.", x + w / 2, ly + 16);
        }
    }

    // The open plan's sections; the chosen one is shown on the right.
    void drawSections(int x, int y, int w, int h) {
        const Metrics& m = metrics();
        const int n = doc_.sections.size(), cur = rows_[docSel_].sec;
        const int rh = std::min(66, (h + 8) / n);
        for (int s = 0; s < n; s++) {
            const campaign::Section& sec = doc_.sections[s];
            const int ry = y + s * rh;
            const bool on = s == cur;
            gfx.fillRoundRect(x, ry, w, rh - 8, 10, on ? C_PANEL : C_BG2);
            gfx.drawRoundRect(x, ry, w, rh - 8, 10, on ? ACCENT : C_EDGE);
            if (on) gfx.fillRoundRect(x, ry, 8, rh - 8, 4, ACCENT);
            int done = 0;
            for (auto& it : sec.items) done += it.done;
            const String count = sec.items.empty() ? String("") : done ? String(done) + "/" + sec.items.size()
                                                                        : String(sec.items.size());
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(done && done == (int)sec.items.size() ? C_EMERALD : C_DIM);
            gfx.drawString(count, x + w - 14, ry + (rh - 8) / 2 + 1);
            const int nameW = w - 34 - (count.length() ? gfx.textWidth(count) + 10 : 0);
            gfx.setFont(m.body);
            if (gfx.textWidth(sec.name) > nameW) gfx.setFont(m.small);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(on ? ACCENT : C_TEXT);
            gfx.drawString(fitText(sec.name, nameW), x + 20, ry + (rh - 8) / 2 + 1);
            touch::zone(x, ry, w, rh - 8, [this, s] { selectRow(s, -1); sfx::click(); });
            touch::onHold(x, ry, w, rh - 8, [this, s] { holdOutline(rowOf(s, -1)); });
        }
    }

    // Above: the plan's name, Played and Rename. Below: the section's items.
    void drawItems(int x, int y, int w, int h) {
        using namespace widgets;
        const Metrics& m = metrics();
        const int si = rows_[docSel_].sec, selItem = rows_[docSel_].item;
        const campaign::Section& sec = doc_.sections[si];
        const bool session = doc_.kind == campaign::SESSION;

        int bx = x + w;
        bx -= 130;
        button(bx, y, 130, 52, "Rename", false, ACCENT, [this] { typeKey('t'); });
        if (session) {
            bx -= 200;
            button(bx, y, 190, 52, doc_.played ? "Played" : "Mark played", doc_.played, C_EMERALD, [this] { typeKey('p'); });
        }
        gfx.setFont(m.bold);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_TEXT);
        const String dt = session ? String("S") + doc_.number + " " + doc_.title : String("Campaign notes");
        gfx.drawString(fitText(dt, bx - x - 16), x + 4, y + 27);

        const int py = y + 64, ph = h - 64;
        drawPanel(x, py, w, ph, C_BG2, C_EDGE);
        bx = x + w - 12 - 120;
        button(bx, py + 12, 120, 52, "+ Add", false, C_GOLD, [this, si] { selectRow(si, -1); addItem(); });
        if (const char* rl = rollLabel(sec.name)) {
            bx -= 160;
            button(bx, py + 12, 150, 52, rl, false, ACCENT, [this, si] { selectRow(si, -1); generate(si); });
        }
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(ACCENT);
        gfx.drawString(fitText(sec.name, bx - x - 30), x + 18, py + 10);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString(fitText(shortHint(sec.name), bx - x - 30), x + 18, py + 52);

        // the items, as cards that grow with their text
        const int lx = x + 12, lw = w - 24, ly = py + 84, lh = py + ph - 10 - ly;
        const bool enc = sec.name == "Encounters";
        const int tw = lw - 70 - (enc ? 104 : 16);
        gfx.setFont(m.body);
        std::vector<std::vector<String>> lines(sec.items.size());
        std::vector<int> tops(sec.items.size() + 1, 0);
        for (size_t i = 0; i < sec.items.size(); i++) {
            wrapLines(sec.items[i].text, tw, lines[i]);
            if (lines[i].empty()) lines[i].push_back("");
            tops[i + 1] = tops[i] + std::max(64, (int)lines[i].size() * m.line + 26) + 10;
        }
        const int total = tops.back() - (sec.items.empty() ? 0 : 10);
        const float maxPos = std::max(0, total - lh);
        // a new section starts at the top; the keyboard's choice stays in view
        if (si != shownSec_) itemPos_ = 0;
        else if (docSel_ != shownSel_ && selItem >= 0) {
            itemPos_ = std::min(itemPos_, (float)tops[selItem]);
            itemPos_ = std::max(itemPos_, (float)(tops[selItem + 1] - 10 - lh));
        }
        shownSec_ = si;
        shownSel_ = docSel_;
        itemPos_ = std::max(0.0f, std::min(itemPos_, maxPos + 60));
        touch::scroller(lx, ly, lw, lh, &itemPos_, maxPos);
        gfx.setClipRect(lx, ly, lw, lh);
        for (int i = 0; i < (int)sec.items.size(); i++) {
            const int cy = ly + tops[i] - (int)itemPos_, ch = tops[i + 1] - tops[i] - 10;
            if (cy + ch < ly || cy > ly + lh) continue;
            const campaign::Item& it = sec.items[i];
            const bool on = i == selItem;
            gfx.fillRoundRect(lx, cy, lw, ch, 10, on ? C_PANEL : C_BG);
            gfx.drawRoundRect(lx, cy, lw, ch, 10, on ? ACCENT : C_EDGE);
            // the tick box
            const int bxx = lx + 16, byy = cy + 16;
            if (it.done) {
                gfx.fillRoundRect(bxx, byy, 34, 34, 6, C_EMERALD);
                gfx.drawWideLine(bxx + 7, byy + 18, bxx + 14, byy + 26, 3, C_BG);
                gfx.drawWideLine(bxx + 14, byy + 26, bxx + 27, byy + 9, 3, C_BG);
            } else {
                gfx.drawRoundRect(bxx, byy, 34, 34, 6, C_EDGE);
                gfx.drawRoundRect(bxx + 1, byy + 1, 32, 32, 5, C_EDGE);
            }
            gfx.setFont(m.body);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(it.done ? C_DIM : C_TEXT);
            for (size_t k = 0; k < lines[i].size(); k++) gfx.drawString(lines[i][k], lx + 70, cy + 15 + k * m.line);
            const int vy = std::max(cy, ly), vh = std::min(cy + ch, ly + lh) - vy;
            if (vh <= 0) continue;
            touch::zone(lx + 70, vy, lw - 70, vh, [this, si, i] {
                selectRow(si, i);
                input::Event e;
                e.key = input::K_ENTER;
                handle(e);
            });
            touch::zone(lx, vy, 70, vh, [this, si, i] { selectRow(si, i); typeKey(' '); });
            touch::onHold(lx, vy, lw, vh, [this, si, i] { holdOutline(rowOf(si, i)); });
            if (enc) {
                const int rby = std::max(ly, cy + (ch - 48) / 2);
                if (rby + 48 <= ly + lh)
                    button(lx + lw - 100, rby, 88, 48, it.done ? "Again" : "Run", false, C_BLOOD,
                           [this, si, i] { selectRow(si, i); typeKey('r'); });
            }
        }
        gfx.clearClipRect();
        if (sec.items.empty()) {
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Nothing here yet.", x + w / 2, ly + lh / 2 - 18);
            gfx.setFont(m.small);
            gfx.drawString(rollLabel(sec.name) ? "Tap + Add to write one, or let the dice help." : "Tap + Add to write one.",
                           x + w / 2, ly + lh / 2 + 22);
        }
    }
};

}  // namespace

App* makeCampaignApp() { return new CampaignApp(); }

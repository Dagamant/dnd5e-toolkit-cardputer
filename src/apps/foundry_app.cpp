#include "../core/app.h"
#include "../core/touch.h"
#include "../core/foundry.h"
#include "../core/hold_menu.h"
#include "../core/widgets.h"
#include "../board/board.h"
#include "../core/sfx.h"
#include "../core/textview.h"
#include "../core/ui.h"
#include "../core/web.h"

using namespace ui;

// Foundry VTT: shows what the "Cardputer Link" Foundry module sends
// (core/foundry.h): party sheets, the combat tracker, rolls, the chat log and
// journal pages. Read-only. Needs web access running on the home network.

namespace {

constexpr uint16_t ACCENT = rgb(240, 120, 50);
const char* TABS[] = {"Party", "Combat", "Rolls", "Chat", "Journal"};
enum { T_PARTY, T_COMBAT, T_ROLLS, T_CHAT, T_JOURNAL, NTABS };
enum View { LIST, SHEET, PAGE };

// A record or line inside one of foundry::text()'s strings. Offsets instead
// of copies: a big world's journal list would not fit in RAM twice.
struct Span { uint16_t start, end; };

struct Actor { String id, name, sub; int hp, max, temp, ac; };
struct Combatant { String name, init, flags, conds; int hp, max, ac; };
// fields of one chat message, read when needed
struct Msg { String id, time, speaker, flavor, formula, total, dice, flags, text; };

// Splits t at every `sep` (reserving exactly, so the vector never regrows).
void split(const String& t, const char* sep, std::vector<Span>& out) {
    out.clear();
    int seplen = strlen(sep), len = t.length(), n = 1;
    for (int p = 0; (p = t.indexOf(sep, p)) >= 0; p += seplen) n++;
    out.reserve(n);
    for (int start = 0; start < len;) {
        int at = t.indexOf(sep, start);
        out.push_back({(uint16_t)start, (uint16_t)(at < 0 ? len : at)});
        if (at < 0) break;
        start = at + seplen;
    }
}

// First line of a record, and the rest of it.
String headOf(const String& t, Span s) {
    int nl = t.indexOf('\n', s.start);
    return t.substring(s.start, nl < 0 || nl > s.end ? s.end : nl);
}
Span bodyOf(const String& t, Span s) {
    int nl = t.indexOf('\n', s.start);
    if (nl < 0 || nl >= s.end) return {s.end, s.end};
    return {(uint16_t)(nl + 1), s.end};
}
String textOf(const String& t, Span s) { return t.substring(s.start, s.end); }

bool has(const String& flags, char c) { return flags.indexOf(c) >= 0; }

class FoundryApp : public App {
public:
    const char* name() const override { return "Foundry"; }
    const char* blurb() const override { return "Party, combat & chat from Foundry VTT."; }
    uint16_t accent() const override { return ACCENT; }

    void drawIcon(int cx, int cy, float t) override {
        // an anvil with a spark
        gfx.fillRoundRect(cx - 16, cy - 6, 32, 7, 2, ACCENT);
        gfx.fillTriangle(cx - 16, cy - 6, cx - 24, cy - 6, cx - 16, cy + 1, ACCENT);
        gfx.fillRect(cx - 6, cy + 1, 12, 7, C_GOLD_DK);
        gfx.fillRoundRect(cx - 14, cy + 8, 28, 5, 2, ACCENT);
        int k = (int)(t * 4) % 4;
        for (int i = 0; i < 3; i++) {
            int a = i * 50 + k * 8;
            gfx.drawLine(cx + 4, cy - 9, cx + 4 + (i - 1) * 7, cy - 15 - (a % 5), i == k % 3 ? C_GOLD : C_EDGE);
        }
    }

    void onEnter() override {
        foundry::key();   // made now, so the setup screen can show it
        view_ = LIST;
        for (auto& v : seen_) v = UINT32_MAX;
        firstChat_ = true;
        tick(0);
    }

    void onExit() override {
        actors_ = {}; fighters_ = {}; msgs_ = {}; rolls_ = {}; pages_ = {}; vis_ = {};
        tv_.clear();
    }

    void tick(uint32_t) override {
        try {
            if (changed(foundry::ACTORS)) parseActors();
            if (changed(foundry::COMBAT)) parseCombat();
            if (changed(foundry::CHAT)) parseChat();
            if (changed(foundry::JOURNALS)) parseJournals();
            if (changed(foundry::PAGE) && view_ == PAGE) showPage();
            if (changed(foundry::SHEET) && view_ == SHEET) showSheet();
        } catch (const std::bad_alloc&) {   // drop what we hold rather than crash
            actors_ = {}; msgs_ = {}; rolls_ = {}; pages_ = {}; vis_ = {};
            tv_.clear();
            view_ = LIST;
            toast("Out of memory - Foundry sent too much", C_BLOOD);
        }
        if (view_ != LIST && loading_ && millis() - asked_ > 8000) { loading_ = false; buildPageMissing(); }
    }

    bool handle(const input::Event& e) override {
        if (showKey_) {
            if (e.key == input::K_CHAR && e.fn && e.base == 'n') { foundry::newKey(); toast("New key - enter it in Foundry"); }
            else showKey_ = false;
            return true;
        }
        if (!foundry::everConnected() || web::state() != web::RUNNING) {   // setup screen
            if (e.key == input::K_BACK) return false;
            if (e.key == input::K_CHAR && e.fn && e.base == 'n') { foundry::newKey(); sfx::select(); }
            return true;
        }
        if (e.key == input::K_BACK) {
            if (view_ != LIST) { view_ = LIST; sfx::back(); return true; }
            if (tab_ == T_JOURNAL && filter_.length()) { filter_ = ""; refilter(); return true; }
            return false;
        }
        if (view_ == SHEET || view_ == PAGE) return handleText(e);
        switch (e.key) {
            case input::K_LEFT: tab_ = (tab_ + NTABS - 1) % NTABS; enterTab(); return true;
            case input::K_RIGHT: case input::K_TAB: tab_ = (tab_ + 1) % NTABS; enterTab(); return true;
            case input::K_UP: move(-1); return true;
            case input::K_DOWN: move(1); return true;
            case input::K_ENTER: open(); return true;
            case input::K_DEL:
                if (tab_ == T_JOURNAL && filter_.length()) { filter_.remove(filter_.length() - 1); refilter(); }
                return true;
            case input::K_CHAR:
                if (tab_ == T_JOURNAL && !e.fn) {
                    if (filter_.length() < 20) { filter_ += (char)tolower(e.ch); refilter(); }
                } else if (e.base == 'k') {
                    showKey_ = true;
                } else if (e.base == 'r') {
                    foundry::requestFull();
                    toast("Asked Foundry to send everything");
                }
                return true;
            default: return true;
        }
    }

    bool bigLayout() const override { return true; }

    void draw() override {
        tick(0);   // web access may have replaced a text since tick(): offsets must match it
        drawBackground();
        if (!foundry::everConnected() || web::state() != web::RUNNING) { drawSetup(); drawToast(); return; }
        if (view_ == SHEET) drawSheet();
        else if (view_ == PAGE) drawPage();
        else drawTabs();
        if (showKey_) drawKey();
        drawToast();
    }

private:
    // ------------------------------------------------------------ data
    bool changed(foundry::Section s) {
        uint32_t v = foundry::version(s);
        if (v == seen_[s]) return false;
        seen_[s] = v;
        return true;
    }

    void parseActors() {
        const String& t = foundry::text(foundry::ACTORS);
        std::vector<Span> ls;
        split(t, "\n", ls);
        actors_ = {};
        actors_.reserve(ls.size());
        String f[7];
        for (auto& l : ls) {
            foundry::fields(textOf(t, l), f, 7);
            if (f[1].length())
                actors_.push_back({f[0], f[1], f[2], (int)f[3].toInt(), (int)f[4].toInt(), (int)f[5].toInt(), (int)f[6].toInt()});
        }
    }

    void parseCombat() {
        const String& t = foundry::text(foundry::COMBAT);
        std::vector<Span> ls;
        split(t, "\n", ls);
        fighters_ = {};
        round_ = 0;
        if (ls.empty()) return;
        fighters_.reserve(ls.size() - 1);
        String f[7];
        foundry::fields(textOf(t, ls[0]), f, 3);
        round_ = f[0].toInt();
        for (size_t i = 1; i < ls.size(); i++) {
            foundry::fields(textOf(t, ls[i]), f, 7);
            fighters_.push_back({f[0], f[1], f[5], f[6], (int)f[2].toInt(), (int)f[3].toInt(), (int)f[4].toInt()});
        }
        // follow the current turn
        for (int i = 0; i < (int)fighters_.size(); i++)
            if (has(fighters_[i].flags, 'c')) sel_[T_COMBAT] = i;
    }

    Msg msgAt(int i) {
        const String& t = foundry::text(foundry::CHAT);
        Msg m;
        String f[8];
        foundry::fields(headOf(t, msgs_[i]), f, 8);
        m = {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], textOf(t, bodyOf(t, msgs_[i]))};
        return m;
    }

    void parseChat() {
        String lastId = msgs_.empty() ? String("") : msgAt(msgs_.size() - 1).id;
        split(foundry::text(foundry::CHAT), "\n\x1e\n", msgs_);
        rolls_ = {};
        rolls_.reserve(msgs_.size());
        for (int i = (int)msgs_.size() - 1; i >= 0; i--)
            if (msgAt(i).formula.length()) rolls_.push_back(i);   // newest first
        // announce a new roll (not the ones there when the app opened)
        if (!firstChat_ && !msgs_.empty()) {
            Msg m = msgAt(msgs_.size() - 1);
            if (m.id != lastId && m.formula.length()) {
                if (has(m.flags, 'c')) sfx::crit();
                else if (has(m.flags, 'f')) sfx::fumble();
                else sfx::land();
                if (tab_ != T_ROLLS || view_ != LIST)
                    toast((m.speaker + ": " + m.total).c_str(), has(m.flags, 'c') ? C_GOLD : has(m.flags, 'f') ? C_BLOOD : C_TEXT);
                sel_[T_ROLLS] = 0;
            }
        }
        firstChat_ = false;
        if (tab_ == T_CHAT && view_ == LIST) buildChat();
    }

    void parseJournals() {
        split(foundry::text(foundry::JOURNALS), "\n", pages_);
        refilter();
    }

    // entry id, page id, folder, entry name, page name
    void pageAt(int i, String* f) { foundry::fields(textOf(foundry::text(foundry::JOURNALS), pages_[i]), f, 5); }

    // The journal pages matching the filter (all of them without one).
    void refilter() {
        vis_ = {};
        vis_.reserve(pages_.size());
        String f[5];
        for (int i = 0; i < (int)pages_.size(); i++) {
            if (filter_.length()) {
                String all = textOf(foundry::text(foundry::JOURNALS), pages_[i]);
                all.toLowerCase();
                int tab = all.indexOf('\t', all.indexOf('\t') + 1);   // skip the two ids
                if (all.indexOf(filter_, tab + 1) < 0) continue;
            }
            vis_.push_back(i);
        }
        sel_[T_JOURNAL] = std::min(sel_[T_JOURNAL], std::max(0, (int)vis_.size() - 1));
        if (filter_.length()) sel_[T_JOURNAL] = 0;
    }

    // ------------------------------------------------------------ actions
    void enterTab() {
        sfx::click();
        if (tab_ == T_CHAT) buildChat();
    }

    int count() {
        switch (tab_) {
            case T_PARTY: return actors_.size();
            case T_COMBAT: return fighters_.size();
            case T_ROLLS: return rolls_.size();
            case T_JOURNAL: return vis_.size();
            default: return 0;
        }
    }

    void move(int d) {
        if (tab_ == T_CHAT) { tv_.scroll(d); return; }
        int n = count();
        if (!n) return;
        sel_[tab_] = std::max(0, std::min(n - 1, sel_[tab_] + d));
        sfx::click();
    }

    void open() {
        if (tab_ == T_PARTY && sel_[tab_] < (int)actors_.size()) {
            buildSheet(sel_[tab_]);
            view_ = SHEET;
            sfx::select();
        } else if (tab_ == T_JOURNAL) {
            if (sel_[tab_] >= (int)vis_.size()) return;
            String f[5];
            pageAt(vis_[sel_[tab_]], f);
            foundry::requestPage(f[0], f[1]);
            wantEntry_ = f[0];
            wantPage_ = f[1];
            pageTitle_ = f[4].length() && f[4] != f[3] ? f[4] : f[3];
            loading_ = true;
            asked_ = millis();
            tv_.clear();
            view_ = PAGE;
            sfx::select();
        }
    }

    bool handleText(const input::Event& e) {
        switch (e.key) {
            case input::K_UP: tv_.scroll(-1); break;
            case input::K_DOWN: tv_.scroll(1); break;
            case input::K_CHAR:
                if (e.ch == ' ') tv_.page(1);
                else if (e.base == 'r') { if (view_ == PAGE) open(); else buildSheet(sel_[T_PARTY]); }   // ask again
                break;
            case input::K_LEFT: case input::K_RIGHT:
                if (view_ == SHEET && actors_.size() > 1) {   // previous / next character
                    int n = actors_.size();
                    sel_[T_PARTY] = (sel_[T_PARTY] + (e.key == input::K_RIGHT ? 1 : n - 1)) % n;
                    buildSheet(sel_[T_PARTY]);
                    sfx::click();
                }
                break;
            default: break;
        }
        return true;
    }

    void buildSheet(int i) {
        sheetId_ = actors_[i].id;
        foundry::requestSheet(sheetId_);
        shownSheet_ = "";
        loading_ = true;
        asked_ = millis();
        tv_.clear();
    }

    // The sheet Foundry sent, if it's the one we're showing (updates keep the scroll position).
    void showSheet() {
        const String& t = foundry::text(foundry::SHEET);
        Span all{0, (uint16_t)t.length()};
        String f[2];
        foundry::fields(headOf(t, all), f, 2);
        if (f[0] != sheetId_) return;
        int top = shownSheet_ == sheetId_ ? tv_.top() : 0;
        loading_ = false;
        tv_.clear();
        for (auto& a : actors_)
            if (a.id == sheetId_ && a.sub.length()) tv_.add(a.sub, TextView::DIM);
        tv_.add(textOf(t, bodyOf(t, all)));
        tv_.scroll(top);
        shownSheet_ = sheetId_;
    }

    void buildChat() {
        bool end = tv_.atEnd();
        tv_.clear();
        for (int i = 0; i < (int)msgs_.size(); i++) {
            Msg m = msgAt(i);
            String who = m.speaker;
            if (m.time.length()) who += "  " + m.time;
            if (has(m.flags, 'w')) who += "  (whisper)";
            if (has(m.flags, 'b')) who += "  (blind)";
            tv_.add(who, TextView::HEAD);
            if (m.formula.length()) {
                String r = (m.flavor.length() ? m.flavor + ": " : String("")) + m.formula + " = " + m.total;
                tv_.add(r, has(m.flags, 'c') ? TextView::ACCENT : has(m.flags, 'f') ? TextView::BAD : TextView::BODY);
            } else {
                if (m.flavor.length()) tv_.add(m.flavor, TextView::DIM);
                if (m.text.length()) tv_.add(m.text);
            }
        }
        if (end || msgs_.empty()) toEnd_ = true;
    }

    void showPage() {
        const String& t = foundry::text(foundry::PAGE);
        Span all{0, (uint16_t)t.length()};
        String f[3];
        foundry::fields(headOf(t, all), f, 3);
        if (f[0] != wantEntry_ || f[1] != wantPage_) return;   // an older answer
        loading_ = false;
        if (f[2].length()) pageTitle_ = f[2];
        tv_.clear();
        String body = textOf(t, bodyOf(t, all));
        tv_.add(body.length() ? body : String("(This page is empty.)"));
    }

    void buildPageMissing() {
        tv_.clear();
        tv_.add("No answer from Foundry. Is the GM's Foundry tab open? R asks again.", TextView::DIM);
    }

    // ------------------------------------------------------------ drawing
    void drawTitle(const String& title, const String& right) {
        bool live = foundry::connected();
        String r = right.length() ? right : String(live ? "live" : "offline");
        if (wide()) {
            // the shared header brings the touch back button and the battery
            gfx.setFont(metrics().small);
            const int rw = gfx.textWidth(r) + u(44);
            drawHeader(title.c_str(), ACCENT, rw);
            gfx.setFont(metrics().small);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(live ? C_EMERALD : C_BLOOD);
            gfx.drawString(r, W - u(40), HEADER_H / 2);
            gfx.setTextDatum(top_left);
            gfx.setFont(ui::font(&fonts::Font0));
            return;
        }
        gfx.fillRect(0, 0, W, HEADER_H, C_BG2);
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(ACCENT);
        gfx.drawString(fitText(title, W - u(80)), u(5), HEADER_H / 2);
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(live ? C_EMERALD : C_BLOOD);
        gfx.drawString(r, W - u(4), HEADER_H / 2);
        gfx.setTextDatum(top_left);
    }

    void drawTabs() {
        drawTitle("Foundry", "");
        int ty = HEADER_H;
        int tw = W / NTABS, th = wide() ? 60 : u(11);   // touch: tabs big enough for a finger
        for (int i = 0; i < NTABS; i++) {
            bool on = i == tab_;
            if (on) gfx.fillRect(i * tw, ty, tw, th, C_PANEL);
            touch::zone(i * tw, ty, tw, th, [this, i] { if (tab_ != i) { tab_ = i; enterTab(); } });
            gfx.setTextDatum(top_center);
            gfx.setTextColor(on ? ACCENT : C_DIM);
            gfx.drawString(TABS[i], i * tw + tw / 2, ty + (th - u(11)) / 2 + u(2));
        }
        gfx.drawFastHLine(0, ty + th, W, C_EDGE);
        gfx.setTextDatum(top_left);
        const int y0 = ty + th + u(2), bottom = H - FOOTER_H;
        switch (tab_) {
            case T_PARTY: drawParty(y0, bottom); drawFooter("Ent sheet  </> tab  R resend  K key"); break;
            case T_COMBAT: drawCombat(y0, bottom); drawFooter("Up/Dn select  </> tab  K key  Esc"); break;
            case T_ROLLS: drawRolls(y0, bottom); drawFooter("Up/Dn scroll  </> tab  K key  Esc"); break;
            case T_CHAT:
                if (msgs_.empty()) empty(y0, "No chat messages yet.");
                else tv_.draw(u(6), y0, bottom - y0 - u(1), ACCENT);
                if (toEnd_) { tv_.toEnd(); toEnd_ = false; }   // needs the height from draw(); shows next frame
                drawFooter("Up/Dn scroll  </> tab  K key  Esc");
                break;
            case T_JOURNAL: drawJournal(y0, bottom); drawFooter("type filter  Ent read  </> tab"); break;
        }
    }

    void empty(int y, const char* msg) {
        gfx.setTextColor(C_DIM);
        gfx.setTextDatum(top_center);
        gfx.drawString(msg, W / 2, y + u(30));
        gfx.setTextDatum(top_left);
    }

    // Keeps the selection on screen; returns the first visible row.
    int scrollTo(int sel, int n, int rows, int& top) {
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        top = std::max(0, std::min(top, std::max(0, n - rows)));
        return top;
    }

    void scrollbar(int y0, int rowH, int rows, int n, int top) {
        if (n <= rows) return;
        int sh = std::max(u(5), rows * rowH * rows / n);
        int sy = y0 + (rows * rowH - sh) * top / std::max(1, n - rows);
        gfx.fillRect(W - u(2), sy, u(2), sh, ACCENT);
    }

    void drawParty(int y0, int bottom) {
        if (actors_.empty()) { empty(y0, "No player characters sent yet."); return; }
        const int rowH = touchRow(16), pad = touchPad(16), rows = (bottom - y0) / rowH, n = actors_.size();
        int top = scrollTo(sel_[T_PARTY], n, rows, top_[T_PARTY]);
        for (int i = top; i < n && i < top + rows; i++) {
            const Actor& a = actors_[i];
            int y = y0 + (i - top) * rowH + pad;
            if (i == sel_[T_PARTY]) gfx.fillRoundRect(u(2), y - pad - u(1), W - u(4), rowH - u(1), u(2), C_PANEL);
            touch::row(0, y - pad - u(1), W, rowH, sel_[T_PARTY], i);
            String right = String(a.hp) + "/" + a.max + (a.temp ? "+" + String(a.temp) : String("")) + "  AC " + a.ac;
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(right, W - u(6), y + u(1));
            int rw = gfx.textWidth(right);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_TEXT);
            gfx.drawString(fitText(a.name, W - u(18) - rw), u(6), y + u(1));
            float f = a.max ? (float)a.hp / a.max : 0;
            drawBar(u(6), y + u(10), W - u(14), u(2), f, a.hp * 2 < a.max ? C_BLOOD : C_EMERALD, C_BG);
        }
        scrollbar(y0, rowH, rows, n, top);
    }

    void drawCombat(int y0, int bottom) {
        if (fighters_.empty()) { empty(y0, "No combat running in Foundry."); return; }
        const int rowH = touchRow(11), pad = touchPad(11), rows = (bottom - y0 - u(11)) / rowH, n = fighters_.size();
        int top = scrollTo(sel_[T_COMBAT], n, rows, top_[T_COMBAT]);
        for (int i = top; i < n && i < top + rows; i++) {
            const Combatant& c = fighters_[i];
            int y = y0 + (i - top) * rowH + pad;
            bool cur = has(c.flags, 'c'), hidden = has(c.flags, 'h'), dead = has(c.flags, 'd');
            if (i == sel_[T_COMBAT]) gfx.fillRoundRect(u(2), y - pad - u(1), W - u(4), rowH, u(2), C_PANEL);
            touch::zone(0, y - pad - u(1), W, rowH, [this, i] { sel_[T_COMBAT] = i; });
            if (cur) gfx.drawRoundRect(u(2), y - pad - u(1), W - u(4), rowH, u(2), C_GOLD);
            gfx.setTextColor(cur ? C_GOLD : C_DIM);
            gfx.setTextDatum(top_right);
            gfx.drawString(c.init.length() ? c.init : String("-"), u(22), y + u(1));
            // HP: bar and numbers (blank for things without HP)
            String right = c.max ? String(c.hp) + "/" + c.max : String("");
            if (c.ac) right += "  AC" + String(c.ac);
            gfx.setTextColor(C_DIM);
            gfx.drawString(right, W - u(6), y + u(1));
            int rw = gfx.textWidth(right);
            if (c.max) drawBar(W - u(10) - rw - u(34), y + u(4), u(30), u(3), (float)c.hp / c.max, c.hp * 2 < c.max ? C_BLOOD : C_EMERALD, C_BG);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(dead ? C_BLOOD : hidden ? C_DIM : has(c.flags, 'p') ? C_ICE : C_TEXT);
            String nm = (hidden ? "~" : "") + c.name + (dead ? " x" : "");
            gfx.drawString(fitText(nm, W - u(66) - rw), u(28), y + u(1));
        }
        scrollbar(y0, rowH, rows, n, top);
        // the selected combatant's conditions, and the round
        const Combatant& s = fighters_[std::min(sel_[T_COMBAT], n - 1)];
        int y = bottom - u(11);
        gfx.drawFastHLine(0, y - u(1), W, C_EDGE);
        gfx.setTextColor(C_GOLD);
        String rnd = "Round " + String(round_);
        gfx.drawString(rnd, u(6), y + u(1));
        gfx.setTextColor(s.conds.length() ? C_ARCANE : C_DIM);
        gfx.drawString(fitText(s.conds.length() ? s.conds : String("no conditions"), W - u(20) - gfx.textWidth(rnd)),
                       u(14) + gfx.textWidth(rnd), y + u(1));
    }

    void drawRolls(int y0, int bottom) {
        if (rolls_.empty()) { empty(y0, "No rolls yet."); return; }
        const int rowH = u(22), rows = (bottom - y0) / rowH, n = rolls_.size();
        int top = scrollTo(sel_[T_ROLLS], n, rows, top_[T_ROLLS]);
        for (int i = top; i < n && i < top + rows; i++) {
            Msg m = msgAt(rolls_[i]);
            int y = y0 + (i - top) * rowH;
            bool crit = has(m.flags, 'c'), fum = has(m.flags, 'f');
            uint16_t edge = crit ? C_GOLD : fum ? C_BLOOD : i == sel_[T_ROLLS] ? C_EDGE : C_BG2;
            gfx.fillRoundRect(u(2), y, W - u(4), rowH - u(2), u(3), i == sel_[T_ROLLS] ? C_PANEL : C_BG2);
            gfx.drawRoundRect(u(2), y, W - u(4), rowH - u(2), u(3), edge);
            touch::zone(0, y, W, rowH, [this, i] { sel_[T_ROLLS] = i; });
            gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(crit ? C_GOLD : fum ? C_BLOOD : C_TEXT);
            gfx.drawString(m.total, W - u(8), y + rowH / 2);
            int tw = gfx.textWidth(m.total);
            gfx.setFont(ui::font(&fonts::Font0));
            gfx.setTextDatum(top_left);
            String l1 = m.speaker + (m.flavor.length() ? " - " + m.flavor : String(""));
            gfx.setTextColor(C_TEXT);
            gfx.drawString(fitText(l1, W - u(20) - tw), u(7), y + u(2));
            gfx.setTextColor(C_DIM);
            gfx.drawString(fitText(m.formula + "  " + m.dice, W - u(20) - tw), u(7), y + u(11));
        }
        scrollbar(y0, rowH, rows, n, top);
    }

    void drawJournal(int y0, int bottom) {
        const auto& vis = vis_;
        int y = y0;
        if (filter_.length()) {
            gfx.setTextColor(C_GOLD);
            gfx.drawString("\"" + filter_ + "\"", u(6), y);
            y += u(11);
        }
        if (vis.empty()) { empty(y, pages_.empty() ? "No journal pages sent yet." : "No matches."); return; }
        const int rowH = touchRow(11), pad = touchPad(11), rows = (bottom - y) / rowH, n = vis.size();
        int top = scrollTo(sel_[T_JOURNAL], n, rows, top_[T_JOURNAL]);
        for (int i = top; i < n && i < top + rows; i++) {
            String p[5];   // entry id, page id, folder, entry, page
            pageAt(vis[i], p);
            int ry = y + (i - top) * rowH + pad;
            bool s = i == sel_[T_JOURNAL];
            touch::row(0, ry - pad - u(1), W, rowH, sel_[T_JOURNAL], i);
            if (s) gfx.fillRoundRect(u(2), ry - pad - u(1), W - u(4), rowH, u(2), C_PANEL);
            String right = fitText(p[2], u(70));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(right, W - u(6), ry + u(1));
            int rw = gfx.textWidth(right);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(s ? C_TEXT : rgb(210, 200, 190));
            String nm = p[4].length() && p[4] != p[3] ? p[3] + ": " + p[4] : p[3];
            gfx.drawString(fitText(nm, W - u(18) - rw), u(6), ry + u(1));
        }
        scrollbar(y, rowH, rows, n, top);
    }

    void drawSheet() {
        const Actor* a = nullptr;
        for (auto& x : actors_) if (x.id == sheetId_) a = &x;
        if (!a) { view_ = LIST; return; }
        String right = String(a->hp) + "/" + a->max + "  AC" + a->ac;
        drawTitle(a->name, right);
        float f = a->max ? (float)a->hp / a->max : 0;
        drawBar(0, HEADER_H, W, u(2), f, a->hp * 2 < a->max ? C_BLOOD : C_EMERALD, C_BG);
        if (loading_) empty(HEADER_H + u(10), "Asking Foundry...");
        else tv_.draw(u(6), HEADER_H + u(5), H - FOOTER_H - HEADER_H - u(6), ACCENT);
        drawFooter(actors_.size() > 1 ? "Up/Dn scroll  </> next PC  Esc" : "Up/Dn scroll  Spc page  Esc back");
    }

    void drawPage() {
        drawTitle(pageTitle_, loading_ ? "..." : " ");
        if (loading_) empty(HEADER_H + u(10), "Asking Foundry...");
        else tv_.draw(u(6), HEADER_H + u(4), H - FOOTER_H - HEADER_H - u(5), ACCENT);
        drawFooter("Up/Dn scroll  Spc page  R reload  Esc");
    }

    void drawSetup() {
        if (wide()) { drawSetupWide(); return; }
        drawHeader("Foundry VTT", ACCENT);
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        const int x = u(8);
        int y = HEADER_H + u(6);
        gfx.setTextColor(C_TEXT);
        if (web::state() == web::OFF) {
            y = textWrapped("Start web access first (System > Web). The Cardputer must join the same home "
                            "network as your Foundry server: set it with W in the Web app.", x, y, W - 2 * x, u(10));
        } else if (web::state() == web::CONNECTING) {
            gfx.drawString("Joining " + web::networkName() + "...", x, y);
        } else if (web::apMode()) {
            y = textWrapped("Web access is running its own hotspot. Foundry needs the Cardputer on your home "
                            "network: press W in the Web app to set it, then restart web access.", x, y, W - 2 * x, u(10));
        } else {
            gfx.setTextColor(C_DIM);
            gfx.drawString("Link key for the Foundry module:", x, y);
            drawBigKey(y + u(12));
            y += u(40);
            gfx.setFont(ui::font(&fonts::Font0));
            gfx.setTextDatum(top_left);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Also on the web page: " + web::address(), x, y);
            String dots = String("...").substring(0, (millis() / 400) % 4);
            gfx.drawString("Waiting for Foundry" + dots, x, y + u(12));
        }
        drawFooter("FnN new key  Esc back");
    }


    // Big screen: the setup as three steps, each ticked off when done and
    // with a button for doing it.
    void drawSetupWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawHeader("Foundry VTT", ACCENT);
        const int X = 16, SW = W - 32, top = HEADER_H + 10, bottom = H - FOOTER_H - 10;
        const web::State st = web::state();
        const bool on = st == web::RUNNING, home = on && !web::apMode();
        const String dots = String("...").substring(0, (millis() / 400) % 4);
        int y = top;
        gfx.setFont(m.small);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString("Shows your Foundry party, combat, rolls, chat and journal here. Three steps:", X + 4, y);
        y += 36;
        auto step = [&](int n, bool done, bool current, const char* title, const String& sub, int h) {
            drawPanel(X, y, SW, h, current ? C_PANEL : C_BG2, current ? ACCENT : C_EDGE);
            const int cx = X + 40, cy = y + 42;
            if (done) {
                gfx.fillCircle(cx, cy, 22, C_EMERALD);
                gfx.drawWideLine(cx - 10, cy + 1, cx - 3, cy + 9, 4, C_BG);
                gfx.drawWideLine(cx - 3, cy + 9, cx + 11, cy - 8, 4, C_BG);
            } else {
                gfx.drawCircle(cx, cy, 22, current ? ACCENT : C_EDGE);
                gfx.drawCircle(cx, cy, 21, current ? ACCENT : C_EDGE);
                gfx.setFont(m.bold);
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(current ? ACCENT : C_DIM);
                gfx.drawString(String(n), cx, cy + 1);
            }
            gfx.setTextDatum(top_left);
            gfx.setFont(m.bold);
            gfx.setTextColor(done ? C_DIM : C_TEXT);
            gfx.drawString(title, X + 84, y + 14);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(fitText(sub, SW - 84 - 260), X + 84, y + 50);
        };
        const int bw = 230, bx = X + SW - bw - 16;

        step(1, on, st != web::RUNNING, "Turn on web access",
             st == web::CONNECTING ? "Joining " + web::networkName() + dots
                                   : on ? String("Running at ") + web::address() : String("Foundry talks to the toolkit through it"), 86);
        if (st == web::OFF) button(bx, y + 15, bw, 56, "Start", true, C_GOLD, [] { web::start(); sfx::select(); });
        y += 96;

        step(2, home, on && !home, "Join your home network",
             home ? "On " + web::networkName()
                  : on ? String("It made its own hotspot: save your home WiFi in Web access, then restart it")
                       : String("The ") + board::name() + " and your Foundry server must share a network", 86);
        if (!home) button(bx, y + 15, bw, 56, "Web access...", on, ACCENT, [] { requestApp("Web"); });
        y += 96;

        const int h3 = bottom - y;
        step(3, false, home, "Enter this key in Foundry", "In Foundry: Configure Settings > Cardputer Link > Link key", h3);
        // the key, big, grouped in fours
        const int ky = y + 92, kh = 96;
        gfx.fillRoundRect(X + 84, ky, SW - 84 - 16, kh, 12, C_BG);
        gfx.drawRoundRect(X + 84, ky, SW - 84 - 16, kh, 12, C_GOLD_DK);
        gfx.setFont(ui::font(&fonts::FreeMonoBold12pt7b));
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(foundry::keyGrouped(), X + 84 + (SW - 100) / 2, ky + kh / 2 + 2);
        button(X + 84, y + h3 - 72, bw, 56, "New key", false, C_BLOOD, [] {
            holdmenu::open("A new link key?", {{"Make one (the old key stops working)", [] {
                foundry::newKey();
                toast("New key - enter it in Foundry");
            }, C_BLOOD, true}});
        });
        gfx.setFont(m.body);
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(home ? ACCENT : C_DIM);
        gfx.drawString(home ? "Waiting for Foundry" + dots : String("Waiting for steps 1 and 2"), X + SW - 24, y + h3 - 44);
        drawFooter("FnN new key  Esc back");
    }

    // The key in a large font, grouped in fours: easy to copy by eye.
    void drawBigKey(int y) {
        gfx.fillRoundRect(u(8), y, W - u(16), u(24), u(4), C_BG2);
        gfx.drawRoundRect(u(8), y, W - u(16), u(24), u(4), C_GOLD_DK);
        gfx.setFont(ui::font(&fonts::FreeMonoBold12pt7b));
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(foundry::keyGrouped(), W / 2, y + u(13));
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
    }

    void drawKey() {
        drawPanel(u(6), u(24), W - u(12), u(80));
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_DIM);
        gfx.drawString("Link key (also on the web page)", u(14), u(30));
        drawBigKey(u(42));
        gfx.setTextColor(C_DIM);
        gfx.drawString("Device " + web::address(), u(14), u(72));
        gfx.drawString("FnN new key  any key closes", u(14), u(88));
    }

    int tab_ = 0, sel_[NTABS] = {0}, top_[NTABS] = {0}, round_ = 0;
    View view_ = LIST;
    uint32_t seen_[foundry::NSECTIONS];
    bool firstChat_ = true, showKey_ = false, loading_ = false, toEnd_ = false;
    uint32_t asked_ = 0;
    String filter_, sheetId_, shownSheet_, wantEntry_, wantPage_, pageTitle_;
    std::vector<Actor> actors_;
    std::vector<Combatant> fighters_;
    std::vector<Span> msgs_;    // chat records, oldest first
    std::vector<uint16_t> rolls_;   // indexes into msgs_, newest first
    std::vector<Span> pages_;   // journal list lines
    std::vector<uint16_t> vis_;     // pages_ matching the filter
    TextView tv_;
};

}  // namespace

App* makeFoundryApp() { return new FoundryApp(); }

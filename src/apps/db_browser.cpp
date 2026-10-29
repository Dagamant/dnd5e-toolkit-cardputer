#include "../core/osk.h"
#include "db_browser.h"
#include "../core/touch.h"

using namespace ui;

void DbBrowser::onEnter() {
    input::setTextMode(false);
    if (!table_.isOpen()) openTable();
    mode_ = Mode::List;
    refilter();
    if (!storage::ok()) toast("No SD card", C_BLOOD);
    else if (!table_.size()) toast("No data: copy /gmtools/srd to the SD card", C_BLOOD);
}

bool DbBrowser::openByName(const String& name) {
    if (!table_.isOpen()) openTable();
    int i = table_.find(name);
    if (i < 0) return false;
    search_ = "";
    vis_.assign(1, i);
    sel_ = 0;
    openDetail(0);
    return mode_ == Mode::Detail;
}

void DbBrowser::onExit() {
    // free the index; it's reloaded next time
    table_.close();
    vis_.clear();
    vis_.shrink_to_fit();
    input::setTextMode(false);
}

void DbBrowser::refilter() {
    vis_.clear();
    listPos_ = 0;
    shownSel_ = -1;
    for (int i = 0; i < table_.size(); i++) {
        if (search_.length()) {
            String n = table_.at(i).name;
            n.toLowerCase();
            String s = search_;
            s.toLowerCase();
            if (n.indexOf(s) < 0) continue;
        }
        if (passFilter(i)) vis_.push_back(i);
    }
    if (sel_ >= (int)vis_.size()) sel_ = std::max(0, (int)vis_.size() - 1);
}

void DbBrowser::openDetail(int v) {
    if (v < 0 || v >= (int)vis_.size()) return;
    curIdx_ = vis_[v];
    if (!table_.read(curIdx_, cur_)) { sfx::error(); toast("Read failed", C_BLOOD); return; }
    tv_.clear();
    tv_.setWidth(wide() ? W - listW() - u(16) : 0);
    buildDetail();
    shownIdx_ = curIdx_;
    mode_ = Mode::Detail;
    confirmDelete_ = false;
    sfx::select();
}

void DbBrowser::startWizard(int editIndex, const db::Rec* from) {
    editIdx_ = editIndex;
    wiz_.begin(editIndex >= 0 ? String("Edit: ") + table_.at(editIndex).name : String(newLabel()), wizardSteps(), accent());
    if (from) presetWizard(*from);
    mode_ = Mode::Wizard;
}

void DbBrowser::finishWizard() {
    db::Rec r = fromWizard();
    int existing = table_.find(r.name());
    if (existing >= 0 && existing != editIdx_ && table_.isCustom(existing)) {
        toast("Name already used - saved anyway", C_GOLD);
    }
    int idx = editIdx_ >= 0 ? table_.update(editIdx_, r) : table_.add(r);
    if (idx < 0) { sfx::error(); toast("Save failed (SD card?)", C_BLOOD); mode_ = Mode::List; return; }
    toast((String("Saved ") + r.name()).c_str(), C_EMERALD);
    search_ = "";
    refilter();
    for (int v = 0; v < (int)vis_.size(); v++) if (vis_[v] == idx) { sel_ = v; openDetail(v); return; }
    mode_ = Mode::List;
}

bool DbBrowser::handle(const input::Event& e) {
    if (mode_ == Mode::Wizard) {
        wiz_.handle(e);
        if (wiz_.finished()) finishWizard();
        else if (wiz_.cancelled()) { mode_ = curIdx_ >= 0 && editIdx_ >= 0 ? Mode::Detail : Mode::List; input::setTextMode(false); }
        return true;
    }
    if (mode_ == Mode::Detail) {
        if (e.key != input::K_CHAR || !(e.fn && e.base == 'x')) confirmDelete_ = false;
        if (detailKey(e)) return true;
        switch (e.key) {
            case input::K_BACK:
                if (closeOnBack) return false;
                mode_ = Mode::List; refilter(); sfx::back(); return true;
            case input::K_UP: tv_.scroll(-1); return true;
            case input::K_DOWN: tv_.scroll(1); return true;
            case input::K_ENTER:
                if (onPick) { onPick(cur_); sfx::select(); }
                else rollCurrent();
                return true;
            case input::K_CHAR:
                if (e.base == ' ') { tv_.page(1); return true; }
                if (!canCreate() && e.fn) return true;
                if (e.fn && e.base == 'e') {
                    if (table_.isCustom(curIdx_)) startWizard(curIdx_, &cur_);
                    else toast("SRD entry: Fn+D makes an editable copy", C_GOLD);
                    return true;
                }
                if (e.fn && e.base == 'd') {
                    db::Rec copy = cur_;
                    copy.set("name", cur_.name() + " (custom)");
                    startWizard(-1, &copy);
                    return true;
                }
                if (e.fn && e.base == 'x' && table_.isCustom(curIdx_)) {
                    if (!confirmDelete_) { confirmDelete_ = true; toast("Fn+X again to delete", C_BLOOD); return true; }
                    table_.remove(curIdx_);
                    curIdx_ = -1;
                    refilter();
                    mode_ = Mode::List;
                    toast("Deleted", C_DIM);
                    return true;
                }
                return true;
            default: return true;
        }
    }
    // list
    int n = vis_.size();
    switch (e.key) {
        case input::K_BACK:
            if (search_.length()) { search_ = ""; refilter(); return true; }
            return false;
        case input::K_UP: if (n) { sel_ = (sel_ + n - 1) % n; sfx::click(); } return true;
        case input::K_DOWN: if (n) { sel_ = (sel_ + 1) % n; sfx::click(); } return true;
        case input::K_LEFT: sel_ = std::max(0, sel_ - 8); return true;
        case input::K_RIGHT: sel_ = std::max(0, std::min(n - 1, sel_ + 8)); return true;
        case input::K_TAB: cycleFilter(e.shift ? -1 : 1); refilter(); sel_ = 0; sfx::click(); return true;
        case input::K_ENTER: openDetail(sel_); return true;
        case input::K_DEL:
            if (search_.length()) { search_.remove(search_.length() - 1); refilter(); }
            return true;
        case input::K_CHAR:
            if (e.fn && e.base == 'n' && canCreate()) { startWizard(-1, nullptr); return true; }
            if (e.fn) return true;
            if (isdigit((unsigned char)e.ch) && quickFilter(e.ch)) { refilter(); sel_ = 0; sfx::click(); return true; }
            if ((isalnum((unsigned char)e.ch) || e.ch == ' ' || e.ch == '\'' || e.ch == '-') && search_.length() < 20) {
                search_ += e.ch;
                refilter();
                sel_ = 0;
            }
            return true;
        default: return true;
    }
}

void DbBrowser::draw() {
    if (mode_ == Mode::Wizard) { wiz_.draw(); return; }
    if (wide()) { drawWide(); return; }
    tv_.setWidth(0);
    drawBackground();
    if (mode_ == Mode::Detail) {
        drawHeader(cur_.name().c_str(), accent(), table_.isCustom(curIdx_) ? 40 : 0);
        if (table_.isCustom(curIdx_)) {
            gfx.setFont(ui::font(&fonts::Font0));
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_EMERALD);
            gfx.drawString("custom", W - 30, HEADER_H / 2);
        }
        drawDetailBar(0, HEADER_H + 2, W);
        tv_.draw(6, HEADER_H + 15, H - FOOTER_H - HEADER_H - 17, accent());
        String foot = detailFooter() ? String(detailFooter()) : onPick ? String("Ent ") + pickVerb + "  Esc back" :
                      table_.isCustom(curIdx_) ? "Ent roll  FnE edit  FnX del  Esc" : "Ent roll  FnD copy+edit  Esc";
        drawFooter(foot.c_str());
        return;
    }
    drawHeader(title(), accent());
    // filter chip + count
    gfx.setFont(ui::font(&fonts::Font0));
    gfx.setTextDatum(middle_right);
    gfx.setTextColor(C_DIM);
    gfx.drawString(String(vis_.size()), W - 30, HEADER_H / 2);
    // search / filter bar
    int by = HEADER_H + 2;
    gfx.fillRect(0, by, W, 12, C_BG2);
    gfx.setTextDatum(middle_left);
    gfx.setTextColor(C_GOLD);
    gfx.drawString(filterLabel(), 4, by + 6);
    gfx.setTextDatum(middle_right);
    gfx.setTextColor(search_.length() ? C_TEXT : C_DIM);
    gfx.drawString(search_.length() ? String("\"") + search_ + "\"" : String("type=search"), W - 4, by + 6);

    const int rowH = 11, y0 = by + 14;
    int rows = (H - FOOTER_H - y0) / rowH;
    int n = vis_.size();
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    for (int v = top_; v < n && v < top_ + rows; v++) {
        int y = y0 + (v - top_) * rowH;
        bool s = v == sel_;
        if (s) gfx.fillRoundRect(2, y - 1, W - 4, rowH, 2, C_PANEL);
        drawRow(vis_[v], y, s);
    }
    if (!n) {
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(table_.size() ? "No matches" : "No data on SD card", W / 2, 70);
    }
    drawFooter(listFooter() ? listFooter() : onPick ? "Ent view  Tab filter  Esc back" :
               canCreate() ? "Ent open  Tab filter  FnN new  Esc back" : "Ent open  Tab filter  Esc back");
}

// ---- big screen: list on the left, the highlighted entry on the right.
// Enter moves the focus to the entry (Up/Down scroll it, Esc goes back).

void DbBrowser::preview() {
    if (vis_.empty() || vis_[sel_] == shownIdx_) return;
    curIdx_ = vis_[sel_];
    if (!table_.read(curIdx_, cur_)) return;
    tv_.clear();
    tv_.setWidth(W - listW() - u(16));
    buildDetail();
    shownIdx_ = curIdx_;
}

void DbBrowser::drawWide() {
    const Metrics& m = metrics();
    bool detail = mode_ == Mode::Detail;
    if (!detail) preview();
    drawBackground();
    drawHeader(title(), accent());
    gfx.setFont(m.body);
    gfx.setTextDatum(middle_right);
    gfx.setTextColor(C_DIM);
    gfx.drawString(String(vis_.size()) + " entries", W - u(34), HEADER_H / 2);

    // left: search / filter bar and the list
    const int lw = listW(), by = HEADER_H + u(2), barH = touchRow(12);
    // the filter is a button: a tap moves to the next one
    gfx.fillRoundRect(u(2), by, lw / 2, barH - 4, 8, C_PANEL);
    gfx.drawRoundRect(u(2), by, lw / 2, barH - 4, 8, C_EDGE);
    gfx.setTextDatum(middle_left);
    gfx.setTextColor(C_GOLD);
    gfx.drawString(fitText(filterLabel(), lw / 2 - u(8)), u(6), by + barH / 2 - 2);
    gfx.setTextDatum(middle_right);
    gfx.setTextColor(search_.length() ? C_TEXT : C_DIM);
    gfx.drawString(search_.length() ? String("\"") + search_ + "\"" : String("type to search"), lw - u(4), by + barH / 2 - 2);
    if (!detail) {
        touch::key(0, by, lw / 2 + u(4), barH, input::K_TAB);
        osk::offer();   // typing searches
    }
    const int rowH = touchRow(11), pad = touchPad(11), y0 = by + barH + u(1), listH = H - FOOTER_H - y0;
    const int n = vis_.size();
    const float maxPos = std::max(0, n * rowH - listH);
    // the selection moved (keys, a tap, a search): bring it into view
    if (sel_ != shownSel_) {
        listPos_ = std::min(listPos_, (float)sel_ * rowH);
        listPos_ = std::max(listPos_, (float)(sel_ + 1) * rowH - listH);
        shownSel_ = sel_;
    }
    if (!touch::scrolling()) listPos_ = std::max(0.0f, std::min(listPos_, maxPos));
    touch::scroller(0, y0, lw, listH, &listPos_, maxPos);
    gfx.setClipRect(0, y0, lw, listH);
    const int p = (int)listPos_;
    for (int v = std::max(0, p / rowH); v < n; v++) {
        int y = y0 + v * rowH - p;
        if (y >= y0 + listH) break;
        bool s = v == sel_;
        if (s) {
            gfx.fillRoundRect(u(2), y, lw - u(4), rowH - 2, u(2), detail ? C_BG2 : C_PANEL);
            gfx.drawRoundRect(u(2), y, lw - u(4), rowH - 2, u(2), detail ? C_EDGE : accent());
        }
        drawRow(vis_[v], y + pad + u(1), s);
        // touch: a tap shows the entry on the right; tapping it again opens it
        // (only the part of the row that shows)
        const int zy = std::max(y, y0), zh = std::min(y + rowH, y0 + listH) - zy;
        touch::zone(0, zy, lw, zh, [this, v] {
            if (mode_ == Mode::Detail) mode_ = Mode::List;
            else if (sel_ == v) { input::Event e; e.key = input::K_ENTER; input::inject(e); }
            sel_ = v;
        });
    }
    gfx.clearClipRect();
    if (maxPos > 0) {
        int sh = std::max(u(6), listH * listH / (n * rowH));
        float f = std::max(0.0f, std::min(1.0f, listPos_ / maxPos));
        gfx.fillRect(lw - u(2), y0 + (int)((listH - sh) * f), u(1), sh, accent());
    }
    if (!n) {
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(table_.size() ? "No matches" : "No data on SD card", lw / 2, u(70));
    }

    // right: the entry
    const int px = lw + u(4), pw = W - px - u(4), py = by, ph = H - FOOTER_H - py - u(3);
    drawPanel(px, py, pw, ph, C_BG2, detail ? accent() : C_EDGE);
    if (!detail && n) touch::key(px, py, pw, ph, input::K_ENTER);   // tap the entry to read or roll it
    if (n && shownIdx_ >= 0) {
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(accent());
        gfx.drawString(fitText(cur_.name(), pw - u(50)), px + u(6), py + u(4));
        if (table_.isCustom(shownIdx_)) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_EMERALD);
            gfx.drawString("custom", px + pw - u(6), py + u(6));
        }
        int ty = py + u(18);
        // the subclass's bar (spell slot, ...) is drawn across the pane
        gfx.setClipRect(px, ty, pw, u(12));
        gfx.setTextDatum(top_left);
        drawDetailBar(px, ty - u(1), pw);
        gfx.clearClipRect();
        tv_.draw(px + u(6), ty + u(12), py + ph - ty - u(14), accent());
    }
    String foot;
    if (detail) foot = detailFooter() ? String(detailFooter()) : onPick ? String("Ent ") + pickVerb + "  Esc list" :
                       table_.isCustom(curIdx_) ? "Up/Dn scroll  Ent roll  FnE edit  FnX delete  Esc list" :
                                                  "Up/Dn scroll  Ent roll  FnD copy+edit  Esc list";
    else foot = listFooter() ? listFooter() : onPick ? "Ent open  Tab filter  type to search  Esc back" :
                canCreate() ? "Ent open  Tab filter  type to search  FnN new  Esc back" : "Ent open  Tab filter  type to search  Esc back";
    drawFooter(foot.c_str());
}

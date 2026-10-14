#include "db_browser.h"

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
    buildDetail();
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
    drawBackground();
    if (mode_ == Mode::Detail) {
        drawHeader(cur_.name().c_str(), accent(), table_.isCustom(curIdx_) ? 40 : 0);
        if (table_.isCustom(curIdx_)) {
            gfx.setFont(&fonts::Font0);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(C_EMERALD);
            gfx.drawString("custom", W - 30, HEADER_H / 2);
        }
        drawDetailBar(HEADER_H + 2);
        tv_.draw(6, HEADER_H + 15, H - FOOTER_H - HEADER_H - 17, accent());
        String foot = detailFooter() ? String(detailFooter()) : onPick ? String("Ent ") + pickVerb + "  Esc back" :
                      table_.isCustom(curIdx_) ? "Ent roll  FnE edit  FnX del  Esc" : "Ent roll  FnD copy+edit  Esc";
        drawFooter(foot.c_str());
        return;
    }
    drawHeader(title(), accent());
    // filter chip + count
    gfx.setFont(&fonts::Font0);
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

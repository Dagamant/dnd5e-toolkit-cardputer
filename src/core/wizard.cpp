#include "osk.h"
#include "wizard.h"
#include "dice_expr.h"
#include "sfx.h"
#include "ui.h"
#include "touch.h"

using namespace ui;

static const char* OTHER = "Other...";

void Wizard::begin(const String& title, const std::vector<Step>& steps, uint16_t accent) {
    title_ = title;
    steps_ = steps;
    accent_ = accent;
    vals_.clear();
    active_ = true;
    finished_ = cancelled_ = review_ = false;
    idx_ = 0;
    while (idx_ < (int)steps_.size() && !visible(idx_)) idx_++;
    enter(idx_);
}

void Wizard::preset(const char* key, const String& value) { set(key, value); if (steps_.size()) enter(idx_); }

String Wizard::get(const char* key) const {
    for (auto& p : vals_) if (p.first == key) return p.second;
    return "";
}
void Wizard::set(const char* key, const String& v) {
    for (auto& p : vals_) if (p.first == key) { p.second = v; return; }
    vals_.push_back({key, v});
}
bool Wizard::contains(const char* key, const char* item) const {
    String v = ", " + get(key) + ",";
    return v.indexOf(String(", ") + item + ",") >= 0;
}

bool Wizard::visible(int i) const { return !steps_[i].when || steps_[i].when(*this); }

void Wizard::enter(int i) {
    if (i >= (int)steps_.size()) { review_ = true; reviewTop_ = 0; input::setTextMode(false); return; }
    review_ = false;
    idx_ = i;
    const Step& s = steps_[i];
    edit_ = get(s.key);
    otherMode_ = false;
    opts_ = s.dynOptions ? s.dynOptions(*this) : s.options;
    if (s.type == YESNO) opts_ = {"Yes", "No"};
    sel_ = 0;
    for (int k = 0; k < (int)opts_.size(); k++) if (opts_[k] == edit_) sel_ = k;
    if (s.type == CHOICE && edit_.length() && opts_.size() && opts_[sel_] != edit_) {
        // a custom value from "Other..."
        for (int k = 0; k < (int)opts_.size(); k++) if (opts_[k] == OTHER) sel_ = k;
    }
    checks_.assign(opts_.size(), false);
    if (s.type == MULTI)
        for (size_t k = 0; k < opts_.size(); k++) checks_[k] = contains(s.key, opts_[k].c_str());
    top_ = std::max(0, sel_ - 3);
    bool text = s.type == TEXT || s.type == LONGTEXT || s.type == NUMBER || s.type == DICE;
    input::setTextMode(text);
    osk::show(text);
}

void Wizard::next() {
    int i = idx_ + 1;
    while (i < (int)steps_.size() && !visible(i)) i++;
    sfx::click();
    enter(i);
}

void Wizard::back() {
    if (review_) {
        int i = (int)steps_.size() - 1;
        while (i >= 0 && !visible(i)) i--;
        if (i >= 0) enter(i);
        sfx::back();
        return;
    }
    int i = idx_ - 1;
    while (i >= 0 && !visible(i)) i--;
    if (i < 0) {
        active_ = false;
        cancelled_ = true;
        input::setTextMode(false);
        sfx::back();
        return;
    }
    sfx::back();
    enter(i);
}

bool Wizard::commit() {
    const Step& s = steps_[idx_];
    String v;
    switch (s.type) {
        case CHOICE:
        case YESNO:
            if (otherMode_) { v = edit_; v.trim(); if (!v.length()) return false; break; }
            if (opts_.empty()) return s.optional;
            v = opts_[sel_];
            if (v == OTHER) { otherMode_ = true; edit_ = ""; input::setTextMode(true); return false; }
            break;
        case MULTI: {
            int n = 0;
            for (size_t k = 0; k < opts_.size(); k++) if (checks_[k]) { v += (v.length() ? ", " : "") + opts_[k]; n++; }
            if (!n && !s.optional) { toast("Pick at least one (Space)", C_BLOOD); return false; }
            int need = s.dynPick ? s.dynPick(*this) : s.pick;
            if (need && n != need) { toast((String("Pick exactly ") + need).c_str(), C_BLOOD); return false; }
            break;
        }
        default:
            v = edit_;
            v.trim();
            if (!v.length() && !s.optional) { toast("Required", C_BLOOD); return false; }
            if (v.length() && s.type == DICE && !dice::roll(v).ok) { toast("Not a valid roll (e.g. 2d6+3)", C_BLOOD); return false; }
            if (v.length() && s.type == NUMBER) {
                for (size_t k = 0; k < v.length(); k++)
                    if (!isdigit((unsigned char)v[k]) && !(k == 0 && (v[k] == '-' || v[k] == '+'))) { toast("Numbers only", C_BLOOD); return false; }
            }
    }
    String old = get(s.key);
    set(s.key, v);
    if (s.validate) {
        String err = s.validate(*this);
        if (err.length()) { set(s.key, old); toast(err.c_str(), C_BLOOD); sfx::error(); return false; }
    }
    if (s.onCommit) s.onCommit(*this);
    return true;
}

bool Wizard::handle(const input::Event& e) {
    if (!active_) return false;
    if (review_) {
        int n = 0;
        for (size_t i = 0; i < steps_.size(); i++) if (visible(i)) n++;
        if (e.key == input::K_ENTER) { active_ = false; finished_ = true; sfx::select(); }
        else if (e.key == input::K_BACK) back();
        else if (e.key == input::K_UP) reviewTop_ = std::max(0, reviewTop_ - 1);
        else if (e.key == input::K_DOWN) reviewTop_ = std::min(std::max(0, n - 7), reviewTop_ + 1);
        return true;
    }
    const Step& s = steps_[idx_];
    bool textEntry = otherMode_ || s.type == TEXT || s.type == LONGTEXT || s.type == NUMBER || s.type == DICE;
    if (e.key == input::K_BACK) {
        if (otherMode_) { otherMode_ = false; input::setTextMode(false); return true; }
        back();
        return true;
    }
    if (e.key == input::K_ENTER) {
        if (commit()) next();
        return true;
    }
    if (textEntry) {
        int maxLen = s.type == LONGTEXT && !otherMode_ ? 900 : s.maxLen;
        if (e.key == input::K_DEL && edit_.length()) edit_.remove(edit_.length() - 1);
        else if (e.key == input::K_CHAR && (int)edit_.length() < maxLen) edit_ += e.ch;
        return true;
    }
    int n = opts_.size();
    if (!n) return true;
    if (e.key == input::K_UP) { sel_ = (sel_ + n - 1) % n; sfx::click(); }
    else if (e.key == input::K_DOWN) { sel_ = (sel_ + 1) % n; sfx::click(); }
    else if (e.key == input::K_LEFT) { sel_ = std::max(0, sel_ - 6); sfx::click(); }
    else if (e.key == input::K_RIGHT) { sel_ = std::min(n - 1, sel_ + 6); sfx::click(); }
    else if (e.key == input::K_CHAR && e.base == ' ' && s.type == MULTI) {
        int count = 0;
        for (bool c : checks_) count += c;
        int need = s.dynPick ? s.dynPick(*this) : s.pick;
        if (!checks_[sel_] && need && count >= need) { sfx::error(); toast((String("Only ") + need + " allowed").c_str(), C_BLOOD); }
        else { checks_[sel_] = !checks_[sel_]; sfx::click(); }
    } else if (e.key == input::K_CHAR) {
        // jump to the next option starting with this letter
        for (int k = 1; k <= n; k++) {
            int j = (sel_ + k) % n;
            if (toupper(opts_[j][0]) == toupper(e.ch)) { sel_ = j; sfx::click(); break; }
        }
    }
    if (sel_ < top_) top_ = sel_;
    return true;
}

void Wizard::drawChoice(const Step& s, int y) {
    const Metrics& m = metrics();
    int n = opts_.size();
    bool hasPreview = (bool)s.preview && n;
    int listW = hasPreview ? (wide() ? m.w * 2 / 5 : 104) : m.w - u(12);
    const int rh = m.rowH - (wide() ? 0 : 1);   // 11 on the classic layout
    int rows = (m.h - m.footerH - y - u(2)) / rh;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    if (sel_ < top_) top_ = sel_;
    gfx.setFont(m.body);
    for (int i = top_; i < n && i < top_ + rows; i++) {
        int ry = y + (i - top_) * rh;
        bool on = i == sel_;
        if (on) gfx.fillRoundRect(u(4), ry - u(1), listW, rh, u(2), C_PANEL);
        // touch: a tap ticks a box (Space), or picks an option; tapping it again goes on
        if (s.type == MULTI) touch::zone(0, ry - u(1), listW + u(8), rh, [this, i] {
            sel_ = i;
            input::Event e;
            e.key = input::K_CHAR;
            e.ch = e.base = ' ';
            input::inject(e);
        });
        else touch::pick(0, ry - u(1), listW + u(8), rh, sel_, i);
        int tx = u(9);
        if (s.type == MULTI) {
            gfx.drawRect(u(8), ry + u(1), u(7), u(7), on ? accent_ : C_DIM);
            if (checks_[i]) gfx.fillRect(u(10), ry + u(3), u(3), u(3), C_EMERALD);
            tx = u(19);
        }
        gfx.setTextDatum(top_left);
        gfx.setTextColor(on ? C_TEXT : rgb(190, 184, 200));
        String o = opts_[i];
        while (o.length() > 2 && gfx.textWidth(o) > listW - tx) o = o.substring(0, o.length() - 2);
        gfx.drawString(o, tx, ry + u(1));
    }
    if (n > rows) {
        int sh = std::max(u(5), rows * rh * rows / n);
        int sy = y + (rows * rh - sh) * top_ / std::max(1, n - rows);
        gfx.fillRect(listW + u(5), sy, u(2), sh, accent_);
    }
    if (hasPreview) {
        int px = listW + u(10);
        drawPanel(px, y - u(2), m.w - px - u(4), m.h - m.footerH - y, C_BG, C_EDGE);
        gfx.setTextColor(C_TEXT);
        String p = s.preview(opts_[sel_]);
        gfx.setClipRect(px + u(2), y, m.w - px - u(8), m.h - m.footerH - y - u(4));
        textWrapped(p.c_str(), px + u(4), y + u(2), m.w - px - u(12), u(9), wide() ? 40 : 12);
        gfx.clearClipRect();
    }
}

void Wizard::drawReview() {
    const Metrics& m = metrics();
    gfx.setFont(m.body);
    gfx.setTextDatum(top_left);
    gfx.setTextColor(accent_);
    gfx.drawString("Review - Enter to save, Esc to go back", u(6), m.headerH + u(6));
    int y = m.headerH + u(20), row = 0;
    for (size_t i = 0; i < steps_.size(); i++) {
        if (!visible(i)) continue;
        if (row++ < reviewTop_) continue;
        if (y > m.h - m.footerH - u(10)) break;
        String label = steps_[i].label;
        if (!wide() && label.length() > 14) label = label.substring(0, 13) + ".";
        String v = get(steps_[i].key);
        if (!v.length()) v = "-";
        v.replace("\n", " ");
        while (v.length() > 2 && gfx.textWidth(v) > m.w - u(wide() ? 120 : 96)) v = v.substring(0, v.length() - 3) + "..";
        gfx.setTextColor(C_DIM);
        gfx.drawString(label, u(6), y);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(v, u(wide() ? 110 : 92), y);
        y += m.line;
    }
}

void Wizard::draw() {
    const Metrics& m = metrics();
    if (!active_) return;
    drawBackground();
    drawHeader(title_.c_str(), accent_);
    // progress
    int total = 0, at = 0;
    for (size_t i = 0; i < steps_.size(); i++) if (visible(i)) { total++; if ((int)i <= idx_) at++; }
    if (review_) at = total;
    drawBar(0, m.headerH, m.w, u(2), total ? (float)at / total : 0, accent_, C_BG2);

    if (review_) { drawReview(); drawFooter("Enter save  Esc back  Up/Dn scroll"); return; }

    const Step& s = steps_[idx_];
    gfx.setTextDatum(top_left);
    gfx.setFont(m.title);
    gfx.setTextColor(accent_);
    gfx.drawString(otherMode_ ? s.label + " (custom)" : s.label, u(6), m.headerH + u(5));
    gfx.setFont(m.body);
    gfx.setTextColor(C_DIM);
    gfx.setTextDatum(top_right);
    gfx.drawString(String(at) + "/" + total, m.w - u(6), m.headerH + u(8));
    int y = m.headerH + u(24);
    if (s.hint.length()) {
        gfx.setTextDatum(top_left);
        y = textWrapped(s.hint.c_str(), u(6), y, m.w - u(12), u(9), 2) + u(2);
    }

    bool textEntry = otherMode_ || s.type == TEXT || s.type == LONGTEXT || s.type == NUMBER || s.type == DICE;
    if (textEntry) {
        int boxH = (s.type == LONGTEXT && !otherMode_) ? m.h - m.footerH - y - u(4) : u(14);
        drawPanel(u(4), y, m.w - u(8), boxH, C_BG, accent_);
        gfx.setTextColor(C_TEXT);
        gfx.setTextDatum(top_left);
        if (boxH > u(14)) {
            // show the tail of long text, wrapped
            std::vector<String> lines;
            wrapLines(edit_ + "_", m.w - u(18), lines);
            int maxL = (boxH - u(4)) / u(9);
            int first = std::max(0, (int)lines.size() - maxL);
            for (int i = first; i < (int)lines.size(); i++) gfx.drawString(lines[i], u(9), y + u(3) + (i - first) * u(9));
            gfx.setTextDatum(bottom_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(edit_.length()), m.w - u(8), y + boxH - u(2));
        } else {
            gfx.drawString(edit_, u(9), y + u(3));
            if ((millis() / 450) % 2) gfx.fillRect(u(9) + gfx.textWidth(edit_), y + u(3), u(5), u(8), accent_);
        }
        drawFooter(s.optional ? "Ent next (blank = skip)  Esc back" : "Ent next  Esc back");
    } else {
        drawChoice(s, y);
        if (s.type == MULTI) {
            int need = s.dynPick ? s.dynPick(*this) : s.pick, count = 0;
            for (bool c : checks_) count += c;
            String f = String("Spc toggle  Ent next  ") + count + (need ? String("/") + need : String(" picked"));
            drawFooter(f.c_str());
        } else {
            drawFooter("Ent choose  Esc back  letter jump");
        }
    }
}

#include "../core/app.h"
#include "../core/hold_menu.h"
#include "../core/osk.h"
#include "../core/touch.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/widgets.h"
#include <algorithm>
#include <vector>

using namespace ui;

// Notes: plain-text files in /gmtools/notes. The first line is the title.
// Editor keys: type normally, Enter = new line, Del = backspace,
// Fn + ; . , / = arrows, Fn+T = insert session time, Fn+S = save, Esc = save & close.
// Notes also autosave a few seconds after you stop typing.

namespace {

constexpr const char* DIR = "/gmtools/notes";
constexpr int MAX_LEN = 8000;
static int lineH() { return ui::metrics().line + (ui::wide() ? 6 : 0); }
constexpr uint32_t AUTOSAVE_MS = 4000;

struct NoteInfo { String file, title; size_t size; };

class NotesApp : public App {
public:
    const char* name() const override { return "Notes"; }
    const char* blurb() const override { return "Session notes, saved to SD."; }
    uint16_t accent() const override { return rgb(220, 200, 150); }

    void drawIcon(int cx, int cy, float t) override {
        // quill over a page
        gfx.fillRect(cx - 14, cy - 17, 26, 34, rgb(225, 215, 190));
        for (int i = 0; i < 5; i++) gfx.drawFastHLine(cx - 10, cy - 10 + i * 6, 16, rgb(150, 140, 120));
        int wob = (int)(sinf(t * 3) * 2);
        gfx.drawWideLine(cx + 2 + wob, cy + 12, cx + 18 + wob, cy - 16, 2, C_GOLD);
        gfx.fillTriangle(cx + 18 + wob, cy - 16, cx + 10 + wob, cy - 8, cx + 20 + wob, cy - 6, C_TEXT);
    }

    void onEnter() override {
        sessionStart_ = sessionStart_ ? sessionStart_ : millis();
        mode_ = Mode::List;
        input::setTextMode(false);
        refresh();
    }
    void onExit() override {
        if (mode_ == Mode::Edit) save(false);
        input::setTextMode(false);
    }

    bool handle(const input::Event& e) override {
        return mode_ == Mode::Edit ? handleEdit(e) : handleList(e);
    }

    void tick(uint32_t) override {
        if (mode_ == Mode::Edit && dirty_ && millis() - lastEdit_ > AUTOSAVE_MS) save(false);
    }

    bool bigLayout() const override { return true; }

    void draw() override {
        drawBackground();
        if (wide()) drawWide();
        else if (mode_ == Mode::Edit) drawEditor();
        else drawList();
    }

private:
    enum class Mode { List, Edit };
    Mode mode_ = Mode::List;
    std::vector<NoteInfo> notes_;
    int sel_ = 0;
    bool confirmDelete_ = false;

    String text_, path_;
    int cursor_ = 0, top_ = 0, wantCol_ = -1;
    bool dirty_ = false;
    bool tooLong_ = false;   // file is bigger than MAX_LEN: never save the cut-off copy
    uint32_t lastEdit_ = 0, sessionStart_ = 0;
    // RAM fallback when there's no SD card
    std::vector<String> ramNotes_;
    int editW_ = 0;        // the editor's text width, from its last draw (0: the whole screen)
    float listPos_ = 0;    // big screen: the note list's scroll, in px

    // editor columns: 38 on the Cardputer (Font0 is 6 px wide: (240 - 12) / 6)
    int cols() const { return (editW_ ? editW_ : ui::W - ui::u(12)) / ui::metrics().charW; }

    // ---------- list ----------
    void refresh() {
        notes_.clear();
        if (storage::ok()) {
            std::vector<String> files;
            storage::listFiles(DIR, files);
            for (auto& f : files) {
                String txt;
                storage::readText((String(DIR) + "/" + f).c_str(), txt, 200);
                notes_.push_back({f, firstLine(txt), txt.length()});
            }
            // newest (highest number) first
            std::reverse(notes_.begin(), notes_.end());
        } else {
            for (size_t i = 0; i < ramNotes_.size(); i++)
                notes_.push_back({String(i), firstLine(ramNotes_[i]), ramNotes_[i].length()});
        }
        if (sel_ > (int)notes_.size()) sel_ = 0;
    }

    static String firstLine(const String& t) {
        int nl = t.indexOf('\n');
        String l = nl < 0 ? t : t.substring(0, nl);
        l.trim();
        return l.length() ? l : String("(untitled)");
    }

    bool handleList(const input::Event& e) {
        int n = notes_.size() + 1;  // row 0 = "New note"
        if (!(e.key == input::K_CHAR && e.base == 'x')) confirmDelete_ = false;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: sel_ = (sel_ + n - 1) % n; sfx::click(); break;
            case input::K_DOWN: sel_ = (sel_ + 1) % n; sfx::click(); break;
            case input::K_ENTER: sel_ == 0 ? newNote() : openNote(sel_ - 1); break;
            case input::K_CHAR:
                if (e.base == 'n') newNote();
                else if (e.base == 'x' && sel_ > 0) deleteNote(sel_ - 1);
                break;
            default: break;
        }
        return true;
    }

    void newNote() {
        path_ = "";
        if (storage::ok()) {
            for (int i = 1; i < 1000; i++) {
                char p[40];
                snprintf(p, sizeof(p), "%s/note%03d.txt", DIR, i);
                if (!storage::exists(p)) { path_ = p; break; }
            }
        } else {
            ramNotes_.push_back("");
            path_ = String("ram:") + (ramNotes_.size() - 1);
            toast("No SD: note kept until power off", C_GOLD);
        }
        text_ = "";
        cursor_ = top_ = 0;
        dirty_ = false;
        tooLong_ = false;
        enterEditor();
    }

    void openNote(int i) {
        const NoteInfo& n = notes_[i];
        text_ = "";
        if (storage::ok()) {
            path_ = String(DIR) + "/" + n.file;
            storage::readText(path_.c_str(), text_, MAX_LEN);
            tooLong_ = n.size > MAX_LEN;
            if (tooLong_) toast("Note too long: read only here", C_BLOOD);
        } else {
            path_ = String("ram:") + n.file;
            text_ = ramNotes_[n.file.toInt()];
        }
        text_.replace("\r", "");
        cursor_ = text_.length();
        top_ = 0;
        dirty_ = false;
        enterEditor();
    }

    void deleteNote(int i) {
        if (!confirmDelete_) { confirmDelete_ = true; toast("X again to delete", C_BLOOD); return; }
        confirmDelete_ = false;
        if (storage::ok()) storage::remove((String(DIR) + "/" + notes_[i].file).c_str());
        else ramNotes_.erase(ramNotes_.begin() + notes_[i].file.toInt());
        sfx::back();
        refresh();
        sel_ = std::min(sel_, (int)notes_.size());
    }

    void drawList() {
        drawHeader("Notes", accent());
        gfx.setFont(ui::font(&fonts::Font0));
        int n = notes_.size() + 1;
        const int rowH = touchRow(13), pad = touchPad(13), top = HEADER_H + u(4), rows = (H - FOOTER_H - top) / rowH;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH + pad;
            bool s = i == sel_;
            if (s) drawPanel(u(4), y - pad - u(2), W - u(8), rowH, C_PANEL, accent());
            touch::row(0, y - pad - u(2), W, rowH, sel_, i);
            gfx.setTextDatum(top_left);
            if (i == 0) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString("+ New note", u(12), y + u(1));
                continue;
            }
            touch::onHold(0, y - pad - u(2), W, rowH, [this, i] {
                sel_ = i;
                holdmenu::open(notes_[i - 1].title, {{"Open", [this, i] { openNote(i - 1); }},
                                                     {"Delete", [this, i] { confirmDelete_ = true; deleteNote(i - 1); }, C_BLOOD, true}});
            });
            const NoteInfo& ni = notes_[i - 1];
            String t = ni.title;
            if (wide()) t = fitText(t, W - u(60));
            else if (t.length() > 30) t = t.substring(0, 29) + ".";
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            gfx.drawString(t, u(12), y + u(1));
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(ni.size >= 1000 ? String(ni.size / 1000) + "k" : String(ni.size), W - u(10), y + u(1));
        }
        drawFooter(!storage::ok() ? "No SD card: notes are lost at power off"
                   : wide()       ? "Ent open  N new  hold for more  Esc back"
                                  : "Ent open  N new  X del  Esc back");
    }

    // ---------- editor ----------
    void enterEditor() {
        mode_ = Mode::Edit;
        wantCol_ = -1;
        input::setTextMode(true);
        sfx::select();
    }

    void save(bool announce) {
        if (!dirty_ && !announce) return;
        if (tooLong_) { if (announce) toast("Too long to save here - edit on a PC", C_BLOOD); dirty_ = false; return; }
        bool ok;
        if (path_.startsWith("ram:")) {
            ramNotes_[path_.substring(4).toInt()] = text_;
            ok = true;
        } else {
            ok = storage::writeText(path_.c_str(), text_);
        }
        if (ok) dirty_ = false;
        if (ok && wide()) refresh();   // the list beside the editor shows the new title
        if (announce) ok ? toast("Saved", C_EMERALD) : toast("Save failed", C_BLOOD);
        if (!ok) sfx::error();
    }

    void insert(const String& s) {
        if ((int)(text_.length() + s.length()) > MAX_LEN) { sfx::error(); toast("Note is full", C_BLOOD); return; }
        text_ = text_.substring(0, cursor_) + s + text_.substring(cursor_);
        cursor_ += s.length();
        changed();
    }

    void changed() { dirty_ = true; lastEdit_ = millis(); wantCol_ = -1; }

    // Visual lines: word-wrapped at COLS, hard breaks at '\n'.
    struct VLine { int start, len; };
    void layout(std::vector<VLine>& out) {
        out.clear();
        int n = text_.length(), i = 0;
        while (true) {
            int start = i, lastSpace = -1, len = 0;
            while (i < n && text_[i] != '\n' && len < cols()) {
                if (text_[i] == ' ') lastSpace = i;
                i++; len++;
            }
            if (i < n && text_[i] != '\n' && lastSpace >= start) {  // wrap at the last space
                i = lastSpace + 1;
                len = i - start;
            }
            out.push_back({start, len});
            if (i < n && text_[i] == '\n') { i++; if (i == n) { out.push_back({n, 0}); break; } continue; }
            if (i >= n) break;
        }
    }

    void cursorPos(const std::vector<VLine>& lines, int& row, int& col) {
        row = 0;
        for (size_t r = 0; r < lines.size(); r++) {
            if (cursor_ >= lines[r].start) row = r;
            else break;
        }
        // a cursor exactly at a soft-wrap boundary belongs to the next line
        col = std::min(cursor_ - lines[row].start, cols());
    }

    // "[1:05] ": time since the session started
    void insertTime() {
        uint32_t m = (millis() - sessionStart_) / 60000;
        char b[16];
        snprintf(b, sizeof(b), "[%lu:%02lu] ", (unsigned long)(m / 60), (unsigned long)(m % 60));
        insert(b);
    }

    void closeEditor() {
        save(false);
        mode_ = Mode::List;
        input::setTextMode(false);
        refresh();
        sfx::back();
    }

    bool handleEdit(const input::Event& e) {
        if (e.fn && e.key == input::K_CHAR) {
            if (e.base == 's') { save(true); sfx::select(); return true; }
            if (e.base == 't') { insertTime(); return true; }
        }
        std::vector<VLine> lines;
        switch (e.key) {
            case input::K_BACK: closeEditor(); return true;
            case input::K_ENTER: insert("\n"); return true;
            case input::K_TAB: insert("  "); return true;
            case input::K_DEL:
                if (cursor_ > 0) { text_.remove(cursor_ - 1, 1); cursor_--; changed(); }
                return true;
            case input::K_LEFT: if (cursor_ > 0) cursor_--; wantCol_ = -1; return true;
            case input::K_RIGHT: if (cursor_ < (int)text_.length()) cursor_++; wantCol_ = -1; return true;
            case input::K_UP:
            case input::K_DOWN: {
                layout(lines);
                int row, col;
                cursorPos(lines, row, col);
                if (wantCol_ < 0) wantCol_ = col;
                int nr = row + (e.key == input::K_UP ? -1 : 1);
                if (nr < 0) { cursor_ = 0; return true; }
                if (nr >= (int)lines.size()) { cursor_ = text_.length(); return true; }
                cursor_ = lines[nr].start + std::min(wantCol_, lines[nr].len);
                return true;
            }
            case input::K_CHAR: insert(String(e.ch)); return true;
            default: return true;
        }
    }

    void drawEditor() {
        String title = firstLine(text_);
        if (!wide() && title.length() > 24) title = title.substring(0, 23) + ".";
        drawHeader(title.c_str(), accent());
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(dirty_ ? C_GOLD : C_EMERALD);
        gfx.drawString(dirty_ ? "*" : "saved", W - u(30), HEADER_H / 2);

        gfx.setFont(ui::font(&fonts::Font0));
        drawText(0, HEADER_H, W, H - HEADER_H - FOOTER_H);
        drawFooter("Esc close  FnS save  FnT time  Fn+arrows");
    }

    // The note's text in (x, y, w, h), with the cursor; a tap moves the cursor there.
    void drawText(int x, int y, int w, int h) {
        const int pad = u(6), tx = x + pad, areaTop = y + u(3);
        editW_ = wide() ? w - 2 * pad : 0;
        std::vector<VLine> lines;
        layout(lines);
        int row, col;
        cursorPos(lines, row, col);
        const int visible = std::max(1, (h - u(5)) / lineH());
        if (row < top_) top_ = row;
        if (row >= top_ + visible) top_ = row - visible + 1;

        gfx.fillRect(x, y, w, h, rgb(20, 18, 26));
        gfx.setTextDatum(top_left);
        const int cw = metrics().charW;
        for (int r = top_; r < (int)lines.size() && r < top_ + visible; r++) {
            int ly = areaTop + (r - top_) * lineH();
            String s = text_.substring(lines[r].start, lines[r].start + lines[r].len);
            // first line is the title
            gfx.setTextColor(r == 0 ? accent() : C_TEXT);
            // highlight [timestamps]
            if (s.startsWith("[") && s.indexOf(']') > 0 && s.indexOf(']') < 8) {
                int b = s.indexOf(']') + 1;
                gfx.setTextColor(C_ARCANE);
                gfx.drawString(s.substring(0, b), tx, ly);
                gfx.setTextColor(C_TEXT);
                gfx.drawString(s.substring(b), tx + b * cw, ly);
            } else {
                gfx.drawString(s, tx, ly);
            }
        }
        if ((millis() / 450) % 2 || millis() - lastEdit_ < 400) {
            int cy = areaTop + (row - top_) * lineH();
            gfx.fillRect(tx + col * cw, cy - u(1), wide() ? 3 : u(1), lineH() - u(1), C_GOLD);
        }
        // scrollbar
        if ((int)lines.size() > visible) {
            int areaH = visible * lineH();
            int sh = std::max(u(6), areaH * visible / (int)lines.size());
            int sy = areaTop + (areaH - sh) * top_ / std::max(1, (int)lines.size() - visible);
            gfx.fillRect(x + w - u(3), sy, u(2), sh, C_GOLD_DK);
        }
        // a tap puts the cursor under the finger
        touch::zone(x, y, w, h, [this, tx, areaTop, cw] {
            std::vector<VLine> ls;
            layout(ls);
            const int r = std::max(0, std::min((int)ls.size() - 1, top_ + (touch::tapY() - areaTop) / lineH()));
            const int c = std::max(0, std::min(ls[r].len, (touch::tapX() - tx + cw / 2) / cw));
            cursor_ = ls[r].start + c;
            wantCol_ = -1;
            lastEdit_ = millis() - 1000;   // the cursor shows at once
        });
        touch::onHold(x, y, w, h, [this] {
            holdmenu::open(firstLine(text_), {{"Add the time", [this] { insertTime(); }},
                                              {"Save", [this] { save(true); sfx::select(); }},
                                              {"Close", [this] { closeEditor(); }}});
        });
    }

    // ---------- big screen: the notes on the left, the open one on the right
    int openIndex() const {
        for (size_t i = 0; i < notes_.size(); i++)
            if (mode_ == Mode::Edit && path_.endsWith("/" + notes_[i].file)) return i;
        return -1;
    }

    void openWide(int i) {
        if (mode_ == Mode::Edit) save(false);
        if (i < 0 || i >= (int)notes_.size()) return;
        const String file = notes_[i].file;
        refresh();
        for (size_t k = 0; k < notes_.size(); k++) if (notes_[k].file == file) { openNote(k); return; }
    }

    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawHeader(mode_ == Mode::Edit ? firstLine(text_).c_str() : "Notes", accent());
        const int LX = 16, LW = 380, RX = LX + LW + 16, RW = W - RX - 16, top = HEADER_H + 10;
        const int bottom = H - (osk::visible() ? osk::height() : FOOTER_H) - 10;

        // the list
        button(LX, top, LW, 56, "+ New note", false, C_GOLD, [this] { if (mode_ == Mode::Edit) save(false); newNote(); });
        const int ly = top + 66, lh = bottom - ly, rowH = 92;
        const int n = notes_.size(), cur = openIndex();
        const float maxPos = std::max(0, n * rowH - lh);
        listPos_ = std::max(0.0f, std::min(listPos_, maxPos + 60));
        touch::scroller(LX, ly, LW, lh, &listPos_, maxPos);
        gfx.setClipRect(LX, ly, LW, lh);
        for (int i = 0; i < n; i++) {
            const int y = ly + i * rowH - (int)listPos_;
            if (y + rowH < ly || y > ly + lh) continue;
            const bool on = i == cur;
            gfx.fillRoundRect(LX, y, LW, rowH - 8, 10, on ? C_PANEL : C_BG2);
            gfx.drawRoundRect(LX, y, LW, rowH - 8, 10, on ? accent() : C_EDGE);
            gfx.setFont(m.bold);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(on ? accent() : C_TEXT);
            gfx.drawString(fitText(notes_[i].title, LW - 32), LX + 16, y + 10);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            const size_t sz = notes_[i].size;
            gfx.drawString(sz >= 1000 ? String(sz / 1000) + "k characters" : String(sz) + " characters", LX + 16, y + 50);
            const int vy = std::max(y, ly), vh = std::min(y + rowH - 8, ly + lh) - vy;
            if (vh > 0) {
                touch::zone(LX, vy, LW, vh, [this, i] { openWide(i); });
                touch::onHold(LX, vy, LW, vh, [this, i] {
                    holdmenu::open(notes_[i].title, {{"Open", [this, i] { openWide(i); }},
                                                     {"Delete", [this, i] { removeWide(i); }, C_BLOOD, true}});
                });
            }
        }
        gfx.clearClipRect();
        if (!n) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString(storage::ok() ? "No notes yet." : "No SD card: notes are lost at power off", LX + LW / 2, ly + 20);
        }

        // the open note
        if (mode_ != Mode::Edit) {
            drawPanel(RX, top, RW, bottom - top, C_BG2, C_EDGE);
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("Pick a note, or start a new one.", RX + RW / 2, (top + bottom) / 2 - 20);
            gfx.setFont(m.small);
            gfx.drawString("The first line is its title. Notes save themselves as you type.", RX + RW / 2, (top + bottom) / 2 + 24);
            drawFooter("Ent open  N new  hold for more  Esc back");
            return;
        }
        // toolbar: what the Fn keys do
        const int bw = 150, bh = 54;
        int bx = RX + RW - 4 * bw - 3 * 10;
        gfx.setFont(m.small);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(dirty_ ? C_GOLD : C_EMERALD);
        gfx.drawString(tooLong_ ? "read only: too long" : dirty_ ? "editing..." : "saved", RX + 4, top + bh / 2);
        button(bx, top, bw, bh, "Time", false, C_ARCANE, [this] { insertTime(); });
        button(bx += bw + 10, top, bw, bh, "Save", false, C_EMERALD, [this] { save(true); sfx::select(); }, dirty_);
        button(bx += bw + 10, top, bw, bh, "Delete", false, C_BLOOD, [this] {
            const int i = openIndex();
            if (i < 0) { closeEditor(); return; }   // never saved: nothing to delete
            holdmenu::open(notes_[i].title, {{"Delete this note", [this, i] { removeWide(i); }, C_BLOOD, true}});
        });
        button(bx += bw + 10, top, bw, bh, "Close", false, C_DIM, [this] { closeEditor(); });
        const int ty = top + bh + 10;
        gfx.setFont(m.body);
        drawText(RX, ty, RW, bottom - ty);
        gfx.drawRect(RX, ty, RW, bottom - ty, C_EDGE);
        drawFooter("Esc close  type to write  hold the text for more");
    }

    void removeWide(int i) {
        const bool wasOpen = i == openIndex();
        if (wasOpen) { mode_ = Mode::List; input::setTextMode(false); dirty_ = false; path_ = ""; }
        confirmDelete_ = true;
        deleteNote(i);
    }
};

}  // namespace

App* makeNotesApp() { return new NotesApp(); }

#include "../core/app.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
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
constexpr int COLS = 38;          // Font0 is 6 px wide: (240 - 12) / 6
constexpr int LINE_H = 10;
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

    void draw() override {
        drawBackground();
        if (mode_ == Mode::Edit) drawEditor();
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
        gfx.setFont(&fonts::Font0);
        int n = notes_.size() + 1;
        const int rowH = 13, top = HEADER_H + 4, rows = 8;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH;
            bool s = i == sel_;
            if (s) drawPanel(4, y - 2, W - 8, rowH, C_PANEL, accent());
            gfx.setTextDatum(top_left);
            if (i == 0) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString("+ New note", 12, y + 1);
                continue;
            }
            const NoteInfo& ni = notes_[i - 1];
            String t = ni.title;
            if (t.length() > 30) t = t.substring(0, 29) + ".";
            gfx.setTextColor(s ? C_TEXT : rgb(200, 192, 180));
            gfx.drawString(t, 12, y + 1);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(ni.size >= 1000 ? String(ni.size / 1000) + "k" : String(ni.size), W - 10, y + 1);
        }
        drawFooter(storage::ok() ? "Ent open  N new  X del  Esc back" : "No SD card: notes are lost at power off");
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
            while (i < n && text_[i] != '\n' && len < COLS) {
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
        col = std::min(cursor_ - lines[row].start, COLS);
    }

    bool handleEdit(const input::Event& e) {
        if (e.fn && e.key == input::K_CHAR) {
            if (e.base == 's') { save(true); sfx::select(); return true; }
            if (e.base == 't') {
                uint32_t m = (millis() - sessionStart_) / 60000;
                char b[16];
                snprintf(b, sizeof(b), "[%lu:%02lu] ", (unsigned long)(m / 60), (unsigned long)(m % 60));
                insert(b);
                return true;
            }
        }
        std::vector<VLine> lines;
        switch (e.key) {
            case input::K_BACK:
                save(false);
                mode_ = Mode::List;
                input::setTextMode(false);
                refresh();
                sfx::back();
                return true;
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
        if (title.length() > 24) title = title.substring(0, 23) + ".";
        drawHeader(title.c_str(), accent());
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(dirty_ ? C_GOLD : C_EMERALD);
        gfx.drawString(dirty_ ? "*" : "saved", W - 30, HEADER_H / 2);

        std::vector<VLine> lines;
        layout(lines);
        int row, col;
        cursorPos(lines, row, col);
        const int areaTop = HEADER_H + 3;
        const int visible = (H - FOOTER_H - areaTop - 2) / LINE_H;
        if (row < top_) top_ = row;
        if (row >= top_ + visible) top_ = row - visible + 1;

        gfx.fillRect(0, HEADER_H, W, H - HEADER_H - FOOTER_H, rgb(20, 18, 26));
        gfx.setTextDatum(top_left);
        for (int r = top_; r < (int)lines.size() && r < top_ + visible; r++) {
            int y = areaTop + (r - top_) * LINE_H;
            String s = text_.substring(lines[r].start, lines[r].start + lines[r].len);
            // first line is the title
            gfx.setTextColor(r == 0 ? accent() : C_TEXT);
            // highlight [timestamps]
            if (s.startsWith("[") && s.indexOf(']') > 0 && s.indexOf(']') < 8) {
                int b = s.indexOf(']') + 1;
                gfx.setTextColor(C_ARCANE);
                gfx.drawString(s.substring(0, b), 6, y);
                gfx.setTextColor(C_TEXT);
                gfx.drawString(s.substring(b), 6 + b * 6, y);
            } else {
                gfx.drawString(s, 6, y);
            }
        }
        if ((millis() / 450) % 2 || millis() - lastEdit_ < 400) {
            int cy = areaTop + (row - top_) * LINE_H;
            gfx.fillRect(6 + col * 6, cy - 1, 1, 9, C_GOLD);
        }
        // scrollbar
        if ((int)lines.size() > visible) {
            int areaH = visible * LINE_H;
            int sh = std::max(6, areaH * visible / (int)lines.size());
            int sy = areaTop + (areaH - sh) * top_ / std::max(1, (int)lines.size() - visible);
            gfx.fillRect(W - 3, sy, 2, sh, C_GOLD_DK);
        }
        drawFooter("Esc close  FnS save  FnT time  Fn+arrows");
    }
};

}  // namespace

App* makeNotesApp() { return new NotesApp(); }

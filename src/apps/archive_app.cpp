#include "../core/app.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include <vector>

using namespace ui;

// Archive: browse and delete what you've saved on the SD card - your own pages
// (/gmtools/ref/*.txt), saved dungeons and roll logs. (Rules live in the Rules app.)
// Page markup: "# Heading" lines are drawn in gold, "- " lines are bullets.

namespace {

struct Entry { String title; const char* builtin = nullptr; String path; bool header = false; };

class ArchiveApp : public App {
public:
    const char* name() const override { return "Archive"; }
    const char* blurb() const override { return "Saved pages, dungeons & logs."; }
    uint16_t accent() const override { return C_ICE; }

    void drawIcon(int cx, int cy, float t) override {
        // filing box with a paper peeking out
        int lift = (int)(fabsf(sinf(t * 2)) * 4);
        gfx.fillRect(cx - 9, cy - 16 - lift, 18, 16, rgb(230, 222, 200));
        for (int i = 0; i < 3; i++) gfx.drawFastHLine(cx - 6, cy - 12 - lift + i * 4, 12, rgb(140, 130, 110));
        gfx.fillRoundRect(cx - 18, cy - 6, 36, 22, 2, rgb(150, 105, 60));
        gfx.fillRect(cx - 20, cy - 8, 40, 6, rgb(175, 125, 72));
        gfx.drawRoundRect(cx - 18, cy - 6, 36, 22, 2, C_GOLD);
        gfx.fillRoundRect(cx - 6, cy + 2, 12, 5, 2, rgb(90, 60, 35));
    }

    void onEnter() override {
        input::setTextMode(false);
        build();
        view_ = 0;
    }

    bool handle(const input::Event& e) override {
        if (view_ == 1) {
            int maxScroll = std::max(0, (int)lines_.size() - visibleLines());
            switch (e.key) {
                case input::K_BACK: view_ = 0; sfx::back(); break;
                case input::K_UP: scroll_ = std::max(0, scroll_ - 1); break;
                case input::K_DOWN: scroll_ = std::min(maxScroll, scroll_ + 1); break;
                case input::K_LEFT: scroll_ = std::max(0, scroll_ - visibleLines() + 1); break;
                case input::K_RIGHT: case input::K_CHAR:
                    if (e.key == input::K_CHAR && e.base != ' ') break;
                    scroll_ = std::min(maxScroll, scroll_ + visibleLines() - 1); break;
                default: break;
            }
            return true;
        }
        if (entries_.empty()) return e.key != input::K_BACK;
        bool del = e.key == input::K_CHAR && e.fn && e.base == 'x';
        bool clearRow = entries_[sel_].path == CLEAR_LOGS;
        if (!del && !(clearRow && e.key == input::K_ENTER)) confirm_ = false;
        switch (e.key) {
            case input::K_BACK: return false;
            case input::K_UP: move(-1); break;
            case input::K_DOWN: move(1); break;
            case input::K_ENTER:
                if (clearRow) clearLogs();
                else open();
                break;
            default: break;
        }
        if (del) deleteSelected();
        return true;
    }

    void draw() override {
        drawBackground();
        drawHeader(view_ == 1 ? title_.c_str() : "Archive", C_ICE);
        if (view_ == 1) drawPage();
        else drawIndex();
    }

private:
    std::vector<Entry> entries_;
    std::vector<String> lines_;
    String title_;
    int sel_ = 0, view_ = 0, scroll_ = 0;
    bool confirm_ = false;
    static constexpr const char* CLEAR_LOGS = "@clear-logs";

    void deleteSelected() {
        const Entry& en = entries_[sel_];
        if (!en.path.length() || en.path == CLEAR_LOGS) { sfx::error(); toast("Built-in pages can't be deleted", C_DIM); return; }
        if (!confirm_) { confirm_ = true; toast((String("Fn+X again to delete ") + en.title).c_str(), C_BLOOD); return; }
        confirm_ = false;
        bool ok = storage::remove(en.path.c_str());
        ok ? toast("Deleted", C_DIM) : toast("Delete failed", C_BLOOD);
        sfx::back();
        int keep = sel_;
        build();
        if (entries_.empty()) { sel_ = 0; return; }
        sel_ = std::max(0, std::min(keep, (int)entries_.size() - 1));
        if (entries_[sel_].header) move(-1);
    }

    void clearLogs() {
        if (!confirm_) { confirm_ = true; toast("Enter again to delete ALL roll logs", C_BLOOD); return; }
        confirm_ = false;
        std::vector<String> files;
        storage::listFiles("/gmtools/logs", files);
        int n = 0;
        for (auto& f : files) n += storage::remove((String("/gmtools/logs/") + f).c_str());
        toast((String("Deleted ") + n + " logs").c_str(), C_DIM);
        sfx::back();
        build();
        sel_ = 1;
    }

    static constexpr int LINE_H = 10;
    int visibleLines() const { return (H - HEADER_H - FOOTER_H - 6) / LINE_H; }

    void header(const String& t) { Entry e; e.title = t; e.header = true; entries_.push_back(e); }

    void build() {
        entries_.clear();
        addDir("/gmtools/ref", "My pages");
        addDir("/gmtools/dungeons", "Saved dungeons");
        if (addDir("/gmtools/logs", "Roll logs")) {
            Entry e;
            e.title = "Delete all roll logs";
            e.path = CLEAR_LOGS;
            entries_.push_back(e);
        }
        if (sel_ >= (int)entries_.size() || (entries_.size() && entries_[sel_].header)) sel_ = entries_.size() > 1 ? 1 : 0;
    }

    bool addDir(const char* dir, const char* heading) {
        std::vector<String> files;
        if (!storage::listFiles(dir, files) || files.empty()) return false;
        if (heading) header(heading);
        for (auto& f : files) {
            Entry e;
            e.title = f.substring(0, f.length() - 4);
            e.title.replace('_', ' ');
            e.path = String(dir) + "/" + f;
            entries_.push_back(e);
        }
        return true;
    }

    void move(int d) {
        int n = entries_.size();
        for (int i = 0; i < n; i++) {
            sel_ = (sel_ + d + n) % n;
            if (!entries_[sel_].header) break;
        }
        sfx::click();
    }

    void open() {
        const Entry& e = entries_[sel_];
        String text;
        if (e.builtin) text = e.builtin;
        else if (!storage::readText(e.path.c_str(), text)) { sfx::error(); toast("Can't read file", C_BLOOD); return; }
        text.replace("\r", "");
        title_ = e.title;
        lines_.clear();
        gfx.setFont(&fonts::Font0);
        // wrap per source line so heading/bullet markers survive
        int start = 0;
        while (start <= (int)text.length()) {
            int nl = text.indexOf('\n', start);
            String l = nl < 0 ? text.substring(start) : text.substring(start, nl);
            if (l.startsWith("# ")) lines_.push_back(l);
            else if (l.startsWith("- ")) {
                std::vector<String> w;
                wrapLines(l.substring(2), W - 22, w);
                for (size_t i = 0; i < w.size(); i++) lines_.push_back((i ? "\x02" : "\x01") + w[i]);
            } else {
                std::vector<String> w;
                wrapLines(l, W - 14, w);
                for (auto& x : w) lines_.push_back(x);
            }
            if (nl < 0) break;
            start = nl + 1;
        }
        scroll_ = 0;
        view_ = 1;
        sfx::select();
    }

    void drawIndex() {
        int n = entries_.size();
        if (!n) {
            gfx.setFont(&fonts::Font0);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString(storage::ok() ? "Nothing saved yet." : "No SD card.", W / 2, 50);
            gfx.drawString("Saved dungeons, roll logs and", W / 2, 70);
            gfx.drawString(".txt pages in /gmtools/ref show here.", W / 2, 81);
            drawFooter("Esc back");
            return;
        }
        const int rowH = 12, top = HEADER_H + 4, rows = 8;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH;
            const Entry& e = entries_[i];
            if (e.header) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString(e.title, 6, y + 2);
                gfx.drawFastHLine(6 + gfx.textWidth(e.title) + 4, y + 5, W - 20 - gfx.textWidth(e.title), C_GOLD_DK);
                continue;
            }
            bool s = i == sel_;
            if (s) drawPanel(10, y, W - 16, rowH, C_PANEL, C_ICE);
            gfx.setTextColor(e.path == CLEAR_LOGS ? (confirm_ ? C_BLOOD : rgb(230, 140, 130)) : s ? C_TEXT : rgb(190, 190, 200));
            gfx.drawString(e.path == CLEAR_LOGS && confirm_ ? String("Enter again to delete all") : e.title, 18, y + 2);
        }
        drawFooter("Ent open  FnX delete  Esc back");
    }

    void drawPage() {
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        int y = HEADER_H + 3;
        int vis = visibleLines();
        for (int i = scroll_; i < (int)lines_.size() && i < scroll_ + vis; i++, y += LINE_H) {
            const String& l = lines_[i];
            if (l.startsWith("# ")) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString(l.substring(2), 6, y + 1);
            } else if (l.length() && (l[0] == '\x01' || l[0] == '\x02')) {
                if (l[0] == '\x01') gfx.fillCircle(11, y + 4, 1, C_ICE);
                gfx.setTextColor(C_TEXT);
                gfx.drawString(l.substring(1), 16, y + 1);
            } else {
                gfx.setTextColor(C_TEXT);
                gfx.drawString(l, 7, y + 1);
            }
        }
        int maxScroll = std::max(0, (int)lines_.size() - vis);
        if (maxScroll) {
            int areaH = vis * LINE_H, sh = std::max(6, areaH * vis / (int)lines_.size());
            gfx.fillRect(W - 3, HEADER_H + 3 + (areaH - sh) * scroll_ / maxScroll, 2, sh, C_ICE);
        }
        drawFooter("Up/Dn scroll  </> page  Esc back");
    }
};

}  // namespace

App* makeArchiveApp() { return new ArchiveApp(); }

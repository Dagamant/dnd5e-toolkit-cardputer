#include "../core/app.h"
#include "../core/hold_menu.h"
#include "../core/touch.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/textview.h"
#include "../core/ui.h"
#include "../core/widgets.h"
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
        gfx.fillRect(cx - u(9), cy - u(16) - lift, u(18), u(16), rgb(230, 222, 200));
        for (int i = 0; i < 3; i++) gfx.drawFastHLine(cx - u(6), cy - u(12) - lift + i * 4, u(12), rgb(140, 130, 110));
        gfx.fillRoundRect(cx - u(18), cy - u(6), u(36), u(22), u(2), rgb(150, 105, 60));
        gfx.fillRect(cx - u(20), cy - u(8), u(40), u(6), rgb(175, 125, 72));
        gfx.drawRoundRect(cx - u(18), cy - u(6), u(36), u(22), u(2), C_GOLD);
        gfx.fillRoundRect(cx - u(6), cy + u(2), u(12), u(5), u(2), rgb(90, 60, 35));
    }

    void onEnter() override {
        input::setTextMode(false);
        build();
        view_ = 0;
    }

    bool handle(const input::Event& e) override {
        if (wide()) return handleWide(e);
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

    bool bigLayout() const override { return true; }

    void draw() override {
        drawBackground();
        if (wide()) { drawWide(); return; }
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
        openIdx_ = -1;
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
        openIdx_ = -1;
    }

    int lineH() const { return wide() ? ui::metrics().line : 10; }
    int visibleLines() const { return (H - HEADER_H - FOOTER_H - u(6)) / lineH(); }

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
        if (wide()) { openWide(sel_); return; }
        const Entry& e = entries_[sel_];
        String text;
        if (e.builtin) text = e.builtin;
        else if (!storage::readText(e.path.c_str(), text)) { sfx::error(); toast("Can't read file", C_BLOOD); return; }
        text.replace("\r", "");
        title_ = e.title;
        lines_.clear();
        gfx.setFont(ui::font(&fonts::Font0));
        // wrap per source line so heading/bullet markers survive
        int start = 0;
        while (start <= (int)text.length()) {
            int nl = text.indexOf('\n', start);
            String l = nl < 0 ? text.substring(start) : text.substring(start, nl);
            if (l.startsWith("# ")) lines_.push_back(l);
            else if (l.startsWith("- ")) {
                std::vector<String> w;
                wrapLines(l.substring(2), W - u(22), w);
                for (size_t i = 0; i < w.size(); i++) lines_.push_back((i ? "\x02" : "\x01") + w[i]);
            } else {
                std::vector<String> w;
                wrapLines(l, W - u(14), w);
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
            gfx.setFont(ui::font(&fonts::Font0));
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString(storage::ok() ? "Nothing saved yet." : "No SD card.", W / 2, u(50));
            gfx.drawString("Saved dungeons, roll logs and", W / 2, u(70));
            gfx.drawString(".txt pages in /gmtools/ref show here.", W / 2, u(81));
            drawFooter("Esc back");
            return;
        }
        const int rowH = touchRow(12), pad = touchPad(12), top = HEADER_H + u(4), rows = (H - top - FOOTER_H - u(2)) / rowH;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH + pad;
            const Entry& e = entries_[i];
            if (e.header) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString(e.title, u(6), y + u(2));
                gfx.drawFastHLine(u(6) + gfx.textWidth(e.title) + u(4), y + u(5), W - u(20) - gfx.textWidth(e.title), C_GOLD_DK);
                continue;
            }
            bool s = i == sel_;
            if (s) drawPanel(u(10), y - pad, W - u(16), rowH, C_PANEL, C_ICE);
            touch::row(0, y - pad, W, rowH, sel_, i);
            if (e.path.length() && e.path != CLEAR_LOGS)
                touch::onHold(0, y - pad, W, rowH, [this, i] {
                    sel_ = i;
                    holdmenu::open(entries_[i].title, {{"Open", [this] { open(); }},
                                                       {"Delete", [this] { confirm_ = true; deleteSelected(); }, C_BLOOD, true}});
                });
            gfx.setTextColor(e.path == CLEAR_LOGS ? (confirm_ ? C_BLOOD : rgb(230, 140, 130)) : s ? C_TEXT : rgb(190, 190, 200));
            gfx.drawString(e.path == CLEAR_LOGS && confirm_ ? String("Enter again to delete all") : e.title, u(18), y + u(2));
        }
        drawFooter(wide() ? "Ent open  hold for more  Esc back" : "Ent open  FnX delete  Esc back");
    }

    void drawPage() {
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        int y = HEADER_H + u(3);
        const int LINE_H = lineH();
        int vis = visibleLines();
        for (int i = scroll_; i < (int)lines_.size() && i < scroll_ + vis; i++, y += LINE_H) {
            const String& l = lines_[i];
            if (l.startsWith("# ")) {
                gfx.setTextColor(C_GOLD);
                gfx.drawString(l.substring(2), u(6), y + u(1));
            } else if (l.length() && (l[0] == '\x01' || l[0] == '\x02')) {
                if (l[0] == '\x01') gfx.fillCircle(u(11), y + u(4), u(1), C_ICE);
                gfx.setTextColor(C_TEXT);
                gfx.drawString(l.substring(1), u(16), y + u(1));
            } else {
                gfx.setTextColor(C_TEXT);
                gfx.drawString(l, u(7), y + u(1));
            }
        }
        int maxScroll = std::max(0, (int)lines_.size() - vis);
        if (maxScroll) {
            int areaH = vis * LINE_H, sh = std::max(u(6), areaH * vis / (int)lines_.size());
            gfx.fillRect(W - u(3), HEADER_H + u(3) + (areaH - sh) * scroll_ / maxScroll, u(2), sh, C_ICE);
        }
        drawFooter("Up/Dn scroll  </> page  Esc back");
    }

    // ---------------------------------------------------------------- big screen
    // The saved files on the left as cards, grouped; the open one on the right
    // in a reader that scrolls with a finger.
    TextView page_;
    int openIdx_ = -1;     // the entry shown on the right
    float listPos_ = 0;

    bool handleWide(const input::Event& e) {
        if (entries_.empty()) return e.key != input::K_BACK;
        const bool clearRow = entries_[sel_].path == CLEAR_LOGS;
        if (!(clearRow && e.key == input::K_ENTER)) confirm_ = false;
        switch (e.key) {
            case input::K_BACK:
                if (openIdx_ < 0) return false;
                openIdx_ = -1;
                sfx::back();
                return true;
            case input::K_UP: move(-1); return true;
            case input::K_DOWN: move(1); return true;
            case input::K_ENTER: clearRow ? clearLogs() : openWide(sel_); return true;
            case input::K_LEFT: page_.page(-1); return true;
            case input::K_RIGHT: page_.page(1); return true;
            case input::K_CHAR:
                if (e.base == ' ') page_.page(1);
                else if (e.fn && e.base == 'x') deleteSelected();
                return true;
            default: return true;
        }
    }

    // What kind of file an entry is, from its folder.
    static const char* kindOf(const String& path) {
        if (path.startsWith("/gmtools/dungeons")) return "Dungeon";
        if (path.startsWith("/gmtools/logs")) return "Roll log";
        return "Page";
    }

    void openWide(int i) {
        const Entry& e = entries_[i];
        String text;
        if (!storage::readText(e.path.c_str(), text)) { sfx::error(); toast("Can't read file", C_BLOOD); return; }
        text.replace("\r", "");
        text.replace("\n- ", "\n* ");   // the reader's bullets
        if (text.startsWith("- ")) text = "* " + text.substring(2);
        sel_ = openIdx_ = i;
        title_ = e.title;
        page_.clear();
        page_.setWidth(readerW_);
        page_.add(text, TextView::BODY, readerW_ - 24);
        sfx::select();
    }

    int readerW_ = 800;

    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawHeader(openIdx_ >= 0 ? title_.c_str() : "Archive", C_ICE);
        const int LX = 16, LW = 400, RX = LX + LW + 16, RW = W - RX - 16, top = HEADER_H + 10, bottom = H - FOOTER_H - 10;
        readerW_ = RW - 32;

        // the files, grouped under their headings
        bool logs = false;
        for (auto& e : entries_) logs |= e.path == CLEAR_LOGS;
        const int ly = top, lh = bottom - ly - (logs ? 66 : 0), cardH = 84, headH = 44;
        std::vector<int> ys;
        int total = 0;
        for (auto& e : entries_) {
            ys.push_back(total);
            if (e.path != CLEAR_LOGS) total += e.header ? headH : cardH;
        }
        const float maxPos = std::max(0, total - 8 - lh);
        listPos_ = std::max(0.0f, std::min(listPos_, maxPos + 60));
        touch::scroller(LX, ly, LW, lh, &listPos_, maxPos);
        gfx.setClipRect(LX, ly, LW, lh);
        for (int i = 0; i < (int)entries_.size(); i++) {
            const Entry& e = entries_[i];
            if (e.path == CLEAR_LOGS) continue;
            const int y = ly + ys[i] - (int)listPos_;
            if (y + cardH < ly || y > ly + lh) continue;
            if (e.header) {
                gfx.setFont(m.small);
                gfx.setTextDatum(middle_left);
                gfx.setTextColor(C_GOLD);
                gfx.drawString(e.title, LX + 4, y + headH / 2);
                const int tw = gfx.textWidth(e.title);
                gfx.drawFastHLine(LX + tw + 16, y + headH / 2, LW - tw - 20, C_GOLD_DK);
                continue;
            }
            const bool on = i == openIdx_, sel = i == sel_;
            gfx.fillRoundRect(LX, y, LW, cardH - 8, 10, on ? C_PANEL : C_BG2);
            gfx.drawRoundRect(LX, y, LW, cardH - 8, 10, on || sel ? C_ICE : C_EDGE);
            gfx.setFont(m.bold);
            gfx.setTextDatum(top_left);
            gfx.setTextColor(on ? C_ICE : C_TEXT);
            gfx.drawString(fitText(e.title, LW - 32), LX + 16, y + 10);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(kindOf(e.path), LX + 16, y + 46);
            const int vy = std::max(y, ly), vh = std::min(y + cardH - 8, ly + lh) - vy;
            if (vh <= 0) continue;
            touch::zone(LX, vy, LW, vh, [this, i] { openWide(i); });
            touch::onHold(LX, vy, LW, vh, [this, i] {
                sel_ = i;
                holdmenu::open(entries_[i].title, {{"Open", [this, i] { openWide(i); }},
                                                   {"Delete", [this] { confirm_ = true; deleteSelected(); }, C_BLOOD, true}});
            });
        }
        gfx.clearClipRect();
        if (logs) {
            bool sel = false;
            for (int i = 0; i < (int)entries_.size(); i++) sel |= entries_[i].path == CLEAR_LOGS && i == sel_;
            button(LX, bottom - 56, LW, 56, "Delete all roll logs", sel, C_BLOOD, [this] {
                holdmenu::open("Roll logs", {{"Delete every roll log", [this] { confirm_ = true; clearLogs(); }, C_BLOOD, true}});
            });
        }
        if (entries_.empty()) {
            gfx.setFont(m.small);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString(storage::ok() ? "Nothing saved yet." : "No SD card.", LX + LW / 2, ly + 20);
        }

        // the reader
        drawPanel(RX, top, RW, bottom - top, C_BG2, C_EDGE);
        if (openIdx_ < 0) {
            gfx.setFont(m.body);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            const int cy = top + (bottom - top) / 2;
            gfx.drawString(entries_.empty() ? "Your saved things show here." : "Pick something to read.", RX + RW / 2, cy - 40);
            gfx.setFont(m.small);
            gfx.drawString("Saved dungeons, roll logs, and your own pages:", RX + RW / 2, cy + 6);
            gfx.drawString(".txt files in /gmtools/ref (# headings, - bullets).", RX + RW / 2, cy + 38);
            drawFooter("Ent open  hold for more  Esc back");
            return;
        }
        const int bw = 150;
        button(RX + RW - 16 - bw, top + 12, bw, 52, "Delete", false, C_BLOOD, [this] {
            const int i = openIdx_;
            sel_ = i;
            holdmenu::open(entries_[i].title, {{"Delete this file", [this] { confirm_ = true; deleteSelected(); }, C_BLOOD, true}});
        });
        button(RX + RW - 32 - 2 * bw, top + 12, bw, 52, "Close", false, C_ICE, [this] { openIdx_ = -1; sfx::back(); });
        gfx.setFont(m.title);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_ICE);
        gfx.drawString(fitText(title_, RW - 64 - 2 * bw), RX + 18, top + 18);
        gfx.setFont(m.small);
        gfx.setTextColor(C_DIM);
        gfx.drawString(kindOf(entries_[openIdx_].path), RX + 18, top + 58);
        gfx.drawFastHLine(RX + 12, top + 88, RW - 24, C_EDGE);
        gfx.setClipRect(RX + 4, top + 96, RW - 8, bottom - top - 104);
        page_.draw(RX + 16, top + 96, bottom - top - 104, C_ICE);
        gfx.clearClipRect();
        drawFooter("Ent open  </> page  hold for more  Esc close");
    }
};

}  // namespace

App* makeArchiveApp() { return new ArchiveApp(); }

#pragma once
#include "input.h"
#include "osk.h"
#include "sfx.h"
#include "touch.h"
#include "ui.h"
#include <Arduino.h>
#include <vector>

// Modal text prompt drawn over the current screen.
// Usage: prompt.open("HP", "", tag); then feed events while prompt.active().
// When the user presses Enter, done() returns true once with the text in value().
// Fn+</> move the cursor (Fn+Up/Dn by a line). Prompts longer than one line
// (maxLen > 30) wrap onto several lines.
class Prompt {
public:
    void open(const String& label, const String& initial = "", int tag = 0, int maxLen = 24) {
        label_ = label;   // copied: callers often pass a temporary
        text_ = initial;
        tag_ = tag;
        max_ = maxLen;
        cursor_ = text_.length();
        active_ = true;
        done_ = false;
        cancelled_ = false;
        input::setTextMode(true);
        osk::show(true);   // no keyboard plugged in: type on the screen
    }
    bool active() const { return active_; }
    int tag() const { return tag_; }
    const String& value() const { return text_; }

    // Returns true if consumed.
    bool handle(const input::Event& e) {
        if (!active_) return false;
        switch (e.key) {
            case input::K_ENTER: active_ = false; done_ = true; sfx::select(); break;
            case input::K_BACK: active_ = false; cancelled_ = true; sfx::back(); break;
            case input::K_DEL:
                if (cursor_ > 0) { text_.remove(cursor_ - 1, 1); cursor_--; }
                break;
            case input::K_LEFT: if (cursor_ > 0) cursor_--; break;
            case input::K_RIGHT: if (cursor_ < (int)text_.length()) cursor_++; break;
            case input::K_UP: cursor_ = std::max(0, cursor_ - cols()); break;
            case input::K_DOWN: cursor_ = std::min((int)text_.length(), cursor_ + cols()); break;
            case input::K_CHAR:
                if ((int)text_.length() < max_) {
                    text_ = text_.substring(0, cursor_) + e.ch + text_.substring(cursor_);
                    cursor_++;
                }
                break;
            default: break;
        }
        if (!active_) {
            input::setTextMode(restoreText_);
            osk::show(false);
        }
        return true;
    }
    // True exactly once after Enter.
    bool done() { bool d = done_; done_ = false; return d; }
    bool cancelled() { bool c = cancelled_; cancelled_ = false; return c; }
    void restoreTextModeTo(bool t) { restoreText_ = t; }
    // Close without an answer (e.g. what it was asking about went away).
    void cancel() {
        if (!active_) return;
        active_ = done_ = cancelled_ = false;
        input::setTextMode(restoreText_);
        osk::show(false);
    }

    void draw(const char* hint = nullptr) {
        if (!active_) return;
        using namespace ui;
        const Metrics& m = metrics();
        const int COLS = cols(), lh = m.line;
        // wrap into lines of at most COLS characters, breaking at spaces
        std::vector<int> starts{0};
        if (max_ > COLS) {
            int s = 0, n = text_.length();
            while (n - s > COLS) {
                int brk = text_.lastIndexOf(' ', s + COLS);
                brk = brk > s ? brk + 1 : s + COLS;
                starts.push_back(brk);
                s = brk;
            }
        }
        int row = 0;
        for (size_t i = 0; i < starts.size(); i++) if (cursor_ >= starts[i]) row = i;
        const int maxRows = 6;
        int rows = std::min((int)starts.size(), maxRows);
        int first = std::max(0, std::min(row - rows + 1, (int)starts.size() - rows));
        const int w = boxW(), boxH = u(4) + rows * lh, h = u(22) + boxH + (hint ? lh : 0), x = (m.w - w) / 2;
        // centred in what the on-screen keyboard leaves
        const int y = std::max(m.headerH + u(2), (m.h - osk::height() - h) / 2 - u(10));
        gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(5), rgb(4, 4, 8));
        drawPanel(x, y, w, h, C_BG2, C_GOLD);
        touch::modal(x, y, w, h);   // a tap outside cancels
        gfx.setFont(m.body);
        gfx.setTextDatum(top_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(fitText(label_, w - u(12)), x + u(6), y + u(5));
        if (max_ > COLS) {
            gfx.setTextDatum(top_right);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(text_.length()) + "/" + max_, x + w - u(6), y + u(5));
            gfx.setTextDatum(top_left);
        }
        gfx.fillRect(x + u(6), y + u(16), w - u(12), boxH, C_BG);
        gfx.setTextColor(C_TEXT);
        for (int r = first; r < first + rows; r++) {
            int a = starts[r], b = r + 1 < (int)starts.size() ? starts[r + 1] : text_.length();
            gfx.drawString(text_.substring(a, b), x + u(9), y + u(18) + (r - first) * lh);
        }
        if ((millis() / 450) % 2) {
            int cx = x + u(9) + (cursor_ - starts[row]) * m.charW, cy = y + u(18) + (row - first) * lh;
            gfx.fillRect(cx, cy - u(1), cursor_ == (int)text_.length() ? m.charW - 1 : u(1), lh - 1, C_GOLD);
        }
        if (hint) { gfx.setTextColor(C_DIM); gfx.drawString(hint, x + u(6), y + u(20) + boxH); }
    }

private:
    // the box is 200 px on the classic layout: 30 characters per line
    static int boxW() { return ui::wide() ? ui::metrics().w * 2 / 3 : ui::metrics().w * 5 / 6; }
    static int cols() { return (boxW() - ui::u(20)) / ui::metrics().charW; }
    String label_;
    String text_;
    int tag_ = 0, max_ = 24, cursor_ = 0;
    bool active_ = false, done_ = false, cancelled_ = false, restoreText_ = false;
};

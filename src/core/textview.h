#pragma once
#include "ui.h"
#include <vector>

// Scrollable block of wrapped, styled lines (Font0).
//
// Text added with add() may contain SRD tables: runs of lines with cells
// separated by " | ", the first line being the heading. They are drawn as
// aligned columns when they fit, with the last column wrapped when only it is
// too wide, and otherwise as one block per row ("heading: value" lines).
// Lines starting with "* " are drawn as bullets, "# " lines as headings.
class TextView {
public:
    enum Style : uint8_t { BODY, HEAD, DIM, ACCENT, GOOD, BAD, BULLET, BULLET_CONT, TH, TD, TD_ALT, TD_KEY };
    void clear() { lines_.clear(); pos_ = 0; }
    // Width to wrap to when add() isn't given one, and to draw table stripes
    // and the scroll bar within (0: the screen width).
    void setWidth(int w) { width_ = w; }
    // Wraps `text` (may contain newlines and tables) to `width` px and appends it.
    // width 0: the full screen width minus margins
    void add(const String& text, Style st = BODY, int width = 0);
    void gap() { lines_.push_back({BODY, ""}); }
    // Keys scroll by lines; on the touch screen a finger scrolls it by the
    // pixel (it registers itself as a touch::scroller when drawn).
    void scroll(int d) { pos_ = std::max(0.0f, std::min((float)(top() + d) * lh_, maxPos())); }
    void page(int d) { scroll(d * (rows_ - 1)); }
    int top() const { return std::max(0, (int)((pos_ + lh_ / 2) / lh_)); }
    bool atEnd() const { return pos_ >= maxPos() - 1; }
    void toEnd() { pos_ = maxPos(); }   // uses the height from the last draw()
    void draw(int x, int y, int h, uint16_t accent);
    int rows() const { return rows_; }
private:
    void addText(const String& text, Style st, int width);
    void addTable(const std::vector<std::vector<String>>& rows, int width);
    std::vector<std::pair<Style, String>> lines_;
    float pos_ = 0;                          // scrolled this many px
    int rows_ = 8, width_ = 0, lh_ = 11, viewH_ = 88;
    float maxPos() const { return std::max(0, (int)lines_.size() * lh_ - viewH_); }
};

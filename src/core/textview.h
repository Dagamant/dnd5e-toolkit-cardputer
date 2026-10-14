#pragma once
#include "ui.h"
#include <vector>

// Scrollable block of wrapped, styled lines (Font0).
//
// Text added with add() may contain SRD tables: runs of lines with cells
// separated by " | ", the first line being the heading. They are drawn as
// aligned columns when they fit, with the last column wrapped when only it is
// too wide, and otherwise as one block per row ("heading: value" lines).
// Lines starting with "* " are drawn as bullets.
class TextView {
public:
    enum Style : uint8_t { BODY, HEAD, DIM, ACCENT, GOOD, BAD, BULLET, BULLET_CONT, TH, TD, TD_ALT, TD_KEY };
    void clear() { lines_.clear(); top_ = 0; }
    // Wraps `text` (may contain newlines and tables) to `width` px and appends it.
    void add(const String& text, Style st = BODY, int width = ui::W - 14);
    void gap() { lines_.push_back({BODY, ""}); }
    void scroll(int d) { top_ = std::max(0, std::min(top_ + d, maxTop())); }
    void page(int d) { scroll(d * (rows_ - 1)); }
    void draw(int x, int y, int h, uint16_t accent);
    int rows() const { return rows_; }
private:
    void addText(const String& text, Style st, int width);
    void addTable(const std::vector<std::vector<String>>& rows, int width);
    std::vector<std::pair<Style, String>> lines_;
    int top_ = 0, rows_ = 8;
    int maxTop() const { return std::max(0, (int)lines_.size() - rows_); }
};

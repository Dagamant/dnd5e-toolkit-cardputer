#pragma once
#include "ui.h"
#include <vector>

// Scrollable block of wrapped, styled lines (Font0).
class TextView {
public:
    enum Style : uint8_t { BODY, HEAD, DIM, ACCENT, GOOD, BAD, BULLET };
    void clear() { lines_.clear(); top_ = 0; }
    // Wraps `text` (may contain newlines) to `width` px and appends it.
    void add(const String& text, Style st = BODY, int width = ui::W - 14);
    void gap() { lines_.push_back({BODY, ""}); }
    void scroll(int d) { top_ = std::max(0, std::min(top_ + d, maxTop())); }
    void page(int d) { scroll(d * (rows_ - 1)); }
    void draw(int x, int y, int h, uint16_t accent);
    int rows() const { return rows_; }
private:
    std::vector<std::pair<Style, String>> lines_;
    int top_ = 0, rows_ = 8;
    int maxTop() const { return std::max(0, (int)lines_.size() - rows_); }
};

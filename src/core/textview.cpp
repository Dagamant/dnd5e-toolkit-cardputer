#include "textview.h"

using namespace ui;

void TextView::add(const String& text, Style st, int width) {
    gfx.setFont(metrics().body);
    std::vector<String> w;
    int indent = st == BULLET ? 8 : 0;
    wrapLines(text, width - indent, w);
    for (size_t i = 0; i < w.size(); i++) lines_.push_back({(st == BULLET && i) ? (Style)(BULLET + 1) : st, w[i]});
}

void TextView::draw(int x, int y, int h, uint16_t accent) {
    const int lh = metrics().line;
    rows_ = h / lh;
    gfx.setFont(metrics().body);
    gfx.setTextDatum(top_left);
    for (int i = top_; i < (int)lines_.size() && i < top_ + rows_; i++) {
        auto& l = lines_[i];
        int ly = y + (i - top_) * lh;
        int lx = x;
        switch (l.first) {
            case HEAD: gfx.setTextColor(accent); break;
            case DIM: gfx.setTextColor(C_DIM); break;
            case ACCENT: gfx.setTextColor(C_GOLD); break;
            case GOOD: gfx.setTextColor(C_EMERALD); break;
            case BAD: gfx.setTextColor(C_BLOOD); break;
            case BULLET: gfx.fillCircle(x + 3, ly + 4, 1, accent); lx += 8; gfx.setTextColor(C_TEXT); break;
            default:
                if (l.first == BULLET + 1) lx += 8;
                gfx.setTextColor(C_TEXT);
        }
        gfx.drawString(l.second, lx, ly);
    }
    if ((int)lines_.size() > rows_) {
        int areaH = rows_ * lh;
        int sh = std::max(6, areaH * rows_ / (int)lines_.size());
        int sy = y + (areaH - sh) * top_ / std::max(1, maxTop());
        gfx.fillRect(metrics().w - 3, sy, 2, sh, accent);
    }
}

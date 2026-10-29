#include "textview.h"
#include "touch.h"

using namespace ui;

static bool isTableLine(const String& l) { return l.indexOf(" | ") >= 0 || l.endsWith(" |"); }

static std::vector<String> cellsOf(const String& l) {
    std::vector<String> out;
    int start = 0;
    for (;;) {
        int bar = l.indexOf(" | ", start);
        String c = bar < 0 ? l.substring(start) : l.substring(start, bar);
        c.trim();
        if (bar < 0 && c.endsWith(" |")) c.remove(c.length() - 2);
        if (c == "|") c = "";
        out.push_back(c);
        if (bar < 0) break;
        start = bar + 3;
    }
    return out;
}

void TextView::add(const String& text, Style st, int width) {
    if (width <= 0) width = width_ ? width_ : metrics().w - u(14);
    // split into plain text and table runs
    String plain;
    std::vector<std::vector<String>> table;
    int start = 0, n = text.length();
    auto flushPlain = [&]() { if (plain.length()) { addText(plain, st, width); plain = ""; } };
    auto flushTable = [&]() { if (!table.empty()) { addTable(table, width); table.clear(); } };
    while (start <= n) {
        int nl = text.indexOf('\n', start);
        String l = nl < 0 ? text.substring(start) : text.substring(start, nl);
        if (isTableLine(l)) {
            if (table.empty()) {
                // a short line without a full stop right above is the table's title
                int cut = plain.lastIndexOf('\n');
                String last = plain.substring(cut + 1);
                if (last.length() && last.length() < 40 && !last.endsWith(".") && !last.endsWith(":")) {
                    plain = cut < 0 ? String("") : plain.substring(0, cut);
                    flushPlain();
                    lines_.push_back({BODY, ""});
                    lines_.push_back({TD_KEY, last});
                }
            }
            flushPlain();
            table.push_back(cellsOf(l));
        } else {
            flushTable();
            if (l.startsWith("* ")) { flushPlain(); addText(l.substring(2), BULLET, width); }
            else if (l.startsWith("# ")) {   // heading (Foundry journals and sheets)
                flushPlain();
                if (!lines_.empty() && lines_.back().second.length()) lines_.push_back({BODY, ""});
                addText(l.substring(2), HEAD, width);
            }
            else plain += (plain.length() ? "\n" : "") + l;
        }
        if (nl < 0) break;
        start = nl + 1;
    }
    flushTable();
    flushPlain();
}

void TextView::addText(const String& text, Style st, int width) {
    gfx.setFont(metrics().body);
    std::vector<String> w;
    int indent = st == BULLET ? u(8) : 0;
    wrapLines(text, width - indent, w);
    for (size_t i = 0; i < w.size(); i++) lines_.push_back({(st == BULLET && i) ? BULLET_CONT : st, w[i]});
}

// Pads or cuts s to exactly n characters (Font0 is monospaced).
static String fixed(const String& s, int n) {
    String o = s.substring(0, n);
    while ((int)o.length() < n) o += ' ';
    return o;
}

void TextView::addTable(const std::vector<std::vector<String>>& in, int width) {
    const int cw = metrics().charW, chars = width / cw, GAP = 2;
    size_t ncol = 0;
    for (auto& r : in) ncol = std::max(ncol, r.size());
    std::vector<std::vector<String>> rows = in;
    for (auto& r : rows) r.resize(ncol);
    std::vector<int> w(ncol, 0);
    for (auto& r : rows)
        for (size_t c = 0; c < ncol; c++) w[c] = std::max(w[c], (int)r[c].length());
    int total = GAP * (ncol - 1);
    for (int x : w) total += x;
    auto sectionRow = [&](const std::vector<String>& r) {   // "Simple Melee Weapons" with empty cells
        if (!r[0].length()) return false;
        for (size_t c = 1; c < ncol; c++) if (r[c].length()) return false;
        return ncol > 1;
    };
    if (lines_.empty() || lines_.back().first != TD_KEY) lines_.push_back({BODY, ""});

    // 1. everything fits: aligned columns
    // 2. it fits if the widest column wraps (keeping at least 10 characters)
    if (total > chars) {
        size_t wide = 0;
        for (size_t c = 1; c < ncol; c++) if (w[c] > w[wide]) wide = c;
        int room = chars - (total - w[wide]);
        if (room >= 10) { w[wide] = room; total = chars; }
    }
    if (total <= chars) {
        gfx.setFont(metrics().body);
        for (size_t i = 0; i < rows.size(); i++) {
            const auto& r = rows[i];
            Style st = i == 0 ? TH : (i % 2 ? TD : TD_ALT);
            if (i && sectionRow(r)) { lines_.push_back({TD_KEY, r[0]}); continue; }
            std::vector<std::vector<String>> cell(ncol);
            size_t height = 1;
            for (size_t c = 0; c < ncol; c++) {
                wrapLines(r[c], w[c] * cw, cell[c]);
                height = std::max(height, cell[c].size());
            }
            for (size_t k = 0; k < height; k++) {
                String line;
                for (size_t c = 0; c < ncol; c++) {
                    String part = k < cell[c].size() ? cell[c][k] : String("");
                    line += c + 1 < ncol ? fixed(part, w[c]) + "  " : part;
                }
                while (line.endsWith(" ")) line.remove(line.length() - 1);   // keep the left padding
                lines_.push_back({st, line.length() ? line : String(" ")});
            }
        }
        lines_.push_back({BODY, ""});
        return;
    }

    // 3. too wide: one block per row, "heading: value"
    const auto& head = rows[0];
    for (size_t i = 1; i < rows.size(); i++) {
        const auto& r = rows[i];
        std::vector<String> key;
        gfx.setFont(metrics().body);
        wrapLines(r[0], width, key);
        for (auto& l : key) lines_.push_back({TD_KEY, l});
        if (sectionRow(r)) continue;
        for (size_t c = 1; c < ncol; c++) {
            if (!r[c].length()) continue;
            String t = head[c].length() ? head[c] + ": " + r[c] : r[c];
            std::vector<String> wrapped;
            gfx.setFont(metrics().body);
            wrapLines(t, width - 2 * cw, wrapped);
            for (auto& l : wrapped) lines_.push_back({TD, "  " + l});
        }
    }
    lines_.push_back({BODY, ""});
}

void TextView::draw(int x, int y, int h, uint16_t accent) {
    const int lh = metrics().line;
    const int right = width_ ? x + width_ + u(5) : metrics().w;   // stripes and scroll bar end here
    lh_ = lh;
    viewH_ = h;
    rows_ = h / lh;
    // a finger may hold it past an end; otherwise keep it inside
    if (!touch::scrolling()) pos_ = std::max(0.0f, std::min(pos_, maxPos()));
    touch::scroller(x - u(4), y, right - x + u(4), h, &pos_, maxPos());
    int32_t cx, cy, cw, ch;
    gfx.getClipRect(&cx, &cy, &cw, &ch);
    gfx.setClipRect(x - u(3), y, right - x + u(3), h);
    gfx.setFont(metrics().body);
    gfx.setTextDatum(top_left);
    const int p = (int)pos_, first = std::max(0, p / lh);
    for (int i = first; i < (int)lines_.size(); i++) {
        int ly = y + i * lh - p;
        if (ly >= y + h) break;
        auto& l = lines_[i];
        int lx = x;
        switch (l.first) {
            case HEAD: gfx.setTextColor(accent); if (wide()) gfx.setFont(metrics().bold); break;
            case DIM: gfx.setTextColor(C_DIM); break;
            case ACCENT: gfx.setTextColor(C_GOLD); break;
            case GOOD: gfx.setTextColor(C_EMERALD); break;
            case BAD: gfx.setTextColor(C_BLOOD); break;
            case BULLET: gfx.fillCircle(x + u(3), ly + lh * 2 / 5, u(1), accent); lx += u(8); gfx.setTextColor(C_TEXT); break;
            case BULLET_CONT: lx += u(8); gfx.setTextColor(C_TEXT); break;
            case TH:
                gfx.fillRect(x - u(2), ly - u(1), right - x - u(2), lh, C_PANEL);
                gfx.setTextColor(C_GOLD);
                break;
            case TD_ALT:
                gfx.fillRect(x - u(2), ly - u(1), right - x - u(2), lh, C_BG2);
                gfx.setTextColor(C_TEXT);
                break;
            case TD_KEY: gfx.setTextColor(accent); break;
            default: gfx.setTextColor(C_TEXT);
        }
        gfx.drawString(l.second, lx, ly);
        if (l.first == HEAD && wide()) gfx.setFont(metrics().body);
    }
    gfx.setClipRect(cx, cy, cw, ch);
    if (maxPos() > 0) {
        int sh = std::max(u(6), h * h / ((int)lines_.size() * lh));
        float f = std::max(0.0f, std::min(1.0f, pos_ / maxPos()));
        gfx.fillRect(right - u(3), y + (int)((h - sh) * f), u(2), sh, accent);
    }
}

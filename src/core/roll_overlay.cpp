#include "roll_overlay.h"
#include "dice_expr.h"
#include "dice_tray.h"
#include "die_shapes.h"
#include "../board/board.h"
#include "rng.h"
#include "settings.h"
#include "sfx.h"
#include "storage.h"
#include "touch.h"
#include "ui.h"
#include "web.h"

using namespace ui;

namespace rollui {

struct Shown {
    Line line;
    dice::Result res;
    bool crit = false;
};

static bool s_active = false;
static String s_title;
static std::vector<Line> s_lines;
static std::vector<Shown> s_res;
static uint32_t s_anim = 0;
static constexpr uint32_t ANIM_MS = 450;
static bool s_locked = false;
// big screen: the dice tumble in a tray above the lines
static dicetray::Tray s_tray;
static bool s_framed = false;    // the box is on screen over the app's last frame
static bool s_landed = false;    // the landed dice have been drawn

// Big-screen box: a title, the tray, one row per line, a hint.
static constexpr int BOX_W = 900, TITLE_H = 60, HINT_H = 44;
struct Box { int x, y, w, h, trayH, rowH; };
static Box box() {
    const Metrics& m = metrics();
    const int n = std::max<int>(1, s_res.size());
    Box b;
    b.w = BOX_W;
    b.rowH = std::min(74, (m.h - 40 - TITLE_H - HINT_H - 180) / n);
    b.trayH = std::max(180, std::min(300, m.h - 40 - TITLE_H - HINT_H - n * b.rowH));
    b.h = TITLE_H + b.trayH + n * b.rowH + HINT_H;
    b.x = (m.w - b.w) / 2;
    b.y = std::max(10, (m.h - b.h) / 2);
    return b;
}

String doubleDice(const String& expr) {
    String out;
    int i = 0, n = expr.length();
    while (i < n) {
        // find a dice term: [count]d<sides>
        int j = i;
        while (j < n && isdigit((unsigned char)expr[j])) j++;
        if (j < n && (expr[j] == 'd' || expr[j] == 'D') && j + 1 < n && isdigit((unsigned char)expr[j + 1])) {
            int count = j > i ? expr.substring(i, j).toInt() : 1;
            out += String(count * 2);
            i = j;
            // copy the 'd' and sides
            out += expr[i++];
            while (i < n && isdigit((unsigned char)expr[i])) out += expr[i++];
            continue;
        }
        if (j > i) { out += expr.substring(i, j); i = j; continue; }
        out += expr[i++];
    }
    return out;
}

static void rollAll() {
    s_res.clear();
    bool crit = false;
    for (size_t k = 0; k < s_lines.size(); k++) {
        Shown sh;
        sh.line = s_lines[k];
        String e = sh.line.expr;
        if (k > 0 && crit) { e = doubleDice(e); sh.crit = true; }
        sh.res = dice::roll(e);
        if (k == 0 && sh.res.ok && sh.res.natural == 20) crit = true;
        s_res.push_back(sh);
        if (sh.res.ok)
            web::logRoll(s_title, sh.line.label, sh.res.expr, sh.res.total, sh.res.breakdown(), sh.crit, sh.res.natural);
        if (settings::cur.logRolls && storage::ok() && sh.res.ok) {
            String l = String(millis() / 1000) + "s  " + s_title + (sh.line.label.length() ? " " + sh.line.label : String("")) +
                       ": " + sh.res.expr + " = " + sh.res.total + "  " + sh.res.breakdown();
            storage::appendLine(storage::sessionLogPath().c_str(), l);
        }
    }
    s_anim = 0;
    s_landed = false;
    if (wide()) {
        std::vector<const dice::Result*> rs;
        for (auto& sh : s_res) rs.push_back(&sh.res);
        Box b = box();
        s_tray.start(rs, b.x + 24, b.y + TITLE_H + 6, b.w - 48, b.trayH - 12, 90);
    } else {
        s_tray.clear();
    }
    sfx::diceRattle();
}

int total(size_t i) { return i < s_res.size() && s_res[i].res.ok ? s_res[i].res.total : 0; }
void lockResult() { s_locked = true; }

void show(const String& title, const std::vector<Line>& lines) {
    s_locked = false;
    s_title = title;
    s_lines = lines;
    s_active = true;
    s_framed = false;
    rollAll();
}

bool active() { return s_active; }

static bool settling() { return wide() ? s_tray.rolling() : s_anim < ANIM_MS; }

bool animating() { return s_active && (!wide() || settling() || !s_landed); }

bool drawsAlone() { return s_active && s_framed && wide(); }

bool handle(const input::Event& e) {
    if (!s_active) return false;
    if (!s_locked && (e.key == input::K_ENTER || (e.key == input::K_CHAR && e.base == ' '))) {
        s_framed = false;   // the box's colours may change: draw it whole
        rollAll();
    }
    else { s_active = false; sfx::back(); }
    return true;
}

void tick(uint32_t dt) {
    if (!s_active) return;
    const bool was = settling();
    s_anim += dt;
    s_tray.step(dt);
    if (was && !settling() && !s_res.empty()) {
        const auto& r0 = s_res[0].res;
        if (r0.natural == 20) sfx::crit();
        else if (r0.natural == 1) sfx::fumble();
        else sfx::land();
    }
}

// The big screen's popup. While the dice tumble only the tray and the rows
// change: those frames redraw and show just that part of the box.
static void drawWide() {
    const Metrics& m = metrics();
    const Box b = box();
    const int n = s_res.size();
    const bool crit = n && s_res[0].res.natural == 20, fumble = n && s_res[0].res.natural == 1;
    const bool rolling = settling();
    const uint16_t edge = !rolling && crit ? C_GOLD : !rolling && fumble ? C_BLOOD : C_ARCANE;
    const int iy = b.y + TITLE_H - 4, ih = b.trayH + n * b.rowH + 8;   // the part that changes
    const bool partial = s_framed && rolling;
    if (partial) {
        if (!board::fill(gfx, b.x + 8, iy, b.w - 16, ih, C_BG2)) gfx.fillRect(b.x + 8, iy, b.w - 16, ih, C_BG2);
    } else {
        gfx.fillRoundRect(b.x + 8, b.y + 8, b.w, b.h, 18, rgb(3, 6, 14));
        drawPanel(b.x, b.y, b.w, b.h, C_BG2, edge);
        if (!rolling && crit) gfx.drawRoundRect(b.x - 5, b.y - 5, b.w + 10, b.h + 10, 20, C_GOLD_DK);
        gfx.setFont(m.title);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(edge);
        gfx.drawString(fitText(s_title, b.w - 48), b.x + 24, b.y + TITLE_H / 2 + 2);
        gfx.setFont(m.small);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(s_locked ? "tap to close" : "tap: roll again     outside: close", b.x + b.w / 2, b.y + b.h - HINT_H / 2);
    }
    fillRound(b.x + 16, b.y + TITLE_H, b.w - 32, b.trayH, 14, C_BG);
    s_tray.draw();
    if (!n) {
        gfx.setFont(m.body);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString("Nothing to roll here.", b.x + b.w / 2, b.y + TITLE_H + b.trayH / 2);
    }

    for (int k = 0; k < n; k++) {
        const Shown& sh = s_res[k];
        const int ry = b.y + TITLE_H + b.trayH + 4 + k * b.rowH, mid = ry + b.rowH / 2;
        const uint16_t tc = dicetray::tint(k);
        gfx.fillRoundRect(b.x + 24, ry + 10, 8, b.rowH - 20, 4, tc);
        String lab = sh.line.label.length() ? sh.line.label : String("Roll");
        if (sh.crit) lab += "  (crit: dice doubled)";
        gfx.setFont(m.bold);
        gfx.setTextDatum(bottom_left);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(fitText(lab, b.w - 300), b.x + 46, mid + 2);
        if (!rolling && k == 0 && (crit || fumble)) {
            gfx.setTextColor(crit ? C_GOLD : C_BLOOD);
            gfx.drawString(crit ? "  NATURAL 20!" : "  NATURAL 1", b.x + 46 + gfx.textWidth(lab), mid + 2);
        }
        gfx.setFont(m.small);
        gfx.setTextDatum(top_left);
        if (!sh.res.ok) {
            gfx.setTextColor(C_BLOOD);
            gfx.drawString(String("bad roll: ") + sh.line.expr, b.x + 46, mid + 6);
            continue;
        }
        gfx.setTextColor(C_DIM);
        String bd = rolling ? sh.res.expr : sh.res.expr + "  " + sh.res.breakdown();
        gfx.drawString(fitText(bd, b.w - 330), b.x + 46, mid + 6);
        // the total: counting up with the dice, then coloured
        const int total = rolling ? s_tray.liveTotal(k, sh.res) : sh.res.total;
        uint16_t c = rolling ? C_DIM : tc == C_GOLD ? C_GOLD : C_TEXT;
        if (!rolling && k == 0 && crit) c = C_GOLD;
        if (!rolling && k == 0 && fumble) c = C_BLOOD;
        gfx.setFont(font(&fonts::FreeSerifBold18pt7b));
        gfx.setTextDatum(middle_right);
        gfx.setTextColor(c);
        gfx.drawString(String(total), b.x + b.w - 28, mid);
        if (sh.line.suffix.length() && !rolling) {
            const int tw = gfx.textWidth(String(total));
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(fitText(sh.line.suffix, 180), b.x + b.w - 44 - tw, mid);
        }
    }

    // touch: the box rolls again, anywhere else closes
    touch::key(0, 0, m.w, m.h, input::K_BACK);
    touch::key(b.x, b.y, b.w, b.h, input::K_ENTER);
    if (partial) presentOnly(b.x + 8, iy, b.w - 16, ih);
    s_framed = true;
    if (!rolling) s_landed = true;
}

void draw() {
    const Metrics& m = metrics();
    if (!s_active) return;
    if (wide()) { drawWide(); return; }
    const int w = u(210), lineH = u(26);
    const int h = u(26) + (int)s_res.size() * lineH + u(12);
    const int x = (m.w - w) / 2, y = std::max(m.headerH + u(2), (m.h - h) / 2);
    bool crit = !s_res.empty() && s_res[0].res.natural == 20;
    bool fumble = !s_res.empty() && s_res[0].res.natural == 1;
    uint16_t edge = crit ? C_GOLD : fumble ? C_BLOOD : C_ARCANE;
    gfx.fillRoundRect(x + u(3), y + u(3), w, h, u(6), rgb(4, 4, 8));
    drawPanel(x, y, w, h, C_BG2, edge);
    if (crit) gfx.drawRoundRect(x - u(2), y - u(2), w + u(4), h + u(4), u(8), C_GOLD_DK);

    gfx.setTextDatum(top_left);
    gfx.setFont(m.title);
    gfx.setTextColor(edge);
    String t = s_title;
    while (t.length() > 3 && gfx.textWidth(t) > w - u(14)) t = t.substring(0, t.length() - 2);
    gfx.drawString(t, x + u(7), y + u(4));

    bool rolling = s_anim < ANIM_MS;
    for (size_t k = 0; k < s_res.size(); k++) {
        const Shown& sh = s_res[k];
        int ly = y + u(24) + (int)k * lineH;
        gfx.setFont(m.body);
        gfx.setTextColor(C_DIM);
        gfx.setTextDatum(top_left);
        String lab = sh.line.label.length() ? sh.line.label : String("Roll");
        if (sh.crit) lab += " (crit)";
        gfx.drawString(lab, x + u(8), ly);
        if (!sh.res.ok) {
            gfx.setTextColor(C_BLOOD);
            gfx.drawString(String("bad roll: ") + sh.line.expr, x + u(8), ly + u(10));
            continue;
        }
        // breakdown under the label
        String bd = sh.res.expr + " " + sh.res.breakdown();
        while (bd.length() > 3 && gfx.textWidth(bd) > w - u(70)) bd = bd.substring(0, bd.length() - 4) + "..";
        gfx.drawString(bd, x + u(8), ly + u(11));
        // big total on the right
        int total = rolling ? rng::roll(std::max(2, sh.res.maxTotal)) : sh.res.total;
        uint16_t c = C_TEXT;
        if (!rolling && k == 0 && sh.res.natural == 20) c = C_GOLD;
        if (!rolling && k == 0 && sh.res.natural == 1) c = C_BLOOD;
        gfx.setFont(m.big);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(c);
        gfx.drawString(String(total), x + w - u(8), ly - u(1));
        if (sh.line.suffix.length() && !rolling) {
            gfx.setFont(m.body);
            gfx.setTextColor(C_DIM);
            gfx.drawString(sh.line.suffix.substring(0, 10), x + w - u(8), ly + u(18));
        }
    }
    gfx.setFont(m.body);
    gfx.setTextDatum(bottom_center);
    gfx.setTextColor(C_DIM);
    if (wide()) {
        // touch: the box rolls again, anywhere else closes
        gfx.setFont(m.small);
        gfx.drawString(s_locked ? "tap to close" : "tap: roll again   outside: close", m.w / 2, y + h - u(2));
        touch::key(0, 0, m.w, m.h, input::K_BACK);
        touch::key(x, y, w, h, input::K_ENTER);
        return;
    }
    gfx.drawString(s_locked ? "any key close" : "Ent reroll  any key close", m.w / 2, y + h - u(2));
}

}  // namespace rollui

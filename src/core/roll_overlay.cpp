#include "roll_overlay.h"
#include "dice_expr.h"
#include "rng.h"
#include "settings.h"
#include "sfx.h"
#include "storage.h"
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
    sfx::diceRattle();
}

int total(size_t i) { return i < s_res.size() && s_res[i].res.ok ? s_res[i].res.total : 0; }
void lockResult() { s_locked = true; }

void show(const String& title, const std::vector<Line>& lines) {
    s_locked = false;
    s_title = title;
    s_lines = lines;
    s_active = true;
    rollAll();
}

bool active() { return s_active; }

bool handle(const input::Event& e) {
    if (!s_active) return false;
    if (!s_locked && (e.key == input::K_ENTER || (e.key == input::K_CHAR && e.base == ' '))) rollAll();
    else { s_active = false; sfx::back(); }
    return true;
}

void tick(uint32_t dt) {
    if (!s_active) return;
    uint32_t before = s_anim;
    s_anim += dt;
    if (before < ANIM_MS && s_anim >= ANIM_MS && !s_res.empty()) {
        const auto& r0 = s_res[0].res;
        if (r0.natural == 20) sfx::crit();
        else if (r0.natural == 1) sfx::fumble();
        else sfx::land();
    }
}

void draw() {
    const Metrics& m = metrics();
    if (!s_active) return;
    const int w = 210, lineH = 26;
    const int h = 26 + (int)s_res.size() * lineH + 12;
    const int x = (m.w - w) / 2, y = std::max(m.headerH + 2, (m.h - h) / 2);
    bool crit = !s_res.empty() && s_res[0].res.natural == 20;
    bool fumble = !s_res.empty() && s_res[0].res.natural == 1;
    uint16_t edge = crit ? C_GOLD : fumble ? C_BLOOD : C_ARCANE;
    gfx.fillRoundRect(x + 3, y + 3, w, h, 6, rgb(4, 4, 8));
    drawPanel(x, y, w, h, C_BG2, edge);
    if (crit) gfx.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 8, C_GOLD_DK);

    gfx.setTextDatum(top_left);
    gfx.setFont(m.title);
    gfx.setTextColor(edge);
    String t = s_title;
    while (t.length() > 3 && gfx.textWidth(t) > w - 14) t = t.substring(0, t.length() - 2);
    gfx.drawString(t, x + 7, y + 4);

    bool rolling = s_anim < ANIM_MS;
    for (size_t k = 0; k < s_res.size(); k++) {
        const Shown& sh = s_res[k];
        int ly = y + 24 + (int)k * lineH;
        gfx.setFont(m.body);
        gfx.setTextColor(C_DIM);
        gfx.setTextDatum(top_left);
        String lab = sh.line.label.length() ? sh.line.label : String("Roll");
        if (sh.crit) lab += " (crit)";
        gfx.drawString(lab, x + 8, ly);
        if (!sh.res.ok) {
            gfx.setTextColor(C_BLOOD);
            gfx.drawString(String("bad roll: ") + sh.line.expr, x + 8, ly + 10);
            continue;
        }
        // breakdown under the label
        String bd = sh.res.expr + " " + sh.res.breakdown();
        while (bd.length() > 3 && gfx.textWidth(bd) > w - 70) bd = bd.substring(0, bd.length() - 4) + "..";
        gfx.drawString(bd, x + 8, ly + 11);
        // big total on the right
        int total = rolling ? rng::roll(std::max(2, sh.res.maxTotal)) : sh.res.total;
        uint16_t c = C_TEXT;
        if (!rolling && k == 0 && sh.res.natural == 20) c = C_GOLD;
        if (!rolling && k == 0 && sh.res.natural == 1) c = C_BLOOD;
        gfx.setFont(m.big);
        gfx.setTextDatum(top_right);
        gfx.setTextColor(c);
        gfx.drawString(String(total), x + w - 8 - (sh.line.suffix.length() ? 0 : 0), ly - 1);
        if (sh.line.suffix.length() && !rolling) {
            gfx.setFont(m.body);
            gfx.setTextColor(C_DIM);
            gfx.drawString(sh.line.suffix.substring(0, 10), x + w - 8, ly + 18);
        }
    }
    gfx.setFont(m.body);
    gfx.setTextDatum(bottom_center);
    gfx.setTextColor(C_DIM);
    gfx.drawString(s_locked ? "any key close" : "Ent reroll  any key close", m.w / 2, y + h - 2);
}

}  // namespace rollui

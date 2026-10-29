#include "../core/app.h"
#include "../board/board.h"
#include "../core/touch.h"
#include "../core/dice_expr.h"
#include "../core/die_shapes.h"
#include "../core/dice_tray.h"
#include "../core/hold_menu.h"
#include "../core/prompt.h"
#include "../core/osk.h"
#include "../core/rng.h"
#include "../core/settings.h"
#include "../core/sfx.h"
#include "../core/storage.h"
#include "../core/ui.h"
#include "../core/web.h"
#include <math.h>

using namespace ui;

namespace {

struct Macro { String name, expr; };

struct Particle { float x, y, vx, vy; uint16_t life; uint16_t color; };

class DiceApp : public App {
public:
    const char* name() const override { return "Dice"; }
    const char* blurb() const override { return "Roll anything. Shake to roll."; }
    uint16_t accent() const override { return C_GOLD; }

    void drawIcon(int cx, int cy, float t) override {
        drawDieShape(20, cx, cy, 20, C_BLOOD_DK, C_GOLD, t * 0.6f);
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextColor(C_GOLD);
        gfx.setTextDatum(middle_center);
        gfx.drawString("20", cx, cy + 1);
    }

    void onEnter() override {
        input::setTextMode(true);
        loadMacros();
        lastShake_ = millis();
    }
    void onExit() override {
        input::setTextMode(false);
        touch::freeze(false);
    }

    bool animating() const override {
        if (rolling_) return true;
        for (auto& p : parts_) if (p.life) return true;
        return false;
    }

    bool handle(const input::Event& e) override {
        touch::freeze(false);   // anything pressed may change more than the tray
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done() && prompt_.value().length()) {
                if (prompt_.tag() == 1 && renameAt_ < (int)macros_.size()) {
                    macros_[renameAt_].name = prompt_.value();
                    macros_[renameAt_].name.replace("=", "-");
                    saveMacros();
                } else if (prompt_.tag() == 0) addMacro(prompt_.value(), newMacro_);
            }
            return true;
        }
        if (view_ == View::Macros) return handleMacros(e);
        if (view_ == View::History) return handleHistory(e);

        // Fn shortcuts (work regardless of what's typed)
        if (e.fn && e.key == input::K_CHAR) {
            static const int quick[] = {4, 6, 8, 10, 12, 20, 100};
            if (e.base >= '1' && e.base <= '7') { doRoll(String("d") + quick[e.base - '1']); return true; }
            if (e.base == 'a') { doRoll(withMod("adv")); return true; }
            if (e.base == 'd') { doRoll(withMod("dis")); return true; }
            if (e.base == 'm') { view_ = View::Macros; sel_ = 0; sfx::select(); return true; }
            if (e.base == 'h') { view_ = View::History; sel_ = 0; sfx::select(); return true; }
            if (e.base == 'c') { line_ = ""; history_.clear(); sfx::back(); return true; }
        }

        switch (e.key) {
            case input::K_BACK:
                if (line_.length()) { line_ = ""; sfx::back(); return true; }
                return false;  // leave app
            case input::K_ENTER:
                if (line_.length()) { doRoll(line_); recall_ = -1; }
                else if (wide()) doRoll(pickerExpr());
                else if (last_.length()) doRoll(last_);
                else doRoll("d20");
                return true;
            case input::K_DEL:
                if (line_.length()) line_.remove(line_.length() - 1);
                return true;
            case input::K_UP:
            case input::K_DOWN: {
                // shell-style recall of previous expressions
                if (history_.empty()) return true;
                int n = history_.size();
                if (e.key == input::K_UP) recall_ = recall_ < 0 ? 0 : std::min(recall_ + 1, n - 1);
                else recall_ = recall_ <= 0 ? -1 : recall_ - 1;
                line_ = recall_ < 0 ? String("") : history_[n - 1 - recall_].expr;
                sfx::click();
                return true;
            }
            case input::K_LEFT:
            case input::K_RIGHT:
                // big screen: the arrows pick the die
                if (wide() && !line_.length()) { type_ = (type_ + NTYPES + (e.key == input::K_RIGHT ? 1 : -1)) % NTYPES; sfx::click(); }
                return true;
            case input::K_CHAR:
                if (line_.length() < 28) line_ += e.ch;
                return true;
            default:
                return true;
        }
    }

    void tick(uint32_t dt) override {
        anim_ += dt;
        // shake detection
        if (settings::cur.shakeToRoll && view_ == View::Main && M5.Imu.isEnabled()) {
            M5.Imu.update();
            float ax, ay, az;
            if (M5.Imu.getAccel(&ax, &ay, &az)) {
                float g = sqrtf(ax * ax + ay * ay + az * az);
                uint32_t now = millis();
                if (fabsf(g - 1.0f) > 1.1f && now - lastShake_ > 900) {
                    lastShake_ = now;
                    doRoll(line_.length() ? line_ : wide() ? pickerExpr() : (last_.length() ? last_ : String("d20")));
                }
            }
        }
        if (rolling_ && wide()) tray_.step(dt);
        // settle
        if (rolling_ && (wide() ? !tray_.rolling() : anim_ >= ROLL_MS)) {
            rolling_ = false;
            onSettle();
        }
        // particles
        for (auto& p : parts_) {
            if (!p.life) continue;
            p.x += p.vx * dt / 16.0f;
            p.y += p.vy * dt / 16.0f;
            p.vy += 0.08f * dt / 16.0f;
            p.life = p.life > dt ? p.life - dt : 0;
        }
        if (shakeAmt_ > 0) shakeAmt_ = std::max(0.0f, shakeAmt_ - dt * 0.02f);
    }

    bool bigLayout() const override { return true; }

    void draw() override {
        // Big screen, dice tumbling, nothing pressed since the last full frame:
        // only the tray changes, so draw and show just that.
        if (wide() && view_ == View::Main && rolling_ && touch::frozen() && !osk::visible() && !prompt_.active() &&
            !holdmenu::active()) {
            const int y = HEADER_H + 10, h = H - FOOTER_H - 10 - y;
            gfx.setClipRect(TRAY_X, y, TRAY_W, h);
            drawTray();
            gfx.clearClipRect();
            presentOnly(TRAY_X, y, TRAY_W, h);
            return;
        }
        touch::freeze(false);
        touch::clear();
        drawBackground();
        drawHeader("Dice", C_GOLD);

        if (view_ == View::Macros) { drawMacros(); prompt_.draw(); return; }
        if (view_ == View::History) { drawHistory(); prompt_.draw(); return; }

        if (wide()) {
            drawRoller();
            prompt_.draw("Saved rolls are in /gmtools/macros.txt");
            if (rolling_ && !prompt_.active()) touch::freeze(true);   // keep these targets for the tray-only frames
            return;
        }
        drawStage();
        drawInputLine();
        drawFooter("Ent roll  Fn1-7 d4..d100  FnA/D adv/dis  FnM macros");
    }

private:
    enum class View { Main, Macros, History };
    static constexpr uint32_t ROLL_MS = 750;

    View view_ = View::Main;
    Prompt prompt_;
    String newMacro_;   // the roll being saved, while its name is asked for
    int renameAt_ = 0;
    String line_, last_;
    int recall_ = -1;
    std::vector<dice::Result> history_;
    std::vector<Macro> macros_;
    dice::Result cur_;
    bool rolling_ = false, haveResult_ = false;
    uint32_t anim_ = 0, lastShake_ = 0;
    int sel_ = 0;
    int shownSides_ = 20;
    float shakeAmt_ = 0;
    Particle parts_[28] = {};

    String withMod(const char* base) {
        // Fn+A with "+5" typed gives "adv+5"
        String l = line_;
        l.trim();
        if (l.length() && (l[0] == '+' || l[0] == '-')) return String(base) + l;
        return String(base);
    }

    void doRoll(const String& expr) {
        dice::Result r = dice::roll(expr);
        if (!r.ok) {
            sfx::error();
            toast((String("Bad roll: ") + r.error).c_str(), C_BLOOD);
            return;
        }
        cur_ = r;
        last_ = expr;
        line_ = "";
        recall_ = -1;
        rolling_ = true;
        haveResult_ = true;
        anim_ = 0;
        shownSides_ = r.largestDie() ? r.largestDie() : 20;
        if (wide()) {
            // the dice land between the formula at the top and the total
            const int top = HEADER_H + 10 + 56;
            tray_.start({&cur_}, TRAY_X + 16, top, TRAY_W - 32, TOTAL_Y - 80 - top);
        }
        sfx::diceRattle();
    }

    void onSettle() {
        history_.push_back(cur_);
        if (history_.size() > 40) history_.erase(history_.begin());
        bool crit = cur_.natural == 20 || cur_.degree == dice::Degree::CritSuccess ||
                    (cur_.natural == 0 && cur_.maxTotal > cur_.minTotal && cur_.total == cur_.maxTotal);
        bool fumble = cur_.natural == 1 || cur_.degree == dice::Degree::CritFail;
        if (crit) { burst(C_GOLD, 28); sfx::crit(); }
        else if (fumble) { burst(C_BLOOD, 16); shakeAmt_ = 6; sfx::fumble(); }
        else sfx::land();

        // shown on the web live view once the dice have landed
        web::logRoll("Dice", cur_.hasTarget ? dice::degreeName(cur_.degree) : "", cur_.expr, cur_.total, cur_.breakdown(),
                     false, cur_.natural);
        if (settings::cur.logRolls && storage::ok()) {
            String l = String(millis() / 1000) + "s  " + cur_.expr + " = " + cur_.total + "  " + cur_.breakdown();
            if (cur_.hasTarget) l += String("  ") + dice::degreeName(cur_.degree);
            storage::appendLine(storage::sessionLogPath().c_str(), l);
        }
    }

    void burst(uint16_t color, int n) {
        for (int i = 0; i < n && i < 28; i++) {
            float a = rng::unit() * 6.2832f, s = 1.0f + rng::unit() * 2.5f;
            // particles live in classic px; on the big screen they burst from the total
            parts_[i] = {wide() ? (TRAY_X + TRAY_W / 2) * 5 / 16.0f : 60.0f, wide() ? TOTAL_Y * 5 / 16.0f : 62.0f, cosf(a) * s, sinf(a) * s - 1.2f, (uint16_t)(500 + rng::below(500)),
                         (i % 3) ? color : C_TEXT};
        }
    }

    // the die, result and recent-rolls panels sit between these
    int stageTop() const { return HEADER_H + u(4); }
    int stageBottom() const { return wide() ? H - FOOTER_H - u(22) : HEADER_H + u(4) + u(84); }

    void drawStage() {
        const int cx = u(60), cy = wide() ? (stageTop() + stageBottom()) / 2 : u(62);
        float t = rolling_ ? (float)anim_ / ROLL_MS : 1.0f;
        int ox = 0, oy = 0;
        float rot = 0;
        int r = u(34);
        if (rolling_) {
            // tumble: spin decays, bounce on an exponential envelope
            float k = 1 - easeOut(t);
            rot = k * 14.0f;
            oy = -(int)(fabsf(sinf(t * 11.0f)) * u(14) * k);
            ox = (int)(sinf(t * 23.0f) * u(6) * k);
        } else if (shakeAmt_ > 0) {
            ox = (int)((rng::unit() - 0.5f) * shakeAmt_ * u(2));
        }
        // pop on settle
        if (!rolling_ && haveResult_ && anim_ < ROLL_MS + 180) {
            float p = (float)(anim_ - ROLL_MS) / 180.0f;
            r += (int)(u(6) * sinf(p * 3.1416f));
        }

        bool crit = haveResult_ && !rolling_ && (cur_.natural == 20 || cur_.degree == dice::Degree::CritSuccess);
        bool fumble = haveResult_ && !rolling_ && (cur_.natural == 1 || cur_.degree == dice::Degree::CritFail);
        uint16_t fill = crit ? C_GOLD_DK : fumble ? C_BLOOD_DK : C_PANEL;
        uint16_t edge = crit ? C_GOLD : fumble ? C_BLOOD : C_GOLD;

        // glow ring for crits
        if (crit) {
            float pulse = 0.5f + 0.5f * sinf(anim_ * 0.008f);
            for (int k = 0; k < u(1); k++) {
                gfx.drawCircle(cx, cy, r + u(6) + (int)(pulse * u(3)) + k, C_GOLD_DK);
                gfx.drawCircle(cx, cy, r + u(9) + (int)(pulse * u(3)) + k, rgb(70, 50, 20));
            }
        }
        // shadow
        gfx.fillEllipse(cx, cy + u(40), u(26) - abs(oy), u(4), rgb(8, 6, 12));
        drawDieShape(shownSides_, cx + ox, cy + oy, r, fill, edge, rot);

        // number
        char num[12];
        if (!haveResult_) strcpy(num, "?");
        else if (rolling_) snprintf(num, sizeof(num), "%d", rng::roll(std::max(2, shownSides_)));
        else snprintf(num, sizeof(num), "%d", cur_.total);
        gfx.setTextDatum(middle_center);
        gfx.setFont(ui::font(strlen(num) > 2 ? &fonts::FreeSerifBold12pt7b : &fonts::FreeSerifBold18pt7b));
        gfx.setTextColor(rgb(0, 0, 0));
        gfx.drawString(num, cx + ox + u(1), cy + oy + u(3));
        gfx.setTextColor(crit ? rgb(255, 230, 150) : fumble ? rgb(255, 160, 160) : C_TEXT);
        gfx.drawString(num, cx + ox, cy + oy + u(2));

        for (auto& p : parts_) {
            if (!p.life) continue;
            int s = p.life > 500 ? u(2) : u(1);   // particles move in classic px
            gfx.fillRect(u((int)p.x), u((int)p.y), s, s, p.color);
        }

        // right panel: details
        const int px = u(122), py = stageTop(), pw = wide() ? u(160) : W - px - 4, ph = stageBottom() - py;
        drawPanel(px, py, pw, ph, C_BG2, C_EDGE);
        gfx.setTextDatum(top_left);
        if (!haveResult_) {
            gfx.setFont(ui::font(&fonts::Font0));
            gfx.setTextColor(C_DIM);
            textWrapped("Type a roll like 4d6kh3, adv+5, 8d6 or d20+7 vs 15 and press Enter.\n\nOr shake me.",
                        px + u(5), py + u(5), pw - u(10), u(10), 8);
            return;
        }
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextColor(C_GOLD);
        String ex = cur_.expr;
        if (gfx.textWidth(ex) > pw - u(10)) { gfx.setFont(ui::font(&fonts::Font0)); }
        gfx.drawString(ex, px + u(5), py + u(4));

        if (rolling_) return;
        gfx.setFont(ui::font(&fonts::Font0));
        int y = py + u(22);
        // breakdown with dropped dice dimmed
        y = drawBreakdown(px + u(5), y, pw - u(10));

        if (cur_.natural) {
            gfx.setTextColor(cur_.natural == 20 ? C_GOLD : cur_.natural == 1 ? C_BLOOD : C_DIM);
            gfx.drawString(String("natural ") + cur_.natural, px + u(5), y + u(2));
            y += u(12);
        }
        if (cur_.hasTarget) {
            uint16_t c = cur_.degree >= dice::Degree::Success ? C_EMERALD : C_BLOOD;
            if (cur_.degree == dice::Degree::CritSuccess) c = C_GOLD;
            gfx.setTextColor(C_DIM);
            gfx.drawString(String("vs DC ") + cur_.target, px + u(5), py + ph - u(24));
            gfx.setTextColor(c);
            gfx.drawString(dice::degreeName(cur_.degree), px + u(5), py + ph - u(13));
        } else if (cur_.maxTotal > cur_.minTotal) {
            // where this roll landed in its possible range
            float f = (float)(cur_.total - cur_.minTotal) / (cur_.maxTotal - cur_.minTotal);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(cur_.minTotal), px + u(5), py + ph - u(11));
            gfx.setTextDatum(top_right);
            gfx.drawString(String(cur_.maxTotal), px + pw - u(5), py + ph - u(11));
            gfx.setTextDatum(top_left);
            drawBar(px + u(22), py + ph - u(9), pw - u(50), u(4), f, f > 0.8f ? C_GOLD : f < 0.2f ? C_BLOOD : C_ARCANE, C_PANEL);
        }
    }

    int drawBreakdown(int x, int y, int w) {
        int cx = x;
        auto put = [&](const String& s, uint16_t c, bool strike) {
            int tw = gfx.textWidth(s);
            if (cx + tw > x + w) { cx = x; y += u(10); }
            if (y > stageBottom() - u(24)) return;
            gfx.setTextColor(c);
            gfx.drawString(s, cx, y);
            if (strike) gfx.drawFastHLine(cx, y + u(3), tw, C_BLOOD);
            cx += tw;
        };
        for (size_t gi = 0; gi < cur_.groups.size(); gi++) {
            const auto& g = cur_.groups[gi];
            if (gi > 0 || g.sign < 0) put(g.sign < 0 ? "- " : "+ ", C_DIM, false);
            if (g.sides == 0) { put(String(g.constant) + " ", C_TEXT, false); continue; }
            put("[", C_DIM, false);
            for (size_t i = 0; i < g.vals.size(); i++) {
                int v = g.vals[i];
                uint16_t c = !g.kept[i] ? C_DIM : v == g.sides ? C_GOLD : v == 1 ? C_BLOOD : C_TEXT;
                put(String(v) + (i + 1 < g.vals.size() ? "," : ""), c, !g.kept[i]);
            }
            put("] ", C_DIM, false);
        }
        return y + u(12);
    }

    // ------------------------------------------------------------ the big-screen roller
    // Modelled on the RPG Watch roller: pick a die, how many and a modifier,
    // then ROLL; every die tumbles and bounces around the tray, slows down
    // and lands in a tidy layout with the total under it.
    static constexpr int NTYPES = 9;
    static constexpr int TYPES[NTYPES] = {2, 3, 4, 6, 8, 10, 12, 20, 100};
    static constexpr int D20 = 7, MAX_COUNT = 20, MOD_LIMIT = 30;
    static constexpr int PICK_X = 16, PICK_W = 580;
    static constexpr int TRAY_X = 612, TRAY_W = 480, TOTAL_Y = 540;
    int type_ = D20, count_ = 1, mod_ = 0, mode_ = 0;   // mode: 0 normal, 1 advantage, 2 disadvantage

    dicetray::Tray tray_;

    String pickerExpr() const {
        String e = TYPES[type_] == 20 && mode_ ? String(mode_ == 1 ? "adv" : "dis") : String(count_) + "d" + TYPES[type_];
        if (mod_) e += (mod_ > 0 ? "+" : "") + String(mod_);
        return e;
    }

    static String modText(int m) { return (m >= 0 ? "+" : "") + String(m); }

    void drawRoller() {
        drawPicker();
        drawTray();
        drawRecentRolls();
        drawFooter("Ent roll  </> die  FnM macros  hold Recent rolls to save");
        drawToast();
    }

    // a rounded button; on: highlighted
    void button(int x, int y, int w, int h, bool on, uint16_t edge = C_GOLD) {
        gfx.fillRoundRect(x, y, w, h, 12, on ? C_PANEL : C_BG2);
        gfx.drawRoundRect(x, y, w, h, 12, on ? edge : C_EDGE);
        if (on) gfx.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, edge);
    }

    void drawPicker() {
        const Metrics& m = metrics();
        const int x0 = PICK_X, w = PICK_W, y0 = HEADER_H + 10, g = 8, cw = (w - 4 * g) / 5, ch = 118;
        // the dice, and a tenth cell for the macros
        for (int i = 0; i <= NTYPES; i++) {
            const int cx = x0 + (i % 5) * (cw + g), cy = y0 + (i / 5) * (ch + g);
            const bool sel = i == type_;
            button(cx, cy, cw, ch, sel);
            if (i == NTYPES) {
                gfx.setFont(m.bold);
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(C_DIM);
                gfx.drawString("Macros", cx + cw / 2, cy + ch / 2);
                touch::zone(cx, cy, cw, ch, [this] { view_ = View::Macros; sel_ = 0; sfx::select(); });
                continue;
            }
            const float bob = sel ? sinf(millis() / 400.0f) * 0.12f : 0;
            dieshape::draw(TYPES[i], cx + cw / 2, cy + 48, 30, bob, 1, sel ? C_PANEL : C_BG, sel ? C_GOLD : C_DIM,
                           sel ? C_GOLD_DK : C_EDGE, sel ? 4 : 3);
            gfx.setFont(m.body);
            gfx.setTextDatum(bottom_center);
            gfx.setTextColor(sel ? C_TEXT : C_DIM);
            gfx.drawString(String("d") + TYPES[i], cx + cw / 2, cy + ch - 8);
            touch::zone(cx, cy, cw, ch, [this, i] {
                if (type_ == i) return;
                type_ = i;
                if (TYPES[i] != 20) mode_ = 0;   // advantage is a d20 thing
                sfx::click();
            });
        }
        // how many, and the modifier: hold a button to repeat
        const int sy = y0 + 2 * ch + g + 14, half = (w - 16) / 2;
        const bool adv = mode_ != 0;
        stepper(x0, sy, half, "NUMBER OF DICE", adv ? String("2") : String(count_), !adv, [this](int d) {
            int c = std::max(1, std::min(MAX_COUNT, count_ + d));
            if (c != count_) { count_ = c; sfx::click(); }
        });
        stepper(x0 + half + 16, sy, half, "MODIFIER", modText(mod_), true, [this](int d) {
            int v = std::max(-MOD_LIMIT, std::min(MOD_LIMIT, mod_ + d));
            if (v != mod_) { mod_ = v; sfx::click(); }
        });
        // normal / advantage / disadvantage (picks the d20)
        const int my = sy + 26 + 80 + 14, sw = (w - 16) / 3;
        static const char* const MODES[] = {"Normal", "Advantage", "Disadvantage"};
        for (int k = 0; k < 3; k++) {
            const int bx = x0 + k * (sw + 8);
            button(bx, my, sw, 60, mode_ == k, k == 1 ? C_EMERALD : k == 2 ? C_BLOOD : C_GOLD);
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(mode_ == k ? C_TEXT : C_DIM);
            gfx.drawString(MODES[k], bx + sw / 2, my + 30);
            touch::zone(bx, my, sw, 60, [this, k] {
                mode_ = k;
                if (k) type_ = D20;
                sfx::click();
            });
        }
        // ROLL
        const int ry = my + 60 + 14, rh = H - FOOTER_H - 10 - ry;
        fillRound(x0, ry, w, rh, 16, rolling_ ? C_BG2 : C_BLOOD_DK);
        gfx.drawRoundRect(x0, ry, w, rh, 16, C_GOLD);
        gfx.drawRoundRect(x0 + 1, ry + 1, w - 2, rh - 2, 15, C_GOLD_DK);
        gfx.setFont(m.big);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(rolling_ ? C_DIM : C_GOLD);
        gfx.drawString("ROLL", x0 + w / 2, ry + rh / 2 - 12);
        gfx.setFont(m.body);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(pickerExpr(), x0 + w / 2, ry + rh - 24);
        touch::zone(x0, ry, w, rh, [this] { if (!rolling_) doRoll(pickerExpr()); });
    }

    template <class F>
    void stepper(int x, int y, int w, const char* label, const String& value, bool enabled, F step) {
        const Metrics& m = metrics();
        gfx.setFont(m.small);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString(label, x + w / 2, y);
        const int by = y + 26, bs = 80;
        for (int k = 0; k < 2; k++) {
            const int bx = k ? x + w - bs : x;
            button(bx, by, bs, bs, false);
            gfx.setFont(ui::font(&fonts::FreeSerifBold18pt7b));
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(enabled ? C_GOLD : C_EDGE);
            gfx.drawString(k ? "+" : "-", bx + bs / 2, by + bs / 2 - 2);
            if (enabled) touch::repeat(bx, by, bs, bs, [step, k] { step(k ? 1 : -1); });
        }
        gfx.setFont(ui::font(&fonts::FreeSerifBold18pt7b));
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(enabled ? C_TEXT : C_DIM);
        gfx.drawString(value, x + w / 2, by + bs / 2);
    }

    void drawTray() {
        const Metrics& m = metrics();
        const int x = TRAY_X, y = HEADER_H + 10, w = TRAY_W, h = H - FOOTER_H - 10 - y;
        // tray-only frames keep the border and clear the inside by DMA
        // (as two rectangles that stay clear of the rounded corners)
        const int c = u(4) + 2;
        if (!touch::frozen() || !board::fill(gfx, x + 4, y + c, w - 8, h - 2 * c, C_BG) ||
            !board::fill(gfx, x + c, y + 4, w - 2 * c, h - 8, C_BG))
            drawPanel(x, y, w, h, C_BG, C_EDGE);
        // tap the tray to roll the same again
        touch::zone(x, y, w, h, [this] { if (!rolling_) doRoll(haveResult_ ? last_ : pickerExpr()); });
        gfx.setFont(m.title);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(rolling_ ? C_DIM : C_GOLD);
        gfx.drawString(fitText(haveResult_ ? cur_.expr : pickerExpr(), w - 40), x + w / 2, y + 12);

        if (!haveResult_ || tray_.empty()) {
            // nothing rolled yet (or only numbers): the chosen die, waiting
            const float bob = sinf(millis() / 500.0f) * 0.1f;
            const int cy = (y + 56 + TOTAL_Y - 80) / 2;
            gfx.fillEllipse(x + w / 2, cy + 100, 80, 14, rgb(6, 5, 10));
            dieshape::draw(TYPES[type_], x + w / 2, cy, 90, bob, 1, C_PANEL, C_GOLD, C_GOLD_DK, 5);
            if (!haveResult_) {
                gfx.setFont(m.body);
                gfx.setTextDatum(middle_center);
                gfx.setTextColor(C_DIM);
                gfx.drawString("Tap ROLL, or type a roll", x + w / 2, TOTAL_Y - 20);
                gfx.drawString("like 4d6kh3 or d20+5 vs 15", x + w / 2, TOTAL_Y + 14);
            }
        }
        tray_.draw();
        if (line_.length()) drawTyped(x, y, w);
        if (haveResult_) drawTotal(x, w);

        for (auto& p : parts_) {
            if (!p.life) continue;
            const int s = p.life > 500 ? 7 : 4;
            gfx.fillRect(u((int)p.x), u((int)p.y), s, s, p.color);
        }
    }

    // The total: live while the dice tumble, then big, with what it was made of.
    void drawTotal(int x, int w) {
        const Metrics& m = metrics();
        const int total = rolling_ ? tray_.liveTotal(0, cur_) : cur_.total;
        const char* label = "TOTAL";
        uint16_t lc = C_DIM;
        if (!rolling_ && cur_.natural == 20) { label = "NATURAL 20!"; lc = C_GOLD; }
        else if (!rolling_ && cur_.natural == 1) { label = "NATURAL 1"; lc = C_BLOOD; }
        else if (!rolling_ && cur_.hasTarget) {
            label = dice::degreeName(cur_.degree);
            lc = cur_.degree >= dice::Degree::Success ? C_EMERALD : C_BLOOD;
        }
        gfx.setFont(m.bold);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(lc);
        gfx.drawString(label, x + w / 2, TOTAL_Y - 56);
        gfx.setFont(ui::font(&fonts::FreeSerifBold24pt7b));
        if (!rolling_) {
            gfx.setTextColor(C_GOLD_DK);
            gfx.drawString(String(total), x + w / 2 + 3, TOTAL_Y + 3);
        }
        gfx.setTextColor(rolling_ ? C_DIM : cur_.natural == 1 ? C_BLOOD : C_GOLD);
        gfx.drawString(String(total), x + w / 2, TOTAL_Y);
        if (!rolling_) {
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            String bd = cur_.breakdown() + (cur_.hasTarget ? String("  vs ") + cur_.target : String(""));
            gfx.drawString(fitText(bd, w - 40), x + w / 2, TOTAL_Y + 58);
        }
    }

    void drawTyped(int x, int y, int w) {
        const Metrics& m = metrics();
        const int by = y + 52;
        drawPanel(x + 12, by, w - 24, 52, C_BG, C_GOLD_DK);
        gfx.setFont(m.body);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(">", x + 26, by + 26);
        gfx.setTextColor(C_TEXT);
        gfx.drawString(line_, x + 50, by + 26);
        if ((millis() / 450) % 2) gfx.fillRect(x + 50 + gfx.textWidth(line_), by + 12, 12, 28, C_GOLD);
    }

    // Recent rolls; a tap rolls one again.
    void drawRecentRolls() {
        const Metrics& m = metrics();
        const int x = TRAY_X + TRAY_W + 10, y = HEADER_H + 10, w = W - 16 - x, h = H - FOOTER_H - 10 - y, rh = 56;
        drawPanel(x, y, w, h, C_BG2, C_EDGE);
        gfx.setFont(m.small);
        gfx.setTextDatum(top_center);
        gfx.setTextColor(C_DIM);
        gfx.drawString("Recent", x + w / 2, y + 10);
        int n = history_.size(), ry = y + 40;
        for (int i = n - 1; i >= 0 && ry + rh <= y + h - 4; i--, ry += rh) {
            const auto& r = history_[i];
            gfx.fillRoundRect(x + 6, ry + 3, w - 12, rh - 6, 8, i == n - 1 ? C_PANEL : C_BG);
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_left);
            gfx.setTextColor(C_DIM);
            gfx.drawString(fitText(r.expr, w - 70), x + 14, ry + rh / 2);
            gfx.setFont(m.bold);
            gfx.setTextDatum(middle_right);
            gfx.setTextColor(r.natural == 20 ? C_GOLD : r.natural == 1 ? C_BLOOD : C_TEXT);
            gfx.drawString(String(r.total), x + w - 14, ry + rh / 2);
            const String ex = r.expr;
            touch::zone(x, ry, w, rh, [this, ex] { if (!rolling_) doRoll(ex); });
            touch::onHold(x, ry, w, rh, [this, ex, i] { holdRoll(ex, i); });
        }
    }

    void drawInputLine() {
        const int y = H - FOOTER_H - u(16);
        drawPanel(u(4), y, W - u(8), u(14), C_BG, (line_.length() ? C_GOLD_DK : C_EDGE));
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(">", u(9), y + u(7));
        gfx.setTextColor(line_.length() ? C_TEXT : C_DIM);
        String shown = line_.length() ? line_ : (last_.length() ? String("Enter: reroll ") + last_ : String("d20"));
        gfx.drawString(shown, u(18), y + u(7));
        if ((millis() / 450) % 2 && line_.length() < 28) {
            int cx = u(18) + (line_.length() ? gfx.textWidth(line_) : 0);
            gfx.fillRect(cx, y + u(3), u(5), u(8), C_GOLD);
        }
    }

    // A long press on a recent roll.
    void holdRoll(const String& expr, int i) {
        holdmenu::open(expr, {{"Roll again", [this, expr] { if (!rolling_) doRoll(expr); }},
                              {"Save as macro", [this, expr] { askMacroName(expr); }},
                              {"Remove", [this, i] {
                                   if (i < (int)history_.size()) history_.erase(history_.begin() + i);
                                   sfx::back();
                               }, C_BLOOD}});
    }

    void askMacroName(const String& expr) {
        if (!storage::ok()) { sfx::error(); toast("No SD card to save to", C_BLOOD); return; }
        newMacro_ = expr;
        prompt_.restoreTextModeTo(true);
        prompt_.open(String("Name for ") + expr, "", 0, 28);
    }

    void addMacro(const String& name, const String& expr) {
        String n = name;
        n.replace("=", "-");   // the file is name=roll
        macros_.push_back({n, expr});
        saveMacros();
        toast((String("Saved ") + n).c_str(), C_EMERALD);
    }

    // The whole list, built-in ones too: the file replaces them once it exists.
    void saveMacros() {
        String out;
        for (auto& m : macros_) out += m.name + "=" + m.expr + "\n";
        if (!storage::writeText("/gmtools/macros.txt", out)) toast("Couldn't save the macros", C_BLOOD);
    }

    // ---- macros ----
    // The web page's saved rolls (the rolls themselves go through /api/roll).
    int web(const WebCall& c, String& out) override {
        if (c.op == "hello") return 200;
        if (!appOpen(this)) loadMacros();
        if (c.op == "add") {
            String n = c.arg("name"), e = c.arg("expr");
            for (const char* bad : {"=", "\n", "\r"}) { n.replace(bad, " "); e.replace(bad, " "); }   // one name=expr line each
            n.trim();
            e.trim();
            if (!n.length() || !dice::roll(e).ok) { out = "Needs a name and a roll that works (e.g. 2d6+3)"; return 400; }
            addMacro(n, e);
        } else if (c.op == "delete") {
            const int i = c.num("i", -1);
            if (i < 0 || i >= (int)macros_.size() || macros_[i].name != c.arg("name")) { out = "That roll changed - reload"; return 409; }
            macros_.erase(macros_.begin() + i);
            saveMacros();
            if (appOpen(this) && view_ == View::Macros) {   // the list moved under the device
                sel_ = std::min(sel_, std::max(0, (int)macros_.size() - 1));
                if (prompt_.active() && prompt_.tag() == 1) prompt_.cancel();
                if (holdmenu::active()) holdmenu::close();
            }
        } else if (c.op != "macros") return 404;
        out = "[";
        for (size_t i = 0; i < macros_.size(); i++)
            out += String(i ? "," : "") + "{\"name\":" + web::js(macros_[i].name) + ",\"expr\":" + web::js(macros_[i].expr) + "}";
        out += "]";
        return 200;
    }

    void loadMacros() {
        macros_.clear();
        std::vector<String> lines;
        if (storage::readLines("/gmtools/macros.txt", lines)) {
            for (auto& l : lines) {
                int eq = l.indexOf('=');
                if (eq > 0) macros_.push_back({l.substring(0, eq), l.substring(eq + 1)});
            }
        }
        if (!macros_.empty()) return;
        macros_ = {{"Ability scores", "4d6kh3"}, {"Advantage", "adv"}, {"Disadvantage", "dis"},
                   {"Death save", "d20 vs 10"}, {"Concentration (DC 10)", "d20 vs 10"},
                   {"Fireball", "8d6"}, {"Sneak attack (L5)", "3d6"}, {"Healing potion", "2d4+2"},
                   {"Greater healing", "4d4+4"}, {"Wild magic surge", "d100"}};
    }

    bool handleMacros(const input::Event& e) {
        int n = macros_.size();
        if (e.key == input::K_BACK) { view_ = View::Main; sfx::back(); return true; }
        if (e.key == input::K_UP && n) { sel_ = (sel_ + n - 1) % n; sfx::click(); }
        if (e.key == input::K_DOWN && n) { sel_ = (sel_ + 1) % n; sfx::click(); }
        if (e.key == input::K_ENTER && n) { view_ = View::Main; doRoll(macros_[sel_].expr); }
        return true;
    }

    void drawMacros() {
        drawList("Saved rolls", macros_.size(), [&](int i, int y, bool s) {
            gfx.setTextColor(s ? C_GOLD : C_TEXT);
            gfx.drawString(macros_[i].name, u(12), y);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(s ? C_TEXT : C_DIM);
            gfx.drawString(macros_[i].expr, W - u(10), y);
            gfx.setTextDatum(top_left);
        });
        if (wide())
            for (int i = 0; i < (int)macros_.size(); i++) {
                const int rowH = touchRow(12), top = HEADER_H + u(22);
                const int rows = (H - FOOTER_H - top) / rowH, n = macros_.size();
                const int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
                if (i < first || i >= first + rows) continue;
                const int y = top + (i - first) * rowH - u(2);
                touch::onHold(0, y, W, rowH, [this, i] {
                    sel_ = i;
                    holdmenu::open(macros_[i].name, {{"Roll", [this, i] { view_ = View::Main; doRoll(macros_[i].expr); }},
                                                     {"Rename", [this, i] {
                                                          renameAt_ = i;
                                                          prompt_.restoreTextModeTo(true);
                                                          prompt_.open("New name", macros_[i].name, 1, 28);
                                                      }},
                                                     {"Delete", [this, i] {
                                                          macros_.erase(macros_.begin() + i);
                                                          sel_ = std::max(0, std::min(sel_, (int)macros_.size() - 1));
                                                          saveMacros();
                                                          sfx::back();
                                                      }, C_BLOOD, true}});
                });
            }
        drawFooter(!storage::ok() ? "Ent roll  Esc back" : wide() ? "Ent roll  hold for more  Esc back"
                                                                   : "Ent roll  Esc back  edit /gmtools/macros.txt");
    }

    bool handleHistory(const input::Event& e) {
        int n = history_.size();
        if (e.key == input::K_BACK) { view_ = View::Main; sfx::back(); return true; }
        if (e.key == input::K_UP && n) { sel_ = std::max(0, sel_ - 1); sfx::click(); }
        if (e.key == input::K_DOWN && n) { sel_ = std::min(n - 1, sel_ + 1); sfx::click(); }
        if (e.key == input::K_ENTER && n) { view_ = View::Main; doRoll(history_[n - 1 - sel_].expr); }
        return true;
    }

    void drawHistory() {
        int n = history_.size();
        drawList("History", n, [&](int i, int y, bool s) {
            const auto& r = history_[n - 1 - i];
            gfx.setTextColor(s ? C_GOLD : C_DIM);
            gfx.drawString(r.expr, u(12), y);
            gfx.setTextDatum(top_right);
            uint16_t c = r.natural == 20 ? C_GOLD : r.natural == 1 ? C_BLOOD : C_TEXT;
            gfx.setTextColor(c);
            gfx.drawString(String(r.total), W - u(10), y);
            gfx.setTextDatum(top_left);
        });
        drawFooter("Ent reroll  Esc back  FnC clear (main)");
    }

    template <typename F>
    void drawList(const char* title, int n, F row) {
        gfx.setFont(ui::font(&fonts::FreeSerifBold9pt7b));
        gfx.setTextColor(C_GOLD);
        gfx.setTextDatum(top_left);
        gfx.drawString(title, u(8), HEADER_H + u(3));
        gfx.setFont(ui::font(&fonts::Font0));
        if (n == 0) {
            gfx.setTextColor(C_DIM);
            gfx.drawString("Nothing here yet.", u(12), HEADER_H + u(26));
            return;
        }
        const int rowH = touchRow(12), pad = touchPad(12), top = HEADER_H + u(22), rows = wide() ? (H - FOOTER_H - top) / rowH : 7;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH + pad;
            bool s = i == sel_;
            if (s) gfx.fillRoundRect(u(6), y - pad - u(2), W - u(12), rowH, u(2), C_PANEL);
            touch::row(0, y - pad - u(2), W, rowH, sel_, i);
            row(i, y, s);
        }
    }
};

}  // namespace

App* makeDiceApp() { return new DiceApp(); }

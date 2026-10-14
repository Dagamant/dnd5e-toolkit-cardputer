#include "../core/app.h"
#include "../core/dice_expr.h"
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
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        gfx.setTextColor(C_GOLD);
        gfx.setTextDatum(middle_center);
        gfx.drawString("20", cx, cy + 1);
    }

    void onEnter() override {
        input::setTextMode(true);
        loadMacros();
        lastShake_ = millis();
    }
    void onExit() override { input::setTextMode(false); }

    bool handle(const input::Event& e) override {
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
                    doRoll(line_.length() ? line_ : (last_.length() ? last_ : String("d20")));
                }
            }
        }
        // settle
        if (rolling_ && anim_ >= ROLL_MS) {
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

    void draw() override {
        drawBackground();
        drawHeader("Dice", C_GOLD);

        if (view_ == View::Macros) { drawMacros(); return; }
        if (view_ == View::History) { drawHistory(); return; }

        drawStage();
        drawInputLine();
        drawFooter("Ent roll  Fn1-7 d4..d100  FnA/D adv/dis  FnM macros");
    }

private:
    enum class View { Main, Macros, History };
    static constexpr uint32_t ROLL_MS = 750;

    View view_ = View::Main;
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
            parts_[i] = {60.0f, 62.0f, cosf(a) * s, sinf(a) * s - 1.2f, (uint16_t)(500 + rng::below(500)),
                         (i % 3) ? color : C_TEXT};
        }
    }

    void drawStage() {
        const int cx = 60, cy = 62;
        float t = rolling_ ? (float)anim_ / ROLL_MS : 1.0f;
        int ox = 0, oy = 0;
        float rot = 0;
        int r = 34;
        if (rolling_) {
            // tumble: spin decays, bounce on an exponential envelope
            float k = 1 - easeOut(t);
            rot = k * 14.0f;
            oy = -(int)(fabsf(sinf(t * 11.0f)) * 14 * k);
            ox = (int)(sinf(t * 23.0f) * 6 * k);
        } else if (shakeAmt_ > 0) {
            ox = (int)((rng::unit() - 0.5f) * shakeAmt_ * 2);
        }
        // pop on settle
        if (!rolling_ && haveResult_ && anim_ < ROLL_MS + 180) {
            float p = (float)(anim_ - ROLL_MS) / 180.0f;
            r += (int)(6 * sinf(p * 3.1416f));
        }

        bool crit = haveResult_ && !rolling_ && (cur_.natural == 20 || cur_.degree == dice::Degree::CritSuccess);
        bool fumble = haveResult_ && !rolling_ && (cur_.natural == 1 || cur_.degree == dice::Degree::CritFail);
        uint16_t fill = crit ? C_GOLD_DK : fumble ? C_BLOOD_DK : C_PANEL;
        uint16_t edge = crit ? C_GOLD : fumble ? C_BLOOD : C_GOLD;

        // glow ring for crits
        if (crit) {
            float pulse = 0.5f + 0.5f * sinf(anim_ * 0.008f);
            gfx.drawCircle(cx, cy, r + 6 + (int)(pulse * 3), C_GOLD_DK);
            gfx.drawCircle(cx, cy, r + 9 + (int)(pulse * 3), rgb(70, 50, 20));
        }
        // shadow
        gfx.fillEllipse(cx, cy + 40, 26 - abs(oy), 4, rgb(8, 6, 12));
        drawDieShape(shownSides_, cx + ox, cy + oy, r, fill, edge, rot);

        // number
        char num[12];
        if (!haveResult_) strcpy(num, "?");
        else if (rolling_) snprintf(num, sizeof(num), "%d", rng::roll(std::max(2, shownSides_)));
        else snprintf(num, sizeof(num), "%d", cur_.total);
        gfx.setTextDatum(middle_center);
        gfx.setFont(strlen(num) > 2 ? &fonts::FreeSerifBold12pt7b : &fonts::FreeSerifBold18pt7b);
        gfx.setTextColor(rgb(0, 0, 0));
        gfx.drawString(num, cx + ox + 1, cy + oy + 3);
        gfx.setTextColor(crit ? rgb(255, 230, 150) : fumble ? rgb(255, 160, 160) : C_TEXT);
        gfx.drawString(num, cx + ox, cy + oy + 2);

        for (auto& p : parts_) {
            if (!p.life) continue;
            int s = p.life > 500 ? 2 : 1;
            gfx.fillRect((int)p.x, (int)p.y, s, s, p.color);
        }

        // right panel: details
        const int px = 122, py = HEADER_H + 4, pw = W - px - 4, ph = 84;
        drawPanel(px, py, pw, ph, C_BG2, C_EDGE);
        gfx.setTextDatum(top_left);
        if (!haveResult_) {
            gfx.setFont(&fonts::Font0);
            gfx.setTextColor(C_DIM);
            textWrapped("Type a roll like 4d6kh3, adv+5, 8d6 or d20+7 vs 15 and press Enter.\n\nOr shake me.",
                        px + 5, py + 5, pw - 10, 10, 8);
            return;
        }
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        gfx.setTextColor(C_GOLD);
        String ex = cur_.expr;
        if (gfx.textWidth(ex) > pw - 10) { gfx.setFont(&fonts::Font0); }
        gfx.drawString(ex, px + 5, py + 4);

        if (rolling_) return;
        gfx.setFont(&fonts::Font0);
        int y = py + 22;
        // breakdown with dropped dice dimmed
        y = drawBreakdown(px + 5, y, pw - 10);

        if (cur_.natural) {
            gfx.setTextColor(cur_.natural == 20 ? C_GOLD : cur_.natural == 1 ? C_BLOOD : C_DIM);
            gfx.drawString(String("natural ") + cur_.natural, px + 5, y + 2);
            y += 12;
        }
        if (cur_.hasTarget) {
            uint16_t c = cur_.degree >= dice::Degree::Success ? C_EMERALD : C_BLOOD;
            if (cur_.degree == dice::Degree::CritSuccess) c = C_GOLD;
            gfx.setTextColor(C_DIM);
            gfx.drawString(String("vs DC ") + cur_.target, px + 5, py + ph - 24);
            gfx.setTextColor(c);
            gfx.drawString(dice::degreeName(cur_.degree), px + 5, py + ph - 13);
        } else if (cur_.maxTotal > cur_.minTotal) {
            // where this roll landed in its possible range
            float f = (float)(cur_.total - cur_.minTotal) / (cur_.maxTotal - cur_.minTotal);
            gfx.setTextColor(C_DIM);
            gfx.drawString(String(cur_.minTotal), px + 5, py + ph - 11);
            gfx.setTextDatum(top_right);
            gfx.drawString(String(cur_.maxTotal), px + pw - 5, py + ph - 11);
            gfx.setTextDatum(top_left);
            drawBar(px + 22, py + ph - 9, pw - 50, 4, f, f > 0.8f ? C_GOLD : f < 0.2f ? C_BLOOD : C_ARCANE, C_PANEL);
        }
    }

    int drawBreakdown(int x, int y, int w) {
        int cx = x;
        auto put = [&](const String& s, uint16_t c, bool strike) {
            int tw = gfx.textWidth(s);
            if (cx + tw > x + w) { cx = x; y += 10; }
            if (y > HEADER_H + 4 + 60) return;
            gfx.setTextColor(c);
            gfx.drawString(s, cx, y);
            if (strike) gfx.drawFastHLine(cx, y + 3, tw, C_BLOOD);
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
        return y + 12;
    }

    void drawInputLine() {
        const int y = H - FOOTER_H - 16;
        drawPanel(4, y, W - 8, 14, C_BG, (line_.length() ? C_GOLD_DK : C_EDGE));
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(middle_left);
        gfx.setTextColor(C_GOLD);
        gfx.drawString(">", 9, y + 7);
        gfx.setTextColor(line_.length() ? C_TEXT : C_DIM);
        String shown = line_.length() ? line_ : (last_.length() ? String("Enter: reroll ") + last_ : String("d20"));
        gfx.drawString(shown, 18, y + 7);
        if ((millis() / 450) % 2 && line_.length() < 28) {
            int cx = 18 + (line_.length() ? gfx.textWidth(line_) : 0);
            gfx.fillRect(cx, y + 3, 5, 8, C_GOLD);
        }
    }

    // ---- macros ----
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
            gfx.drawString(macros_[i].name, 12, y);
            gfx.setTextDatum(top_right);
            gfx.setTextColor(s ? C_TEXT : C_DIM);
            gfx.drawString(macros_[i].expr, W - 10, y);
            gfx.setTextDatum(top_left);
        });
        drawFooter(storage::ok() ? "Ent roll  Esc back  edit /gmtools/macros.txt" : "Ent roll  Esc back");
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
            gfx.drawString(r.expr, 12, y);
            gfx.setTextDatum(top_right);
            uint16_t c = r.natural == 20 ? C_GOLD : r.natural == 1 ? C_BLOOD : C_TEXT;
            gfx.setTextColor(c);
            gfx.drawString(String(r.total), W - 10, y);
            gfx.setTextDatum(top_left);
        });
        drawFooter("Ent reroll  Esc back  FnC clear (main)");
    }

    template <typename F>
    void drawList(const char* title, int n, F row) {
        gfx.setFont(&fonts::FreeSerifBold9pt7b);
        gfx.setTextColor(C_GOLD);
        gfx.setTextDatum(top_left);
        gfx.drawString(title, 8, HEADER_H + 3);
        gfx.setFont(&fonts::Font0);
        if (n == 0) {
            gfx.setTextColor(C_DIM);
            gfx.drawString("Nothing here yet.", 12, HEADER_H + 26);
            return;
        }
        const int rowH = 12, top = HEADER_H + 22, rows = 7;
        int first = std::max(0, std::min(sel_ - rows / 2, n - rows));
        for (int i = first; i < n && i < first + rows; i++) {
            int y = top + (i - first) * rowH;
            bool s = i == sel_;
            if (s) gfx.fillRoundRect(6, y - 2, W - 12, rowH, 2, C_PANEL);
            row(i, y, s);
        }
    }
};

}  // namespace

App* makeDiceApp() { return new DiceApp(); }

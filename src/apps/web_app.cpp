#include "../core/app.h"
#include "../core/prompt.h"
#include "../core/sfx.h"
#include "../core/ui.h"
#include "../core/web.h"
#include "../core/widgets.h"

using namespace ui;

// Web access: starts the web server (core/web.h) and shows how to reach it.
// It keeps running after leaving this app, until stopped here or power-off.

namespace {

enum { P_SSID = 1, P_PASS };
// QR side in px; bigger on the Tab5 so a phone reads it from across the table
inline int qrSize() { return ui::wide() ? 360 : 84; }

class WebApp : public App {
public:
    const char* name() const override { return "Web"; }
    const char* blurb() const override { return "Use the toolkit from a phone or laptop."; }
    uint16_t accent() const override { return C_ICE; }

    void drawIcon(int cx, int cy, float t) override {
        // a globe with a pulsing signal
        gfx.drawCircle(cx, cy + u(3), u(15), C_ICE);
        gfx.drawEllipse(cx, cy + u(3), u(7), u(15), C_ICE);
        gfx.drawFastHLine(cx - u(15), cy + u(3), u(31), C_ICE);
        gfx.drawFastHLine(cx - u(13), cy - u(4), u(27), C_EDGE);
        gfx.drawFastHLine(cx - u(13), cy + u(10), u(27), C_EDGE);
        int k = (int)(t * 2) % 3;
        for (int i = 0; i < 3; i++)
            gfx.drawArc(cx + u(15), cy - u(13), u(4) + i * 4, u(4) + i * 4, 225, 315, i <= k ? C_GOLD : C_EDGE);
    }

    void onEnter() override { qrKey_ = ""; }
    void onExit() override { qr_.deleteSprite(); qrKey_ = ""; }

    bool handle(const input::Event& e) override {
        if (prompt_.active()) {
            prompt_.handle(e);
            if (prompt_.done()) {
                if (prompt_.tag() == P_SSID) {
                    ssid_ = prompt_.value();
                    ssid_.trim();
                    if (!ssid_.length()) { web::saveNetwork("", ""); toast("Home WiFi forgotten - hotspot only"); }
                    else prompt_.open("Password for " + ssid_, "", P_PASS, 63);
                } else {
                    web::saveNetwork(ssid_, prompt_.value());
                    toast(web::state() == web::OFF ? "Saved" : "Saved - restart web access to use it");
                }
            }
            return true;
        }
        if (e.key == input::K_BACK) return false;
        if (e.key == input::K_ENTER) { startStop(); return true; }
        if (e.key == input::K_CHAR) {
            if (e.base == 'w') askWifi();
            else if (e.base == 'q') { showUrl_ = !showUrl_; sfx::click(); }
        }
        return true;
    }

    void startStop() {
        if (web::state() == web::OFF) { web::start(); sfx::select(); }
        else { web::stop(); qrKey_ = ""; sfx::back(); toast("Web access off", C_DIM); }
    }
    void askWifi() { prompt_.open("Home WiFi name (empty = none)", web::savedSsid(), P_SSID, 32); }

    bool bigLayout() const override { return true; }

    void draw() override {
        if (wide()) { drawWide(); return; }
        drawBackground();
        drawHeader("Web access", C_ICE);
        gfx.setFont(ui::font(&fonts::Font0));
        gfx.setTextDatum(top_left);
        const int x = u(8), QR = qrSize();
        int y = HEADER_H + u(wide() ? 14 : 5);
        auto row = [&](const char* label, const String& value, uint16_t c = C_TEXT) {
            gfx.setTextColor(C_DIM);
            gfx.drawString(label, x, y);
            gfx.setTextColor(c);
            gfx.drawString(fitText(value, W - QR - u(14) - x), x, y + u(10));
            y += u(23);
        };
        switch (web::state()) {
            case web::OFF: {
                gfx.setTextColor(C_TEXT);
                y = textWrapped("Edit campaigns, notes and SD files from a phone or laptop, browse the SRD, and "
                                "show initiative and rolls live on another screen.", x, y, W - 2 * x, u(10));
                y += u(6);
                String s = web::savedSsid();
                gfx.setTextColor(C_DIM);
                y = textWrapped(s.length() ? (String("Joins \"") + s + "\", or makes its own hotspot if it can't.").c_str()
                                           : "Makes its own WiFi hotspot. W saves a home network to join instead.",
                                x, y, W - 2 * x, u(10));
                gfx.setTextColor(C_GOLD);
                gfx.drawString("Enter to start", x, y + u(6));
                drawFooter("Ent start  W home WiFi  Esc back");
                break;
            }
            case web::CONNECTING: {
                gfx.setTextColor(C_TEXT);
                String dots = String("...").substring(0, (millis() / 400) % 4);
                gfx.drawString("Joining " + web::networkName() + dots, x, y + u(10));
                gfx.setTextColor(C_DIM);
                gfx.drawString("If it can't, it makes a hotspot.", x, y + u(26));
                drawFooter("Ent cancel  Esc back");
                break;
            }
            case web::RUNNING: {
                bool ap = web::apMode();
                if (ap) {
                    row("Join WiFi", web::networkName(), C_ICE);
                    row("Password", web::apPassword(), C_ICE);
                    row("Then open", web::address());
                } else {
                    row("On WiFi", web::networkName(), C_ICE);
                    row("Open", web::address());
                    row("or", "http://dnd.local", C_DIM);
                }
                gfx.setTextColor(C_DIM);
                gfx.drawString("PIN", x, y + u(4));
                gfx.setFont(ui::font(&fonts::FreeSerifBold12pt7b));
                gfx.setTextColor(C_GOLD);
                gfx.drawString(web::pin(), x + u(24), y - u(1));
                gfx.setFont(ui::font(&fonts::Font0));
                // QR: joining the hotspot first, Q flips to the address
                bool url = !ap || showUrl_;
                String text = url ? web::address()
                                  : "WIFI:T:WPA;S:" + web::networkName() + ";P:" + web::apPassword() + ";;";
                drawQr(text, W - QR - u(6), HEADER_H + u(3));
                gfx.setTextDatum(top_center);
                gfx.setTextColor(C_DIM);
                gfx.drawString(url ? "scan to open" : "scan to join", W - QR / 2 - u(6), HEADER_H + QR + u(5));
                gfx.setTextDatum(top_left);
                if (web::requests()) {
                    gfx.setTextDatum(top_right);
                    gfx.drawString(String(web::requests()) + " req", W - QR - u(12), y + u(4));
                    gfx.setTextDatum(top_left);
                }
                drawFooter(ap ? "Ent stop  Q QR  W WiFi  Esc keep on" : "Ent stop  W WiFi  Esc keep on");
                break;
            }
        }
        prompt_.draw();
        drawToast();
    }


    // ---------------------------------------------------------------- big screen
    // Buttons for what the keys do, the details as cards, and the QR code big
    // on the right where a phone can read it from across the table.
    void drawWide() {
        using namespace widgets;
        const Metrics& m = metrics();
        drawBackground();
        drawHeader("Web access", C_ICE);
        const int QR = qrSize(), PW = QR + 40, PX = W - 16 - PW, LX = 16, LW = PX - 24 - LX;
        const int top = HEADER_H + 10, bottom = H - FOOTER_H - 10;
        const web::State st = web::state();

        // the QR panel
        drawPanel(PX, top, PW, bottom - top, C_BG2, C_EDGE);
        const int qy = top + 20;
        if (st == web::RUNNING) {
            const bool ap = web::apMode(), url = !ap || showUrl_;
            const String text = url ? web::address() : "WIFI:T:WPA;S:" + web::networkName() + ";P:" + web::apPassword() + ";;";
            drawQr(text, PX + 20, qy);
            gfx.setFont(m.body);
            gfx.setTextDatum(top_center);
            gfx.setTextColor(C_ICE);
            gfx.drawString(url ? "Scan to open the page" : "Scan to join the WiFi", PX + PW / 2, qy + QR + 14);
            if (ap)
                button(PX + 20, bottom - 72, QR, 56, url ? "Show the WiFi code" : "Show the page code", false, C_ICE,
                       [this] { showUrl_ = !showUrl_; sfx::click(); });
        } else {
            gfx.drawRoundRect(PX + 20, qy, QR, QR, 12, C_EDGE);
            gfx.setFont(m.small);
            gfx.setTextDatum(middle_center);
            gfx.setTextColor(C_DIM);
            gfx.drawString("A code to scan", PX + PW / 2, qy + QR / 2 - 16);
            gfx.drawString("shows here once it's on", PX + PW / 2, qy + QR / 2 + 16);
        }

        // details and buttons
        int y = top;
        const String saved = web::savedSsid();
        auto card = [&](const char* label, const String& value, uint16_t c, const lgfx::IFont* f) {
            drawPanel(LX, y, LW, 86, C_BG2, C_EDGE);
            gfx.setTextDatum(top_left);
            gfx.setFont(m.small);
            gfx.setTextColor(C_DIM);
            gfx.drawString(label, LX + 18, y + 10);
            gfx.setFont(f);
            gfx.setTextColor(c);
            gfx.drawString(fitText(value, LW - 36), LX + 18, y + 38);
            y += 96;
        };
        switch (st) {
            case web::OFF: {
                gfx.setFont(m.title);
                gfx.setTextDatum(top_left);
                gfx.setTextColor(C_TEXT);
                gfx.drawString("Use the toolkit from a phone or laptop", LX + 4, y + 4);
                y += 60;
                gfx.setFont(m.body);
                const char* const what[] = {"Run the fight, roll dice and play characters",
                                            "Plan dungeons, encounters, shops and travel",
                                            "Browse the SRD; edit campaigns, notes and SD files",
                                            "Show initiative, rolls and dungeon maps on a TV"};
                for (auto w : what) {
                    gfx.fillCircle(LX + 14, y + 16, 5, C_ICE);
                    gfx.setTextColor(C_TEXT);
                    gfx.drawString(w, LX + 32, y);
                    y += 40;
                }
                y += 16;
                card(saved.length() ? "Joins this WiFi" : "Network", saved.length() ? saved : String("Its own WiFi hotspot"),
                     C_ICE, m.title);
                gfx.setFont(m.small);
                gfx.setTextDatum(top_left);
                gfx.setTextColor(C_DIM);
                gfx.drawString(saved.length() ? "If it can't, it makes its own hotspot." : "Home WiFi... saves a network to join instead.",
                               LX + 4, y);
                button(LX, bottom - 64, LW / 2 - 6, 64, "Start web access", true, C_GOLD, [this] { startStop(); });
                button(LX + LW / 2 + 6, bottom - 64, LW / 2 - 6, 64, "Home WiFi...", false, C_ICE, [this] { askWifi(); });
                drawFooter("Ent start  W home WiFi  Esc back");
                break;
            }
            case web::CONNECTING: {
                const String dots = String("...").substring(0, (millis() / 400) % 4);
                card("Joining", web::networkName() + dots, C_ICE, m.title);
                gfx.setFont(m.body);
                gfx.setTextDatum(top_left);
                gfx.setTextColor(C_DIM);
                gfx.drawString("If it can't, it makes its own hotspot.", LX + 4, y + 8);
                button(LX, bottom - 64, LW / 2 - 6, 64, "Cancel", false, C_BLOOD, [this] { startStop(); });
                drawFooter("Ent cancel  Esc back");
                break;
            }
            case web::RUNNING: {
                const bool ap = web::apMode();
                if (ap) {
                    card("1. Join this WiFi", web::networkName(), C_ICE, m.title);
                    card("Password", web::apPassword(), C_ICE, m.title);
                    card("2. Then open", web::address(), C_TEXT, m.title);
                } else {
                    card("On WiFi", web::networkName(), C_ICE, m.title);
                    card("Open", web::address() + "  or  http://dnd.local", C_TEXT, m.title);
                }
                // the PIN, big
                drawPanel(LX, y, LW, 86, C_PANEL, C_GOLD_DK);
                gfx.setTextDatum(middle_left);
                gfx.setFont(m.small);
                gfx.setTextColor(C_DIM);
                gfx.drawString("PIN to sign in", LX + 18, y + 43);
                gfx.setFont(m.big);
                gfx.setTextColor(C_GOLD);
                gfx.drawString(web::pin(), LX + 220, y + 46);
                if (web::requests()) {
                    gfx.setFont(m.small);
                    gfx.setTextDatum(middle_right);
                    gfx.setTextColor(C_DIM);
                    gfx.drawString(String(web::requests()) + " requests", LX + LW - 18, y + 43);
                }
                button(LX, bottom - 64, LW / 2 - 6, 64, "Stop web access", false, C_BLOOD, [this] { startStop(); });
                button(LX + LW / 2 + 6, bottom - 64, LW / 2 - 6, 64, "Home WiFi...", false, C_ICE, [this] { askWifi(); });
                drawFooter("Ent stop  W WiFi  Esc keep on");
                break;
            }
        }
        prompt_.draw();
        drawToast();
    }
private:
    // The QR is drawn once into a 1-bit sprite and reused every frame.
    void drawQr(const String& text, int x, int y) {
        if (text != qrKey_) {
            const int QR = qrSize();
            if (qr_.getBuffer() && qr_.width() != QR) qr_.deleteSprite();
            if (!qr_.getBuffer()) {
                qr_.setColorDepth(1);
                if (!qr_.createSprite(QR, QR)) return;
            }
            qr_.fillScreen(1);
            qr_.qrcode(text.c_str(), 0, 0, QR, 1, true);
            qrKey_ = text;
        }
        qr_.pushSprite(&gfx, x, y);
    }

    Prompt prompt_;
    String ssid_, qrKey_;
    bool showUrl_ = false;
    M5Canvas qr_;
};

}  // namespace

App* makeWebApp() { return new WebApp(); }

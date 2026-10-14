#include "../core/app.h"
#include "../core/prompt.h"
#include "../core/sfx.h"
#include "../core/ui.h"
#include "../core/web.h"

using namespace ui;

// Web access: starts the web server (core/web.h) and shows how to reach it.
// It keeps running after leaving this app, until stopped here or power-off.

namespace {

enum { P_SSID = 1, P_PASS };
constexpr int QR = 84;   // px

class WebApp : public App {
public:
    const char* name() const override { return "Web"; }
    const char* blurb() const override { return "Use the toolkit from a phone or laptop."; }
    uint16_t accent() const override { return C_ICE; }

    void drawIcon(int cx, int cy, float t) override {
        // a globe with a pulsing signal
        gfx.drawCircle(cx, cy + 3, 15, C_ICE);
        gfx.drawEllipse(cx, cy + 3, 7, 15, C_ICE);
        gfx.drawFastHLine(cx - 15, cy + 3, 31, C_ICE);
        gfx.drawFastHLine(cx - 13, cy - 4, 27, C_EDGE);
        gfx.drawFastHLine(cx - 13, cy + 10, 27, C_EDGE);
        int k = (int)(t * 2) % 3;
        for (int i = 0; i < 3; i++)
            gfx.drawArc(cx + 15, cy - 13, 4 + i * 4, 4 + i * 4, 225, 315, i <= k ? C_GOLD : C_EDGE);
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
        if (e.key == input::K_ENTER) {
            if (web::state() == web::OFF) { web::start(); sfx::select(); }
            else { web::stop(); qrKey_ = ""; sfx::back(); toast("Web access off", C_DIM); }
            return true;
        }
        if (e.key == input::K_CHAR) {
            if (e.base == 'w') prompt_.open("Home WiFi name (empty = none)", web::savedSsid(), P_SSID, 32);
            else if (e.base == 'q') { showUrl_ = !showUrl_; sfx::click(); }
        }
        return true;
    }

    void draw() override {
        drawBackground();
        drawHeader("Web access", C_ICE);
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(top_left);
        const int x = 8;
        int y = HEADER_H + 5;
        auto row = [&](const char* label, const String& value, uint16_t c = C_TEXT) {
            gfx.setTextColor(C_DIM);
            gfx.drawString(label, x, y);
            gfx.setTextColor(c);
            gfx.drawString(fitText(value, W - QR - 14 - x), x, y + 10);
            y += 23;
        };
        switch (web::state()) {
            case web::OFF: {
                gfx.setTextColor(C_TEXT);
                y = textWrapped("Edit campaigns, notes and SD files from a phone or laptop, browse the SRD, and "
                                "show initiative and rolls live on another screen.", x, y, W - 2 * x, 10);
                y += 6;
                String s = web::savedSsid();
                gfx.setTextColor(C_DIM);
                y = textWrapped(s.length() ? (String("Joins \"") + s + "\", or makes its own hotspot if it can't.").c_str()
                                           : "Makes its own WiFi hotspot. W saves a home network to join instead.",
                                x, y, W - 2 * x, 10);
                gfx.setTextColor(C_GOLD);
                gfx.drawString("Enter to start", x, y + 6);
                drawFooter("Ent start  W home WiFi  Esc back");
                break;
            }
            case web::CONNECTING: {
                gfx.setTextColor(C_TEXT);
                String dots = String("...").substring(0, (millis() / 400) % 4);
                gfx.drawString("Joining " + web::networkName() + dots, x, y + 10);
                gfx.setTextColor(C_DIM);
                gfx.drawString("If it can't, it makes a hotspot.", x, y + 26);
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
                gfx.drawString("PIN", x, y + 4);
                gfx.setFont(&fonts::FreeSerifBold12pt7b);
                gfx.setTextColor(C_GOLD);
                gfx.drawString(web::pin(), x + 24, y - 1);
                gfx.setFont(&fonts::Font0);
                // QR: joining the hotspot first, Q flips to the address
                bool url = !ap || showUrl_;
                String text = url ? web::address()
                                  : "WIFI:T:WPA;S:" + web::networkName() + ";P:" + web::apPassword() + ";;";
                drawQr(text, W - QR - 6, HEADER_H + 3);
                gfx.setTextDatum(top_center);
                gfx.setTextColor(C_DIM);
                gfx.drawString(url ? "scan to open" : "scan to join", W - QR / 2 - 6, HEADER_H + QR + 5);
                gfx.setTextDatum(top_left);
                if (web::requests()) {
                    gfx.setTextDatum(top_right);
                    gfx.drawString(String(web::requests()) + " req", W - QR - 12, y + 4);
                    gfx.setTextDatum(top_left);
                }
                drawFooter(ap ? "Ent stop  Q QR  W WiFi  Esc keep on" : "Ent stop  W WiFi  Esc keep on");
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

#include "debug_console.h"
#include "foundry.h"
#include "input.h"
#include "touch.h"
#include "ui.h"
#include "storage.h"
#include "app.h"
#include "../board/board.h"
#include <Arduino.h>
#include <SD.h>
#include <mbedtls/base64.h>

namespace debug_console {

static String s_line;

static void key(String name) {
    input::Event e;
    name.trim();
    if (name.startsWith("fn+")) { e.fn = true; name = name.substring(3); }
    if (name.startsWith("shift+")) { e.shift = true; name = name.substring(6); }
    if (name == "up") e.key = input::K_UP;
    else if (name == "down") e.key = input::K_DOWN;
    else if (name == "left") e.key = input::K_LEFT;
    else if (name == "right") e.key = input::K_RIGHT;
    else if (name == "enter") e.key = input::K_ENTER;
    else if (name == "back") e.key = input::K_BACK;
    else if (name == "del") e.key = input::K_DEL;
    else if (name == "tab") e.key = input::K_TAB;
    else if (name == "space") { e.key = input::K_CHAR; e.ch = e.base = ' '; }
    else if (name.length() == 1) {
        e.key = input::K_CHAR;
        e.base = tolower(name[0]);
        e.ch = e.shift ? toupper(name[0]) : name[0];
    } else { Serial.println("ERR key"); return; }
    // mirror the real keyboard: Fn + ; . , / are arrows
    if (e.fn && e.key == input::K_CHAR) {
        if (e.base == ';') e.key = input::K_UP;
        else if (e.base == '.') e.key = input::K_DOWN;
        else if (e.base == ',') e.key = input::K_LEFT;
        else if (e.base == '/') e.key = input::K_RIGHT;
    }
    if (e.key != input::K_CHAR) { e.ch = e.base = 0; }
    input::inject(e);
    Serial.println("OK");
}

static void shot() {
    auto& g = ui::gfx;
    Serial.printf("SHOT %d %d\n", ui::W, ui::H);
    static char buf[1280 * 4 + 2];
    for (int y = 0; y < ui::H; y++) {
        int p = 0;
        for (int x = 0; x < ui::W; x++) {
            uint16_t c = g.readPixel(x, y);
            p += sprintf(buf + p, "%04x", c);
        }
        buf[p++] = '\n';
        buf[p] = 0;
        Serial.write((const uint8_t*)buf, p);
    }
    Serial.println("END");
}

// shot <scale>: every <scale>-th pixel, run-length encoded. Header
// "SHOTRLE w h", then one line per row of 6-hex-digit runs: count (2) + RGB565 (4).
// Big screens (Tab5) need this; on the Cardputer it is also several times faster.
static void shotRle(int scale) {
    auto& g = ui::gfx;
    scale = std::max(1, scale);
    int w = g.width() / scale, h = g.height() / scale;
    watchdogPause(true);   // a full big-screen shot takes several seconds
    Serial.printf("SHOTRLE %d %d\n", w, h);
    String line;
    for (int y = 0; y < h; y++) {
        line = "";
        int x = 0;
        while (x < w) {
            uint16_t c = g.readPixel(x * scale, y * scale);
            int n = 1;
            while (x + n < w && n < 255 && g.readPixel((x + n) * scale, y * scale) == c) n++;
            char run[8];
            snprintf(run, sizeof(run), "%02x%04x", n, c);
            line += run;
            x += n;
        }
        Serial.println(line);
    }
    Serial.println("END");
    watchdogPause(false);
}

// put <path>: then base64 lines, each acked with "k"; a line "end" finishes.
// Runs in a tight loop (no frames drawn) so transfers are fast.
static void put(const String& path) {
    if (!storage::ok()) { Serial.println("ERR no sd"); return; }
    String dir = path.substring(0, path.lastIndexOf('/'));
    // create parent directories
    for (int i = 1; i <= (int)dir.length(); i++)
        if (i == (int)dir.length() || dir[i] == '/') { String d = dir.substring(0, i); if (d.length() && !SD.exists(d)) SD.mkdir(d); }
    File f = SD.open(path, FILE_WRITE);
    if (!f) { Serial.println("ERR open"); return; }
    watchdogPause(true);
    Serial.println("READY");
    String line;
    size_t total = 0;
    static uint8_t buf[1024];
    uint32_t last = millis();
    for (;;) {
        while (Serial.available()) {
            char c = Serial.read();
            last = millis();
            if (c == '\r') continue;
            if (c != '\n') { line += c; continue; }
            if (line == "end") { f.close(); watchdogPause(false); Serial.printf("OK %u\n", (unsigned)total); return; }
            size_t n = 0;
            if (mbedtls_base64_decode(buf, sizeof(buf), &n, (const uint8_t*)line.c_str(), line.length()) != 0) {
                f.close(); watchdogPause(false); Serial.println("ERR b64"); return;
            }
            f.write(buf, n);
            total += n;
            line = "";
            Serial.println("k");
        }
        if (millis() - last > 5000) { f.close(); watchdogPause(false); Serial.println("ERR timeout"); return; }
        delay(1);
    }
}

static void ls(const String& path) {
    File d = SD.open(path.length() ? path : String("/"));
    if (!d) { Serial.println("ERR"); return; }
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        Serial.printf("%s%s %u\n", f.name(), f.isDirectory() ? "/" : "", (unsigned)f.size());
        f.close();
    }
    Serial.println("END");
}

static void run(const String& l) {
    if (l.startsWith("key ")) key(l.substring(4));
    else if (l.startsWith("type ")) {
        for (size_t i = 5; i < l.length(); i++) {
            input::Event e;
            e.key = input::K_CHAR;
            e.ch = l[i];
            e.base = tolower(l[i]);
            input::inject(e);
        }
        Serial.println("OK");
    } else if (l.startsWith("drag ")) {   // drag X Y DY: a quick flick, DY px up (+) or down (-)
        int x = 0, y = 0, dy = 0;
        sscanf(l.c_str() + 5, "%d %d %d", &x, &y, &dy);
        touch::press();
        for (int i = 0; i < 10; i++) { touch::drag(x, y, dy / 10); delay(12); }
        touch::release();
        Serial.println("OK");
    } else if (l.startsWith("hold ")) {   // hold X Y: a long press (opens a hold menu)
        int x = 0, y = 0;
        sscanf(l.c_str() + 5, "%d %d", &x, &y);
        touch::hold(x, y, 0);
        touch::hold(x, y, 600);
        Serial.println("OK");
    } else if (l.startsWith("tap ")) {   // tap X Y: a touch screen tap
        input::Event e;
        e.key = input::K_TAP;
        int sp = l.indexOf(' ', 4);
        e.x = l.substring(4, sp).toInt();
        e.y = l.substring(sp + 1).toInt();
        input::inject(e);
        Serial.println("OK");
    } else if (l == "shot") shot();
    else if (l.startsWith("shot ")) shotRle(l.substring(5).toInt());
    else if (l.startsWith("put ")) put(l.substring(4));
    else if (l.startsWith("ls")) ls(l.substring(2).length() ? l.substring(3) : String("/"));
    else if (l.startsWith("rm ")) Serial.println(SD.remove(l.substring(3)) ? "OK" : "ERR");
#ifdef BOARD_TAB5
    else if (l == "nokb") { board::pretendNoKeyboard(!board::pretendingNoKeyboard()); Serial.println(board::pretendingNoKeyboard() ? "no keyboard" : "keyboard"); }
#endif
    else if (l == "perf") { char b[96]; perfReport(b, sizeof(b)); Serial.println(b); }
    else if (l == "mem") Serial.printf("heap %u  min %u  block %u  stack-free %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                                       (unsigned)ESP.getMaxAllocHeap(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    else if (l == "foundry") {   // size and push count of each Foundry section
        static const char* names[] = {"actors", "combat", "chat", "journals", "page", "sheet"};
        for (int i = 0; i < foundry::NSECTIONS; i++)
            Serial.printf("%s %u bytes, v%u\n", names[i], (unsigned)foundry::text((foundry::Section)i).length(),
                          (unsigned)foundry::version((foundry::Section)i));
        Serial.printf("connected %d\n", foundry::connected());
    }
    else Serial.println("ERR unknown");
}

void update() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') { s_line.trim(); if (s_line.length()) run(s_line); s_line = ""; }
        else if (s_line.length() < 120) s_line += c;
    }
}

}  // namespace debug_console

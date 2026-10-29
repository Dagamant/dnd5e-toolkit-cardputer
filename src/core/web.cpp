#include "web.h"
#include "clock.h"
#include "app.h"
#include "dice_expr.h"
#include "foundry.h"
#include "roll_overlay.h"
#include "settings.h"
#include "storage.h"
#include "web_page.h"
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <Preferences.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

namespace web {

static State s_state = OFF;
static bool s_ap = false;
static WebServer* s_server = nullptr;
static String s_pin, s_token, s_apPass, s_name;
static uint32_t s_deadline = 0, s_requests = 0;
static int s_badLogins = 0;
static uint32_t s_lockedUntil = 0;

static constexpr uint32_t JOIN_TIMEOUT_MS = 12000;
static constexpr size_t MAX_TEXT = 64 * 1024;

// ---------------------------------------------------------------- recent rolls

struct Roll { uint32_t id; String title, label, expr, breakdown; int total, natural; bool crit; };
static constexpr int MAX_ROLLS = 16;
static Roll s_rolls[MAX_ROLLS];
static uint32_t s_rollId = 0;

void logRoll(const String& title, const String& label, const String& expr, int total, const String& breakdown,
             bool crit, int natural) {
    if (s_state != RUNNING) return;
    s_rollId++;
    s_rolls[s_rollId % MAX_ROLLS] = {s_rollId, title, label, expr, breakdown, total, natural, crit};
}

// ---------------------------------------------------------------- helpers

static String randomString(const char* alphabet, int n) {
    String s;
    int len = strlen(alphabet);
    for (int i = 0; i < n; i++) s += alphabet[esp_random() % len];
    return s;
}

String js(const String& s) {
    String o = "\"";
    o.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') continue;
        else if (c == '\t') o += "\\t";
        else if ((uint8_t)c < 0x20) continue;
        else o += c;
    }
    return o + "\"";
}

static String json(const String& s) { return js(s); }

// Only paths inside /gmtools, without "..".
static bool safePath(const String& p) {
    if (p != "/gmtools" && !p.startsWith("/gmtools/")) return false;
    return p.indexOf("..") < 0 && p.indexOf("//") < 0 && p.indexOf('\\') < 0;
}

static bool authed() {
    if (!s_server) return false;
    String t = s_server->header("X-Token");
    if (!t.length()) {   // downloads opened as plain links carry it in the URL
        t = s_server->arg("t");
    }
    return t.length() && t == s_token;
}

static void reply(int code, const String& body, const char* type = "text/plain") {
    s_server->sendHeader("Cache-Control", "no-store");
    s_server->send(code, type, body);
}

// Each API route checks the token and the path argument first.
static bool guard(String* path = nullptr, const char* arg = "path") {
    s_requests++;
    if (!authed()) { reply(401, "PIN needed"); return false; }
    if (!path) return true;
    *path = s_server->arg(arg);
    if (path->length() > 1 && path->endsWith("/")) path->remove(path->length() - 1);
    if (!storage::ok()) { reply(503, "No SD card"); return false; }
    if (!safePath(*path)) { reply(403, "Path not allowed"); return false; }
    return true;
}

static void makeParents(const String& path) {
    for (int i = 1; i < (int)path.length(); i++)
        if (path[i] == '/') { String d = path.substring(0, i); if (!SD.exists(d)) SD.mkdir(d); }
}

// Replace `path` with `tmp` (already written): a power cut leaves either the
// old file or the new one, never half of one.
static bool commit(const String& tmp, const String& path) {
    if (SD.exists(path) && !SD.remove(path)) { SD.remove(tmp); return false; }
    return SD.rename(tmp, path);
}

static bool removeTree(const String& path, int depth = 0) {
    File f = SD.open(path);
    if (!f) return false;
    bool dir = f.isDirectory();
    if (!dir) { f.close(); return SD.remove(path); }
    if (depth > 8) { f.close(); return false; }
    std::vector<String> kids;
    for (File k = f.openNextFile(); k; k = f.openNextFile()) { kids.push_back(path + "/" + k.name()); k.close(); }
    f.close();
    bool ok = true;
    for (auto& k : kids) ok &= removeTree(k, depth + 1);
    return SD.rmdir(path) && ok;
}

static const char* contentType(const String& p) {
    if (p.endsWith(".txt") || p.endsWith(".idx") || p.endsWith(".md") || p.endsWith(".csv")) return "text/plain; charset=utf-8";
    if (p.endsWith(".json")) return "application/json";
    if (p.endsWith(".png")) return "image/png";
    if (p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
    if (p.endsWith(".html")) return "text/plain; charset=utf-8";   // never run stored pages in our origin
    return "application/octet-stream";
}

// ---------------------------------------------------------------- routes

static void handleLogin() {
    s_requests++;
    if (millis() < s_lockedUntil) { reply(429, "Too many tries - wait a moment"); return; }
    String p = s_server->hasArg("pin") ? s_server->arg("pin") : s_server->arg("plain");   // page sends text/plain
    p.trim();
    if (p != s_pin) {
        if (++s_badLogins >= 5) { s_badLogins = 0; s_lockedUntil = millis() + 30000; }
        reply(403, "Wrong PIN");
        return;
    }
    s_badLogins = 0;
    reply(200, s_token);
}

static void handleStatus() {
    if (!guard()) return;
    String o = String("{\"mode\":") + json(s_ap ? "hotspot" : "wifi") + ",\"network\":" + json(s_name) +
               ",\"ip\":" + json(address()) + ",\"heap\":" + ESP.getFreeHeap() + ",\"saved\":" + json(savedSsid()) +
               ",\"sd\":" + (storage::ok() ? "true" : "false") + ",\"fkey\":" + json(foundry::keyGrouped()) + ",\"apps\":[";
    bool first = true;
    forEachApp([&](App* a) {
        WebCall c;
        c.op = "hello";
        c.arg = [](const char*) { return String(); };
        String out;
        if (a->web(c, out) != 200) return;
        o += (first ? "" : ",") + json(a->name());
        first = false;
    });
    o += "]}";
    reply(200, o, "application/json");
}

static void handleList() {
    String path;
    if (!guard(&path)) return;
    File d = SD.open(path);
    if (!d || !d.isDirectory()) { reply(404, "Not a folder"); return; }
    String o = "[";
    bool first = true;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        if (!first) o += ",";
        first = false;
        o += String("{\"n\":") + json(f.name()) + ",\"d\":" + (f.isDirectory() ? 1 : 0) + ",\"s\":" + (unsigned)f.size() + "}";
        f.close();
    }
    d.close();
    reply(200, o + "]", "application/json");
}

static void handleGet() {
    String path;
    if (!guard(&path)) return;
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) { reply(404, "No such file"); return; }
    s_server->sendHeader("Cache-Control", "no-store");
    if (s_server->hasArg("off")) {
        // part of a file (e.g. the first line of each note)
        uint32_t off = s_server->arg("off").toInt(), len = std::min<uint32_t>(s_server->arg("len").toInt(), 8192);
        f.seek(off);
        String out;
        char buf[512];
        while (len) {
            int got = f.read((uint8_t*)buf, std::min<uint32_t>(len, sizeof(buf)));
            if (got <= 0) break;
            out.concat(buf, got);
            len -= got;
        }
        f.close();
        s_server->send(200, "text/plain; charset=utf-8", out);
        return;
    }
    if (s_server->hasArg("dl")) {
        String name = path.substring(path.lastIndexOf('/') + 1);
        s_server->sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    }
    s_server->send(200, contentType(path), f, f.size());
    f.close();
}

// One record of an SRD .txt: from `off` up to the blank line that ends it.
static void handleRecord() {
    String path;
    if (!guard(&path)) return;
    File f = SD.open(path, FILE_READ);
    if (!f) { reply(404, "No such file"); return; }
    f.seek(s_server->arg("off").toInt());
    String rec;
    char buf[512];
    int end = -1;
    while (end < 0 && rec.length() < 32768) {
        int got = f.read((uint8_t*)buf, sizeof(buf));
        if (got <= 0) break;
        int from = std::max(0, (int)rec.length() - 1);
        rec.concat(buf, got);
        end = rec.indexOf("\n\n", from);
    }
    f.close();
    if (end >= 0) rec.remove(end);
    reply(200, rec, "text/plain; charset=utf-8");
}

static void handlePut() {
    String path;
    if (!guard(&path)) return;
    const String& body = s_server->arg("plain");
    if (body.length() > MAX_TEXT) { reply(413, "Too big for the editor - use upload"); return; }
    makeParents(path);
    String tmp = path + ".tmp";
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) { reply(500, "Can't write"); return; }
    size_t n = f.write((const uint8_t*)body.c_str(), body.length());
    f.close();
    if (n != body.length() || !commit(tmp, path)) { SD.remove(tmp); reply(500, "Write failed (card full?)"); return; }
    reply(200, "saved");
}

static void handleRemove() {
    String path;
    if (!guard(&path)) return;
    if (path == "/gmtools") { reply(403, "Not the root"); return; }
    reply(removeTree(path) ? 200 : 500, "");
}

static void handleMkdir() {
    String path;
    if (!guard(&path)) return;
    makeParents(path + "/");
    reply(SD.exists(path) ? 200 : 500, "");
}

static void handleMove() {
    String from, to;
    if (!guard(&from)) return;
    to = s_server->arg("to");
    if (!safePath(to) || from == "/gmtools") { reply(403, "Path not allowed"); return; }
    if (SD.exists(to)) { reply(409, "Already exists"); return; }
    makeParents(to);
    reply(SD.rename(from, to) ? 200 : 500, "");
}

static File s_upFile;
static String s_upPath;
static bool s_upOk = false;

static void handleUploadChunk() {
    HTTPUpload& up = s_server->upload();
    if (up.status == UPLOAD_FILE_START) {
        String dir = s_server->arg("dir");
        if (dir.endsWith("/")) dir.remove(dir.length() - 1);
        String name = up.filename.substring(up.filename.lastIndexOf('/') + 1);
        s_upPath = dir + "/" + name;
        s_upOk = authed() && storage::ok() && safePath(s_upPath) && name.length() && name.indexOf("..") < 0;
        if (!s_upOk) return;
        makeParents(s_upPath);
        s_upFile = SD.open(s_upPath + ".part", FILE_WRITE);
        s_upOk = (bool)s_upFile;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (s_upOk && s_upFile.write(up.buf, up.currentSize) != up.currentSize) s_upOk = false;
    } else if (up.status == UPLOAD_FILE_END) {
        if (s_upFile) s_upFile.close();
        if (s_upOk) s_upOk = commit(s_upPath + ".part", s_upPath);
        else SD.remove(s_upPath + ".part");
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (s_upFile) s_upFile.close();
        SD.remove(s_upPath + ".part");
        s_upOk = false;
    }
}

static void handleUploadDone() {
    if (!guard()) return;
    reply(s_upOk ? 200 : 500, s_upOk ? "uploaded" : "Upload failed");
}

static void handleLive() {
    if (!guard()) return;
    String init;
    if (storage::ok()) storage::readText("/gmtools/encounter.txt", init, 16384);
    String o = "{\"init\":" + json(init) + ",\"rolls\":[";
    bool first = true;
    for (uint32_t id = s_rollId > MAX_ROLLS ? s_rollId - MAX_ROLLS + 1 : 1; id <= s_rollId; id++) {
        const Roll& r = s_rolls[id % MAX_ROLLS];
        if (!first) o += ",";
        first = false;
        o += String("{\"id\":") + r.id + ",\"title\":" + json(r.title) + ",\"label\":" + json(r.label) +
             ",\"expr\":" + json(r.expr) + ",\"total\":" + r.total + ",\"detail\":" + json(r.breakdown) +
             ",\"nat\":" + r.natural + ",\"crit\":" + (r.crit ? "true" : "false") + "}";
    }
    reply(200, o + "]}", "application/json");
}

// Rolls from the page, made here so they join the shared roll log (and the
// session log). Body: one "label|expr" line per roll; as on the device, a
// natural 20 on the first line doubles the dice of the lines after it.
static void handleRoll() {
    if (!guard()) return;
    const String title = s_server->arg("title");
    String body = s_server->arg("plain"), o = "[";
    bool crit = false, first = true;
    int start = 0, k = 0;
    while (start < (int)body.length()) {
        int nl = body.indexOf('\n', start);
        String l = nl < 0 ? body.substring(start) : body.substring(start, nl);
        start = nl < 0 ? body.length() : nl + 1;
        l.trim();
        if (!l.length()) continue;
        const int bar = l.indexOf('|');
        const String label = bar < 0 ? String("") : l.substring(0, bar);
        String e = bar < 0 ? l : l.substring(bar + 1);
        const bool doubled = k > 0 && crit;
        if (doubled) e = rollui::doubleDice(e);
        dice::Result r = dice::roll(e);
        if (k++ == 0 && r.ok && r.natural == 20) crit = true;
        if (r.ok) {
            logRoll(title, label, r.expr, r.total, r.breakdown(), doubled, r.natural);
            if (settings::cur.logRolls && storage::ok())
                storage::appendLine(storage::sessionLogPath().c_str(),
                                    String(millis() / 1000) + "s  " + title + (label.length() ? " " + label : String("")) + ": " +
                                        r.expr + " = " + r.total + "  " + r.breakdown() + "  (web)");
        }
        o += String(first ? "" : ",") + "{\"label\":" + json(label) + ",\"ok\":" + (r.ok ? "true" : "false") +
             ",\"error\":" + json(r.error) + ",\"expr\":" + json(r.expr) + ",\"total\":" + r.total +
             ",\"detail\":" + json(r.breakdown()) + ",\"nat\":" + r.natural + ",\"crit\":" + (doubled ? "true" : "false") +
             ",\"degree\":" + json(r.hasTarget ? dice::degreeName(r.degree) : "") + "}";
        first = false;
    }
    reply(200, o + "]", "application/json");
}

// An app's own web side: /api/app?app=<name>&op=<op>&... (App::web).
static void handleApp() {
    if (!guard()) return;
    App* a = findApp(s_server->arg("app").c_str());
    if (!a) { reply(404, "No such app"); return; }
    WebCall c;
    c.op = s_server->arg("op");
    c.body = s_server->arg("plain");
    c.arg = [](const char* k) { return s_server->arg(k); };
    String out;
    const int code = a->web(c, out);
    if (!code) { reply(404, "That app has no web side"); return; }
    reply(code, out, out.startsWith("{") || out.startsWith("[") ? "application/json" : "text/plain; charset=utf-8");
}

static void handleWifi() {
    if (!guard()) return;
    String b = s_server->arg("plain");
    int nl = b.indexOf('\n');
    String ssid = nl < 0 ? b : b.substring(0, nl), pass = nl < 0 ? "" : b.substring(nl + 1);
    ssid.trim();
    pass.trim();
    saveNetwork(ssid, pass);
    reply(200, "saved");
}

// Foundry VTT link (core/foundry.h). These routes take the pairing key instead
// of the PIN token, so a reverse proxy can expose just /api/foundry/ to the
// browser running Foundry. CORS is open for setups that call the device directly.
static bool foundryGuard() {
    s_server->sendHeader("Access-Control-Allow-Origin", "*");
    s_server->sendHeader("Cache-Control", "no-store");
    if (foundry::keyOk(s_server->header("X-Link-Key"))) return true;
    s_server->send(401, "text/plain", "Wrong link key");
    return false;
}

static void handleFoundryOptions() {
    s_server->sendHeader("Access-Control-Allow-Origin", "*");
    s_server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    s_server->sendHeader("Access-Control-Allow-Headers", "X-Link-Key, Content-Type");
    s_server->sendHeader("Access-Control-Max-Age", "600");
    s_server->send(204);
}

static void handleFoundryPush() {
    if (!foundryGuard()) return;
    bool ok = foundry::store(s_server->arg("s"), s_server->arg("plain"));
    s_server->send(ok ? 200 : 413, "text/plain", ok ? "ok" : "Unknown section, too big, or the Cardputer is low on memory");
}

static void handleFoundryPoll() {
    if (!foundryGuard()) return;
    s_server->send(200, "text/plain", foundry::takeRequest());
}

static void handlePage() {
    s_server->sendHeader("Content-Encoding", "gzip");
    s_server->sendHeader("Cache-Control", "no-cache");
    s_server->send_P(200, "text/html; charset=utf-8", (const char*)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

static void beginServer() {
    s_server = new WebServer(80);
    static const char* headers[] = {"X-Token", "X-Link-Key"};
    s_server->collectHeaders(headers, 2);
    s_server->on("/", HTTP_GET, handlePage);
    s_server->on("/api/login", HTTP_POST, handleLogin);
    s_server->on("/api/status", HTTP_GET, handleStatus);
    s_server->on("/api/ls", HTTP_GET, handleList);
    s_server->on("/api/file", HTTP_GET, handleGet);
    s_server->on("/api/file", HTTP_POST, handlePut);
    s_server->on("/api/rec", HTTP_GET, handleRecord);
    s_server->on("/api/rm", HTTP_POST, handleRemove);
    s_server->on("/api/mkdir", HTTP_POST, handleMkdir);
    s_server->on("/api/mv", HTTP_POST, handleMove);
    s_server->on("/api/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
    s_server->on("/api/live", HTTP_GET, handleLive);
    s_server->on("/api/wifi", HTTP_POST, handleWifi);
    s_server->on("/api/roll", HTTP_POST, handleRoll);
    s_server->on("/api/app", HTTP_GET, handleApp);
    s_server->on("/api/app", HTTP_POST, handleApp);
    s_server->on("/api/foundry/push", HTTP_POST, handleFoundryPush);
    s_server->on("/api/foundry/poll", HTTP_GET, handleFoundryPoll);
    s_server->on("/api/foundry/push", HTTP_OPTIONS, handleFoundryOptions);
    s_server->on("/api/foundry/poll", HTTP_OPTIONS, handleFoundryOptions);
    s_server->onNotFound([] { reply(404, "Not found"); });
    s_server->begin();
    s_state = RUNNING;
}

// ---------------------------------------------------------------- lifecycle

String savedSsid() {
    Preferences p;
    p.begin("gmtools", true);
    String s = p.getString("wssid", "");
    p.end();
    return s;
}

void saveNetwork(const String& ssid, const String& pass) {
    Preferences p;
    p.begin("gmtools", false);
    p.putString("wssid", ssid);
    p.putString("wpass", pass);
    p.end();
}

static void startHotspot() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char name[24];
    snprintf(name, sizeof(name), "DnD-Toolkit-%02X%02X", mac[4], mac[5]);
    s_name = name;
    s_ap = true;
    WiFi.softAP(s_name.c_str(), s_apPass.c_str());
    beginServer();
}

void start() {
    if (s_state != OFF) return;
    Preferences p;
    p.begin("gmtools", false);
    String ssid = p.getString("wssid", ""), pass = p.getString("wpass", "");
    // the hotspot keeps its password, so phones can remember the network
    s_apPass = p.getString("wap", "");
    if (s_apPass.length() < 8) { s_apPass = randomString("abcdefghjkmnpqrstuvwxyz23456789", 8); p.putString("wap", s_apPass); }
    p.end();
    s_pin = randomString("0123456789", 4);
    s_token = randomString("0123456789abcdef", 32);
    s_requests = 0;
    s_badLogins = 0;
    s_ap = false;
    WiFi.persistent(false);
    if (ssid.length()) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);   // snappier replies; it only runs while wanted
        WiFi.begin(ssid.c_str(), pass.c_str());
        s_name = ssid;
        s_deadline = millis() + JOIN_TIMEOUT_MS;
        s_state = CONNECTING;
    } else {
        startHotspot();
    }
}

void stop() {
    if (s_state == OFF) return;
    if (s_server) { s_server->stop(); delete s_server; s_server = nullptr; }
    if (!s_ap) MDNS.end();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_state = OFF;
    s_token = "";
}

void update() {
    if (s_state == CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            wallclock::syncFromInternet();   // a home network: set the clock while we're at it
            MDNS.begin("dnd");
            MDNS.addService("http", "tcp", 80);
            beginServer();
        } else if (millis() > s_deadline) {
            startHotspot();
        }
    } else if (s_state == RUNNING && s_server) {
        // requests block until served; the server has its own timeouts
        watchdogPause(true);
        s_server->handleClient();
        watchdogPause(false);
    }
}

State state() { return s_state; }
bool apMode() { return s_ap; }
String networkName() { return s_name; }
String apPassword() { return s_apPass; }
String pin() { return s_pin; }
uint32_t requests() { return s_requests; }

String address() {
    if (s_state != RUNNING) return "";
    return "http://" + (s_ap ? WiFi.softAPIP() : WiFi.localIP()).toString();
}

}  // namespace web

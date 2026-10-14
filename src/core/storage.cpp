#include "storage.h"
#include "../board/board.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include "ff.h"
#include "sd_diskio.h"

namespace storage {

static bool   s_ok = false;
static String s_logPath;

static void ensureDir(const char* d) {
    if (!SD.exists(d)) SD.mkdir(d);
}

static bool s_spiStarted = false;

bool begin() {
    const auto& pin = board::sdPins();
    if (!s_spiStarted) { SPI.begin(pin.sck, pin.miso, pin.mosi, pin.cs); s_spiStarted = true; }
    s_ok = SD.begin(pin.cs, SPI, pin.hz);
    if (!s_ok) return false;
    ensureDir("/gmtools");
    ensureDir("/gmtools/tables");
    ensureDir("/gmtools/ref");
    ensureDir("/gmtools/dungeons");
    ensureDir("/gmtools/logs");
    ensureDir("/gmtools/notes");
    ensureDir("/gmtools/campaigns");
    // No RTC on the Cardputer, so number sessions sequentially after the
    // highest existing log (one directory scan instead of an exists() per number,
    // and deleted logs never get their number reused out of order).
    int highest = 0;
    std::vector<String> logs;
    listFiles("/gmtools/logs", logs, ".txt");
    for (auto& f : logs)
        if (f.startsWith("session")) highest = std::max(highest, (int)f.substring(7).toInt());
    char p[40];
    snprintf(p, sizeof(p), "/gmtools/logs/session%03d.txt", highest + 1);
    s_logPath = p;
    return true;
}

bool ok() { return s_ok; }

void end() {
    SD.end();
    s_ok = false;
}

uint64_t cardSizeMB() { return s_ok ? SD.cardSize() / (1024 * 1024) : 0; }

bool format(String& err) {
    end();
    const auto& pin = board::sdPins();
    if (!s_spiStarted) { SPI.begin(pin.sck, pin.miso, pin.mosi, pin.cs); s_spiStarted = true; }
    uint8_t pdrv = sdcard_init(pin.cs, &SPI, pin.hz);
    if (pdrv == 0xFF) { err = "No card detected"; return false; }
    char drv[3] = {(char)('0' + pdrv), ':', 0};
    const size_t WORK = 4096;
    void* work = malloc(WORK);
    if (!work) { sdcard_uninit(pdrv); err = "Out of memory"; return false; }
#if ESP_ARDUINO_VERSION_MAJOR >= 3   // newer FatFs (core 3) takes a parameter struct
    MKFS_PARM fat32 = {FM_FAT32, 0, 0, 0, 0}, any = {FM_ANY, 0, 0, 0, 0};
    FRESULT r = f_mkfs(drv, &fat32, work, WORK);
    if (r == FR_MKFS_ABORTED) r = f_mkfs(drv, &any, work, WORK);  // very small cards
#else
    FRESULT r = f_mkfs(drv, FM_FAT32, 0, work, WORK);
    if (r == FR_MKFS_ABORTED) r = f_mkfs(drv, FM_ANY, 0, work, WORK);  // very small cards
#endif
    free(work);
    sdcard_uninit(pdrv);
    if (r != FR_OK) { err = String("Format failed (") + (int)r + ")"; return false; }
    if (!begin()) { err = "Formatted, but remount failed"; return false; }
    return true;
}
bool exists(const char* path) { return s_ok && SD.exists(path); }
bool remove(const char* path) { return s_ok && SD.remove(path); }
String sessionLogPath() { return s_logPath; }

bool readLines(const char* path, std::vector<String>& out, size_t maxLines) {
    if (!s_ok) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    while (f.available() && out.size() < maxLines) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (l.length() == 0 || l[0] == '#') continue;
        out.push_back(l);
    }
    f.close();
    return true;
}

bool readText(const char* path, String& out, size_t maxBytes) {
    if (!s_ok) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    size_t n = std::min((size_t)f.size(), maxBytes);
    out = "";
    out.reserve(n);
    char buf[512];
    while (out.length() < n) {
        int got = f.read((uint8_t*)buf, std::min(sizeof(buf), n - out.length()));
        if (got <= 0) break;
        out.concat(buf, got);
    }
    f.close();
    return true;
}

bool writeText(const char* path, const String& text) {
    if (!s_ok) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(text);
    f.close();
    return true;
}

bool appendLine(const char* path, const String& line) {
    if (!s_ok || !path || !*path) return false;
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    f.println(line);
    f.close();
    return true;
}

bool listFiles(const char* dir, std::vector<String>& out, const char* ext) {
    if (!s_ok) return false;
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) return false;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        if (!f.isDirectory()) {
            String n = f.name();
            int slash = n.lastIndexOf('/');
            if (slash >= 0) n = n.substring(slash + 1);
            if (!ext || n.endsWith(ext)) out.push_back(n);
        }
        f.close();
    }
    d.close();
    std::sort(out.begin(), out.end(), [](const String& a, const String& b) { return a < b; });
    return true;
}

bool listDirs(const char* dir, std::vector<String>& out) {
    if (!s_ok) return false;
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) return false;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        if (f.isDirectory()) {
            String n = f.name();
            int slash = n.lastIndexOf('/');
            out.push_back(slash >= 0 ? n.substring(slash + 1) : n);
        }
        f.close();
    }
    d.close();
    std::sort(out.begin(), out.end(), [](const String& a, const String& b) { return a < b; });
    return true;
}

bool removeDir(const char* dir) {
    if (!s_ok) return false;
    std::vector<String> files;
    listFiles(dir, files, nullptr);
    for (auto& f : files) SD.remove(String(dir) + "/" + f);
    return SD.rmdir(dir);
}

bool mkdir(const char* dir) { return s_ok && (SD.exists(dir) || SD.mkdir(dir)); }

}  // namespace storage

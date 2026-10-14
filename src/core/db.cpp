#include "db.h"
#include "storage.h"
#include <SD.h>
#include <algorithm>

namespace db {

String unescape(const String& s) {
    String o;
    o.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == '\\' && i + 1 < s.length() && s[i + 1] == 'n') { o += '\n'; i++; }
        else o += s[i];
    }
    return o;
}

String escape(const String& s) {
    String o;
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == '\n') o += "\\n";
        else if (s[i] != '\r') o += s[i];
    }
    return o;
}

String Rec::get(const char* k, const char* def) const {
    for (auto& p : kv) if (p.first == k) return p.second;
    return def;
}
int Rec::num(const char* k, int def) const {
    for (auto& p : kv) if (p.first == k) return p.second.toInt();
    return def;
}
bool Rec::has(const char* k) const {
    for (auto& p : kv) if (p.first == k) return p.second.length() > 0;
    return false;
}
void Rec::set(const char* k, const String& v) {
    for (size_t i = 0; i < kv.size(); i++) {
        if (kv[i].first == k) {
            if (v.length()) kv[i].second = v;
            else kv.erase(kv.begin() + i);
            return;
        }
    }
    if (v.length()) kv.push_back({k, v});
}
String Rec::serialize() const {
    String s;
    // name first so files stay readable
    s += "name: " + get("name") + "\n";
    for (auto& p : kv) if (p.first != "name" && p.second.length()) s += p.first + ": " + escape(p.second) + "\n";
    return s + "\n";
}

static bool parseLine(const String& l, Rec& out) {
    int c = l.indexOf(": ");
    if (c <= 0) return false;
    out.kv.push_back({l.substring(0, c), l.substring(c + 2)});
    return true;
}

bool readRecordAt(const char* path, uint32_t off, Rec& out) {
    out.kv.clear();
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    f.seek(off);
    // read in blocks until the blank line that ends the record
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
    int start = 0;
    while (start < (int)rec.length()) {
        int nl = rec.indexOf('\n', start);
        String l = nl < 0 ? rec.substring(start) : rec.substring(start, nl);
        l.trim();
        if (!l.length()) break;   // tolerate "\r\n\r\n" endings
        parseLine(l, out);
        if (nl < 0) break;
        start = nl + 1;
    }
    return !out.kv.empty();
}

bool findRecord(const char* base, const String& name, Rec& out) {
    File f = SD.open(String(base) + ".idx", FILE_READ);
    if (!f) return false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        int t1 = line.indexOf('\t');
        if (t1 < 0) continue;
        int t2 = line.indexOf('\t', t1 + 1);
        String n = t2 < 0 ? line.substring(t1 + 1) : line.substring(t1 + 1, t2);
        n.trim();
        if (n.equalsIgnoreCase(name)) {
            f.close();
            return readRecordAt((String(base) + ".txt").c_str(), line.substring(0, t1).toInt(), out);
        }
    }
    f.close();
    return false;
}

static char* loadFile(const String& path, size_t& n) {
    File f = SD.open(path, FILE_READ);
    if (!f) return nullptr;
    n = f.size();
    char* buf = new (std::nothrow) char[n + 1];
    if (!buf) { f.close(); return nullptr; }
    f.read((uint8_t*)buf, n);
    buf[n] = 0;
    f.close();
    return buf;
}

void Table::parseIdx(char* buf, uint8_t file, const std::vector<const char*>& keys) {
    // map this file's idx columns to unified columns
    int map[MAX_COLS];
    for (size_t k = 0; k < keys.size() && k < MAX_COLS; k++) {
        map[k] = -1;
        for (size_t c = 0; c < cols_.size(); c++) if (!strcmp(keys[k], cols_[c])) map[k] = c;
    }
    char* p = buf;
    while (*p) {
        char* line = p;
        char* nl = strchr(p, '\n');
        if (nl) { *nl = 0; p = nl + 1; } else p += strlen(p);
        if (!*line) continue;
        E e{};
        e.file = file;
        for (int c = 0; c < MAX_COLS; c++) e.col[c] = "";
        char* field[2 + MAX_COLS];
        int nf = 0;
        field[nf++] = line;
        for (char* q = line; *q && nf < 2 + MAX_COLS; q++) if (*q == '\t') { *q = 0; field[nf++] = q + 1; }
        if (nf < 2) continue;
        e.off = strtoul(field[0], nullptr, 10);
        e.name = field[1];
        for (int k = 0; k + 2 < nf && k < (int)keys.size(); k++) if (map[k] >= 0) e.col[map[k]] = field[k + 2];
        e_.push_back(e);
    }
}

bool Table::readCustomAll(std::vector<Rec>& out, std::vector<uint32_t>* offsets) const {
    out.clear();
    if (offsets) offsets->clear();
    File f = SD.open(custom_, FILE_READ);
    if (!f) return true;
    Rec cur;
    uint32_t pos = 0, recStart = 0;
    auto flush = [&]() {
        if (cur.kv.empty()) return;
        out.push_back(cur);
        if (offsets) offsets->push_back(recStart);
        cur.kv.clear();
    };
    while (f.available()) {
        uint32_t lineStart = pos;
        String l = f.readStringUntil('\n');
        pos += l.length() + 1;
        l.trim();
        if (!l.length()) { flush(); continue; }
        if (cur.kv.empty()) recStart = lineStart;
        parseLine(l, cur);
    }
    flush();
    f.close();
    return true;
}

// Custom entries are matched by file offset, so duplicate names stay distinct.
int Table::customSlot(int i) const {
    if (!isCustom(i)) return -1;
    std::vector<Rec> recs;
    std::vector<uint32_t> offs;
    readCustomAll(recs, &offs);
    for (size_t k = 0; k < offs.size(); k++) if (offs[k] == e_[i].off) return k;
    return -1;
}

void Table::loadCustom() {
    if (!custom_.length() || !storage::exists(custom_.c_str())) return;
    // Build an in-memory idx for the custom file by scanning it.
    File f = SD.open(custom_, FILE_READ);
    if (!f) return;
    String idx;
    Rec cur;
    uint32_t recStart = 0, pos = 0;
    auto flush = [&]() {
        if (cur.kv.empty()) return;
        idx += String(recStart) + "\t" + cur.get("name");
        for (auto c : cols_) idx += String("\t") + cur.get(c);
        idx += "\n";
        cur.kv.clear();
    };
    while (f.available()) {
        uint32_t lineStart = pos;
        String l = f.readStringUntil('\n');
        pos += l.length() + 1;
        l.trim();
        if (!l.length()) { flush(); continue; }
        if (cur.kv.empty()) recStart = lineStart;
        parseLine(l, cur);
    }
    flush();
    f.close();
    char* buf = new (std::nothrow) char[idx.length() + 1];
    if (!buf) return;
    memcpy(buf, idx.c_str(), idx.length() + 1);
    bufs_.emplace_back(buf);
    parseIdx(buf, CUSTOM, cols_);
}

void Table::sortEntries() {
    std::sort(e_.begin(), e_.end(), [](const E& a, const E& b) { return strcasecmp(a.name, b.name) < 0; });
}

bool Table::open(const std::vector<Source>& sources, const char* customPath, const std::vector<const char*>& columns) {
    close();
    src_ = sources;
    custom_ = customPath ? customPath : "";
    cols_ = columns;
    if (cols_.size() > MAX_COLS) cols_.resize(MAX_COLS);   // E::col has room for MAX_COLS
    for (size_t i = 0; i < src_.size(); i++) {
        size_t n;
        char* buf = loadFile(String(src_[i].base) + ".idx", n);
        if (!buf) continue;
        bufs_.emplace_back(buf);
        parseIdx(buf, (uint8_t)i, src_[i].idxKeys);
    }
    loadCustom();
    sortEntries();
    open_ = true;
    return !e_.empty();
}

void Table::close() {
    e_.clear();
    e_.shrink_to_fit();
    bufs_.clear();
    open_ = false;
}

const char* Table::col(int i, const char* column) const {
    for (size_t c = 0; c < cols_.size(); c++) if (!strcmp(cols_[c], column)) return e_[i].col[c];
    return "";
}

int Table::find(const String& name) const {
    for (int i = 0; i < (int)e_.size(); i++) if (name.equalsIgnoreCase(e_[i].name)) return i;
    return -1;
}

bool Table::read(int i, Rec& out) const {
    const E& e = e_[i];
    String path = e.file == CUSTOM ? custom_ : String(src_[e.file].base) + ".txt";
    return readRecordAt(path.c_str(), e.off, out);
}

bool Table::rewriteCustom(const std::vector<Rec>& recs) {
    String all;
    for (auto& r : recs) all += r.serialize();
    String dir = custom_.substring(0, custom_.lastIndexOf('/'));
    if (!SD.exists(dir)) SD.mkdir(dir);
    return storage::writeText(custom_.c_str(), all);
}

static int reopenAndFind(Table& t, const std::vector<Source>& s, const String& custom,
                         const std::vector<const char*>& cols, const String& name) {
    t.open(s, custom.c_str(), cols);
    for (int i = 0; i < t.size(); i++) if (t.isCustom(i) && name.equalsIgnoreCase(t.at(i).name)) return i;
    return -1;
}

int Table::add(const Rec& r) {
    if (!custom_.length() || !storage::ok()) return -1;
    std::vector<Rec> recs;
    readCustomAll(recs);
    recs.push_back(r);
    if (!rewriteCustom(recs)) return -1;
    auto s = src_; auto c = custom_; auto cols = cols_;
    return reopenAndFind(*this, s, c, cols, r.name());
}

int Table::update(int i, const Rec& r) {
    int slot = customSlot(i);
    if (slot < 0) return -1;
    std::vector<Rec> recs;
    readCustomAll(recs);
    recs[slot] = r;
    if (!rewriteCustom(recs)) return -1;
    auto s = src_; auto c = custom_; auto cols = cols_;
    return reopenAndFind(*this, s, c, cols, r.name());
}

bool Table::remove(int i) {
    int slot = customSlot(i);
    if (slot < 0) return false;
    std::vector<Rec> recs;
    readCustomAll(recs);
    recs.erase(recs.begin() + slot);
    if (!rewriteCustom(recs)) return false;
    auto s = src_; auto c = custom_; auto cols = cols_;
    open(s, c.c_str(), cols);
    return true;
}

}  // namespace db

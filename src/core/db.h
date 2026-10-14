#pragma once
#include <Arduino.h>
#include <memory>
#include <vector>

// Read-only SRD tables plus a user "custom" table, merged and sorted by name.
//
// Data files (made by tools/srd_extract.py) are records of "key: value" lines
// separated by blank lines; text fields store paragraph breaks as "\n" escapes.
// Each SRD file has an .idx with "offset<TAB>name<TAB>fields..." so only names
// and a few columns are held in RAM. Full records are read from SD on demand.
namespace db {

String unescape(const String& s);   // "\\n" -> newline
String escape(const String& s);     // newline -> "\\n"

struct Rec {
    std::vector<std::pair<String, String>> kv;
    String get(const char* k, const char* def = "") const;
    int num(const char* k, int def = 0) const;
    bool has(const char* k) const;
    void set(const char* k, const String& v);  // empty v removes the key
    String name() const { return get("name"); }
    String serialize() const;
};

struct Source {
    const char* base;                    // e.g. "/gmtools/srd/spells" (no extension)
    std::vector<const char*> idxKeys;    // column names in the .idx after the name
};

class Table {
public:
    static constexpr int MAX_COLS = 8;
    struct E {
        uint32_t off;
        const char* name;
        const char* col[MAX_COLS];
        uint8_t file;   // index into sources, or CUSTOM
    };
    static constexpr uint8_t CUSTOM = 0xFF;

    // columns: the unified column names every entry exposes via col().
    bool open(const std::vector<Source>& sources, const char* customPath, const std::vector<const char*>& columns);
    void close();
    bool isOpen() const { return open_; }

    int size() const { return (int)e_.size(); }
    const E& at(int i) const { return e_[i]; }
    const char* col(int i, const char* column) const;   // "" if missing
    bool isCustom(int i) const { return e_[i].file == CUSTOM; }
    int find(const String& name) const;                 // -1 if absent (case-insensitive)

    bool read(int i, Rec& out) const;
    // Custom entries (stored in customPath). Return the new index or -1.
    int add(const Rec& r);
    int update(int i, const Rec& r);
    bool remove(int i);

private:
    bool open_ = false;
    std::vector<Source> src_;
    String custom_;
    std::vector<const char*> cols_;
    std::vector<std::unique_ptr<char[]>> bufs_;
    std::vector<E> e_;

    void parseIdx(char* buf, uint8_t file, const std::vector<const char*>& keys);
    void loadCustom();
    void sortEntries();
    bool rewriteCustom(const std::vector<Rec>& recs);
    bool readCustomAll(std::vector<Rec>& out, std::vector<uint32_t>* offsets = nullptr) const;
    int customSlot(int i) const;   // position of entry i within the custom file
};

// Parse "key: value" lines until a blank line from an open file stream position.
bool readRecordAt(const char* path, uint32_t off, Rec& out);
// Look up one record by name via "<base>.idx" (e.g. "/gmtools/srd/glossary").
bool findRecord(const char* base, const String& name, Rec& out);

}  // namespace db

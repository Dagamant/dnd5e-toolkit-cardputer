#pragma once
#include "input.h"
#include <Arduino.h>
#include <functional>
#include <vector>

// Step-by-step form ("wizard") used by the item, spell and character builders.
//
// Each step asks for one value. Steps can be hidden based on earlier answers
// (`when`). After the last step a review screen lists everything; Enter there
// finishes, Esc goes back. Esc on the first step cancels.
class Wizard {
public:
    enum Type { TEXT, LONGTEXT, NUMBER, DICE, CHOICE, MULTI, YESNO };
    struct Step {
        const char* key;
        String label;
        Type type = TEXT;
        std::vector<String> options;                       // CHOICE / MULTI
        String hint;
        bool optional = false;
        int maxLen = 32;
        std::function<bool(const Wizard&)> when;           // show this step?
        std::function<String(const String&)> preview;      // CHOICE: text shown beside the list
        std::function<std::vector<String>(const Wizard&)> dynOptions;  // options computed from answers
        int pick = 0;                                      // MULTI: max selections (0 = any)
        std::function<void(Wizard&)> onCommit;             // runs after the value is accepted
        std::function<int(const Wizard&)> dynPick;         // MULTI: computed exact pick count
        std::function<String(const Wizard&)> validate;     // non-empty = error message
    };

    void begin(const String& title, const std::vector<Step>& steps, uint16_t accent);
    void preset(const char* key, const String& value);    // pre-fill (for editing)
    bool active() const { return active_; }
    // True once after the review screen is confirmed / the wizard is cancelled.
    bool finished() { bool f = finished_; finished_ = false; return f; }
    bool cancelled() { bool c = cancelled_; cancelled_ = false; return c; }

    bool handle(const input::Event& e);
    void draw();

    String get(const char* key) const;
    bool is(const char* key, const char* value) const { return get(key) == value; }
    bool yes(const char* key) const { return get(key) == "Yes"; }
    bool has(const char* key) const { return get(key).length() > 0; }
    bool contains(const char* key, const char* item) const;  // MULTI contains item
    void set(const char* key, const String& v);

private:
    String title_;
    uint16_t accent_ = 0;
    std::vector<Step> steps_;
    std::vector<std::pair<String, String>> vals_;
    std::vector<String> opts_;      // options for the current step
    int idx_ = 0;
    bool review_ = false, active_ = false, finished_ = false, cancelled_ = false;
    String edit_;                   // text being edited
    int sel_ = 0, top_ = 0, reviewTop_ = 0;
    std::vector<bool> checks_;
    bool otherMode_ = false;        // typing a custom value for an "Other..." choice

    bool visible(int i) const;
    void enter(int i);
    void next();
    void back();
    bool commit();
    void drawChoice(const Step& s, int y);
    void drawReview();
};

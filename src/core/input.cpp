#include "input.h"
#include "../board/board.h"

namespace input {

static constexpr int QSIZE = 64;
static Event    s_queue[QSIZE];
static int      s_head = 0, s_tail = 0;
static bool     s_textMode = false;
static uint32_t s_lastActivity = 0;

// previous frame's held keys (board-specific ids)
static constexpr int MAXKEYS = 12;
static uint16_t s_prev[MAXKEYS];
static int      s_prevN = 0;
static constexpr uint16_t NO_KEY = 0xFFFF;
static uint16_t s_repeatKey = NO_KEY;
static uint32_t s_repeatAt = 0;

static void push(const Event& e) {
    int next = (s_tail + 1) % QSIZE;
    if (next == s_head) return;  // full, drop
    s_queue[s_tail] = e;
    s_tail = next;
}

void begin() { s_lastActivity = millis(); }
void setTextMode(bool on) { s_textMode = on; }
bool textMode() { return s_textMode; }
uint32_t lastActivity() { return s_lastActivity; }

static void emit(uint16_t id, bool repeat) {
    Event e;
    if (!board::keyEvent(id, s_textMode, e)) return;   // modifier keys
    e.repeat = repeat;
    push(e);
}

void update() {
    uint16_t cur[MAXKEYS];
    int curN = board::keysDown(cur, MAXKEYS);

    uint32_t now = millis();
    bool anyNew = false;
    for (int i = 0; i < curN; i++) {
        bool wasDown = false;
        for (int j = 0; j < s_prevN; j++) if (s_prev[j] == cur[i]) { wasDown = true; break; }
        if (!wasDown) {
            emit(cur[i], false);
            s_repeatKey = board::keyRepeats(cur[i]) ? cur[i] : NO_KEY;
            s_repeatAt = now + 380;
            anyNew = true;
        }
    }
    if (curN) s_lastActivity = now;

    // auto-repeat for the most recently pressed key while it stays held
    if (!anyNew && s_repeatKey != NO_KEY) {
        bool held = false;
        for (int i = 0; i < curN; i++) if (cur[i] == s_repeatKey) { held = true; break; }
        if (!held) s_repeatKey = NO_KEY;
        else if (now >= s_repeatAt) {
            emit(s_repeatKey, true);
            s_repeatAt = now + 70;
        }
    }

    memcpy(s_prev, cur, curN * sizeof(uint16_t));
    s_prevN = curN;
}

void inject(const Event& e) {
    s_lastActivity = millis();
    push(e);
}

bool poll(Event& e) {
    if (s_head == s_tail) return false;
    e = s_queue[s_head];
    s_head = (s_head + 1) % QSIZE;
    return true;
}

}  // namespace input

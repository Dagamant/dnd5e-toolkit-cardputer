#include "sfx.h"
#include "rng.h"
#include "settings.h"
#include <M5Unified.h>

namespace sfx {

struct Note { uint16_t freq; uint16_t ms; };
static Note     s_seq[24];
static int      s_len = 0, s_pos = 0;
static uint32_t s_nextAt = 0;

static void play(const Note* notes, int n) {
    if (!settings::cur.sound) return;
    if (n > 24) n = 24;
    memcpy(s_seq, notes, n * sizeof(Note));
    s_len = n;
    s_pos = 0;
    s_nextAt = 0;
}

void begin() { M5.Speaker.begin(); }

void update() {
    if (s_pos >= s_len) return;
    uint32_t now = millis();
    if (now < s_nextAt) return;
    const Note& n = s_seq[s_pos++];
    if (n.freq) M5.Speaker.tone(n.freq, n.ms);
    s_nextAt = now + n.ms + 4;
}

static void one(uint16_t f, uint16_t ms) { Note n{f, ms}; play(&n, 1); }

void click()  { one(2400, 8); }
void select() { const Note n[] = {{1600, 25}, {2400, 35}}; play(n, 2); }
void back()   { const Note n[] = {{1400, 25}, {900, 35}}; play(n, 2); }
void error()  { const Note n[] = {{220, 80}, {0, 30}, {180, 120}}; play(n, 3); }

void diceRattle() {
    Note n[14];
    for (int i = 0; i < 14; i++) {
        n[i].freq = (i % 2) ? 0 : (uint16_t)(2500 + rng::below(3500));
        n[i].ms = (i % 2) ? (uint16_t)(20 + i * 6) : 6;
    }
    play(n, 14);
}
void land() { const Note n[] = {{3800, 6}, {0, 30}, {3000, 5}}; play(n, 3); }
void crit() {
    const Note n[] = {{523, 70}, {659, 70}, {784, 70}, {1047, 220}};
    play(n, 4);
}
void fumble() {
    const Note n[] = {{392, 120}, {330, 120}, {262, 120}, {196, 300}};
    play(n, 4);
}
void alarm() {
    const Note n[] = {{1760, 90}, {0, 60}, {1760, 90}, {0, 60}, {1760, 160}};
    play(n, 5);
}

}  // namespace sfx

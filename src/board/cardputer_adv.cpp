// Board support: M5Stack Cardputer / Cardputer ADV.
#ifdef BOARD_CARDPUTER
#include "board.h"
#include "keyboard_adv.h"
#include <M5Cardputer.h>

namespace board {

const char* name() { return "Cardputer"; }

void begin() {
    auto cfg = M5.config();
    // Keyboard is started manually so the ADV can use the polling reader.
    M5Cardputer.begin(cfg, false);
    if (M5.getBoard() == m5::board_t::board_M5CardputerADV)
        M5Cardputer.Keyboard.begin(std::unique_ptr<KeyboardReader>(new PollingTCA8418Reader()));
    else
        M5Cardputer.Keyboard.begin();
}

void update() {
    M5Cardputer.update();
    M5Cardputer.Keyboard.updateKeyList();
    M5Cardputer.Keyboard.updateKeysState();
}

// Key ids pack the matrix position: x | y << 4.
static Point2D_t pos(uint16_t id) { return Point2D_t{(int)(id & 0x0F), (int)(id >> 4)}; }

int keysDown(uint16_t* ids, int max) {
    int n = 0;
    for (const auto& k : M5Cardputer.Keyboard.keyList()) {
        if (n >= max) break;
        ids[n++] = (uint16_t)((k.x & 0x0F) | ((k.y & 0x0F) << 4));
    }
    return n;
}

static bool isModifier(char c) {
    return c == (char)KEY_FN || c == (char)KEY_OPT || c == (char)KEY_LEFT_CTRL ||
           c == (char)KEY_LEFT_SHIFT || c == (char)KEY_LEFT_ALT;
}

bool keyEvent(uint16_t id, bool textMode, input::Event& e) {
    auto& kb = M5Cardputer.Keyboard;
    auto& ks = kb.keysState();
    KeyValue_t kv = kb.getKeyValue(pos(id));
    char base = kv.value_first;
    if (isModifier(base)) return false;
    e = input::Event();
    e.base = base;
    e.fn = ks.fn; e.shift = ks.shift; e.ctrl = ks.ctrl; e.opt = ks.opt; e.alt = ks.alt;
    e.ch = (ks.shift || kb.capslocked()) ? kv.value_second : kv.value_first;
    // The printed arrows ; . , / are arrows in menus; in text entry hold Fn.
    bool arrows = !textMode || ks.fn;
    if (base == (char)KEY_ENTER)          e.key = input::K_ENTER;
    else if (base == (char)KEY_BACKSPACE) e.key = input::K_DEL;
    else if (base == (char)KEY_TAB)       e.key = input::K_TAB;
    else if (base == '`')                 e.key = input::K_BACK;
    else if (arrows && base == ';')       e.key = input::K_UP;
    else if (arrows && base == '.')       e.key = input::K_DOWN;
    else if (arrows && base == ',')       e.key = input::K_LEFT;
    else if (arrows && base == '/')       e.key = input::K_RIGHT;
    else                                  e.key = input::K_CHAR;
    return true;
}

bool keyRepeats(uint16_t id) {
    char base = M5Cardputer.Keyboard.getKeyValue(pos(id)).value_first;
    return !(base == (char)KEY_ENTER || base == (char)KEY_TAB || base == '`');
}

const char* fnName() { return "Fn"; }

const SdPins& sdPins() {
    static const SdPins p{40, 39, 14, 12, 20000000};
    return p;
}

uint8_t displayRotation() { return 1; }

bool bigScreen() { return false; }

void makeCanvas(M5Canvas& c, bool) {
    c.setColorDepth(16);
    c.createSprite(240, 135);
}

bool fill(M5Canvas&, int, int, int, int, uint16_t) { return false; }

void present(M5Canvas& frame) { frame.pushSprite(0, 0); }

}  // namespace board
#endif

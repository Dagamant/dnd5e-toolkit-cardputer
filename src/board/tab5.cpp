// Board support: M5Stack Tab5 with the Tab5 Keyboard (Phase 1: the 240x135
// UI drawn 5x into 1200x675, centred). See docs/TAB5_PORT.md.
#ifdef BOARD_TAB5
#include "board.h"
#include "../core/touch.h"
#include <M5UnitUnified.h>
#include <M5UnitUnifiedKEYBOARD.h>
#include <esp_cache.h>
#include <driver/ppa.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

namespace board {

namespace kb = m5::unit::tab5_keyboard;

static m5::unit::UnitUnified s_units;
static m5::unit::UnitTab5Keyboard s_kb;
static bool s_kbOk = false;     // it started at power-on
static bool s_kbHere = false;   // and still answers
static constexpr int ZOOM = 5;

const char* name() { return "Tab5"; }

static bool startKeyboard() {
    auto cfg = s_kb.config();
    cfg.mode = kb::Mode::Normal;   // press/release per key: input.cpp does the auto-repeat
    cfg.software_repeat = false;
    s_kb.config(cfg);
    // Tab5 Keyboard on ExtPort1: SDA G0, SCL G1 (INT G50, set in the driver's defaults)
    Wire.end();
    Wire.begin(0, 1, s_kb.component_config().clock);
    return s_units.add(s_kb, Wire) && s_units.begin();
}

void begin() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(displayRotation());
    M5.Display.fillScreen(TFT_BLACK);   // the border around the scaled frame
    s_kbOk = startKeyboard();
    s_kbHere = s_kbOk;
}

// Touch: a tap is sent with its position and the screen under it decides
// what it means (touch::tap). Dragging scrolls: a scroller under the finger
// (touch::scroller) follows it and coasts after a flick; elsewhere it is one
// Up/Down per row's worth of movement, finger up = further down the list.
// A sideways swipe is Left/Right (next tab, page or panel).
static void touch() {
    static int s_x0 = 0, s_y0 = 0, s_lastY = 0, s_dragY = 0;
    static bool s_moved = false, s_swiped = false, s_repeated = false, s_scrolled = false, s_waking = false;
    static uint32_t s_downAt = 0;
    constexpr int SLOP = 24, STEP = 40, SWIPE = 140;
    if (!M5.Touch.isEnabled()) return;
    auto t = M5.Touch.getDetail();
    input::Event e;
    e.gesture = true;
    // a touch on the dark screen just wakes it: nothing under the finger reacts
    if (t.wasPressed() && input::asleep()) s_waking = true;
    if (s_waking) {
        input::poke();
        if (t.wasReleased() || !t.isPressed()) s_waking = false;
        return;
    }
    if (t.wasPressed()) {
        s_x0 = t.x; s_y0 = s_lastY = s_dragY = t.y;
        s_moved = s_swiped = s_repeated = s_scrolled = false;
        s_downAt = millis();
        touch::press();
    }
    if (t.isPressed()) {
        int dx = t.x - s_x0, dy = t.y - s_y0;
        if (!s_moved && (abs(dx) > SLOP || abs(dy) > SLOP)) { s_moved = true; s_dragY = t.y; }
        // a finger held still on a stepper repeats it
        if (!s_moved && touch::hold(s_x0, s_y0, millis() - s_downAt)) s_repeated = true;
        if (s_moved && !s_swiped && (s_scrolled || abs(dy) >= abs(dx))) {
            // finger up = content up = further down: the offset grows
            if (t.y != s_dragY && touch::drag(s_x0, s_y0, s_dragY - t.y)) { s_scrolled = true; input::poke(); }
            s_dragY = t.y;
            while (!s_scrolled && abs(t.y - s_lastY) >= STEP) {
                bool up = t.y < s_lastY;
                e.key = up ? input::K_DOWN : input::K_UP;
                input::inject(e);
                s_lastY += up ? -STEP : STEP;
            }
        } else if (s_moved && !s_swiped && abs(dx) >= SWIPE && abs(dy) < STEP) {
            e.key = dx < 0 ? input::K_RIGHT : input::K_LEFT;
            input::inject(e);
            s_swiped = true;
        }
    }
    if (t.wasReleased() && s_scrolled) touch::release();
    if (t.wasReleased() && !s_moved && !s_repeated) {
        e.key = input::K_TAP;
        e.x = s_x0;
        e.y = s_y0;
        input::inject(e);
    }
}

// Is the keyboard there? An I2C probe every two seconds.
static bool s_pretend = false;
void pretendNoKeyboard(bool on) { s_pretend = on; }
bool pretendingNoKeyboard() { return s_pretend; }
bool hasKeyboard() { return s_kbOk && s_kbHere && !s_pretend; }
// The LEDs: in "bind" mode the keyboard drives them itself (right: its
// mode, left: modifiers) at a brightness we set; "custom" mode with both
// set to black turns them off.
static uint8_t s_lights = 2;
static void applyLights() {
    if (!s_kbOk || !s_kbHere) return;
    if (s_lights == 0) {
        s_kb.writeRgbMode(kb::RgbMode::Custom);
        s_kb.writeRgb(0, 0, 0, 0);
        s_kb.writeRgb(1, 0, 0, 0);
    } else {
        s_kb.writeRgbMode(kb::RgbMode::Bind);
        s_kb.writeBrightness(s_lights == 1 ? 3 : 20);
    }
}
void keyboardLights(uint8_t level) { s_lights = level; applyLights(); }

static void checkKeyboard() {
    static uint32_t s_next = 0;
    if (!s_kbOk || millis() < s_next) return;
    s_next = millis() + 2000;
    const bool was = s_kbHere;
    Wire.beginTransmission(s_kb.address());
    s_kbHere = Wire.endTransmission() == 0;
    if (s_kbHere && !was) applyLights();   // plugged back in: it forgot
}

void update() {
    M5.update();
    checkKeyboard();
    if (hasKeyboard()) s_units.update();
    touch();
}

int keysDown(uint16_t* ids, int max) {
    if (!hasKeyboard()) return 0;
    int n = 0;
    for (uint8_t k = 0; k < kb::KEY_COUNT && n < max; k++)
        if (s_kb.isPressed(k)) ids[n++] = k;
    return n;
}

static bool isModifier(uint16_t id) {
    return id == kb::KIDX_SYM || id == kb::KIDX_AA || id == kb::KIDX_CTRL || id == kb::KIDX_ALT;
}

static uint8_t hidCode(uint16_t id) {
    uint8_t row = id / kb::KEY_COL_COUNT, col = id % kb::KEY_COL_COUNT;
    return (s_kb.isSym() ? kb::keyMatrixToHidSym(row, col) : kb::keyMatrixToHidBase(row, col)).keycode;
}

bool keyEvent(uint16_t id, bool textMode, input::Event& e) {
    if (isModifier(id)) return false;
    uint8_t row = id / kb::KEY_COL_COUNT, col = id % kb::KEY_COL_COUNT;
    e = input::Event();
    // Ctrl is this board's "Fn" (hints say Ctrl+X); Aa is Shift, Sym the symbol layer
    e.fn = e.ctrl = s_kb.isCtrl();
    e.alt = s_kb.isAlt();
    e.opt = s_kb.isSym();
    e.shift = s_kb.isAa();
    e.ch = s_kb.keyMatrixToChar(row, col);
    auto plain = kb::keyMatrixToHidBase(row, col);
    e.base = kb::hidUsageToChar(plain.keycode, plain.modifier);
    switch (hidCode(id)) {
        case 0x28: e.key = input::K_ENTER; break;
        case 0x29: e.key = input::K_BACK; break;
        case 0x2A: case 0x4C: e.key = input::K_DEL; break;
        case 0x2B: e.key = input::K_TAB; break;
        case 0x52: e.key = input::K_UP; break;
        case 0x51: e.key = input::K_DOWN; break;
        case 0x50: e.key = input::K_LEFT; break;
        case 0x4F: e.key = input::K_RIGHT; break;
        default:
            if (!e.ch) return false;
            e.key = input::K_CHAR;
    }
    (void)textMode;   // real arrow keys: no text-mode switching needed
    return true;
}

bool keyRepeats(uint16_t id) {
    uint8_t c = hidCode(id);
    return !(c == 0x28 || c == 0x29 || c == 0x2B);
}

const char* fnName() { return "Ctrl"; }

// microSD slot over SPI
const SdPins& sdPins() {
    static const SdPins p{43, 39, 44, 42, 20000000};
    return p;
}

// landscape with the keyboard below
uint8_t displayRotation() { return 3; }

// The panel is portrait underneath (720x1280, RGB565 little-endian), so a
// landscape row is a panel column. Landscape rotation 3 maps (x, y) to panel
// (y, 1279 - x). Frames are copied straight into the framebuffer, then written
// back from the cache once; M5GFX's own pushImage (which converts and rotates
// pixel by pixel) took ~80 ms a frame.
static constexpr int PW = 720, PH = 1280;
static bool s_valid = false;   // the framebuffer holds the last frame of this canvas kind

static uint16_t* framebuffer() {
    return (uint16_t*)static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel())->config_detail().buffer;
}

static void flush(uint16_t* fb, int firstRow, int rows) {
    esp_cache_msync(fb + (size_t)firstRow * PW, (size_t)rows * PW * 2,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

bool bigScreen() { return true; }

void makeCanvas(M5Canvas& c, bool wide) {
    c.deleteSprite();
    c.setColorDepth(16);
    c.setPsram(wide);
    if (wide) {
        // landscape 1280x720, cache-line aligned for the PPA; kept for the whole run
        static void* buf = heap_caps_aligned_calloc(64, (size_t)PW * PH, 2, MALLOC_CAP_SPIRAM);
        if (buf) c.setBuffer(buf, PH, PW, 16);
        else c.createSprite(PH, PW);
    } else {
        c.createSprite(240, 135);
    }
    // the other kind of frame was on screen: start from black
    if (auto* fb = framebuffer()) { memset(fb, 0, (size_t)PW * PH * 2); flush(fb, 0, PH); }
    s_valid = false;
}

// Big canvas: the P4's pixel-processing accelerator (PPA) rotates it into the
// portrait framebuffer and byte-swaps it by DMA. Drawing stays in plain
// landscape rows, which is much faster than drawing into a rotated canvas.
static ppa_client_handle_t client(ppa_operation_t type) {
    static ppa_client_handle_t h[PPA_OPERATION_INVALID] = {};
    if (!h[type]) {
        ppa_client_config_t cfg = {};
        cfg.oper_type = type;
        if (ppa_register_client(&cfg, &h[type]) != ESP_OK) h[type] = nullptr;
    }
    return h[type];
}

bool fill(M5Canvas& c, int x, int y, int w, int h, uint16_t color) {
    auto* ppa = client(PPA_OPERATION_FILL);
    if (!ppa || c.width() != PH) return false;
    ppa_fill_oper_config_t op = {};
    op.out.buffer = c.getBuffer();
    op.out.buffer_size = (size_t)PW * PH * 2;
    op.out.pic_w = PH;
    op.out.pic_h = PW;
    op.out.block_offset_x = x;
    op.out.block_offset_y = y;
    op.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
    op.fill_block_w = w;
    op.fill_block_h = h;
    // the canvas stores RGB565 big-endian: give the PPA the swapped value
    uint16_t le = __builtin_bswap16(color);
    op.fill_argb_color.val = ((le >> 11) & 0x1F) << 19 | ((le >> 5) & 0x3F) << 10 | (le & 0x1F) << 3 | 0xFF000000;
    op.mode = PPA_TRANS_MODE_BLOCKING;
    return ppa_do_fill(ppa, &op) == ESP_OK;
}

static void presentWide(M5Canvas& c, uint16_t* fb) {
    auto* ppa = client(PPA_OPERATION_SRM);
    if (!ppa) return;
    ppa_srm_oper_config_t op = {};
    op.in.buffer = c.getBuffer();
    op.in.pic_w = op.in.block_w = PH;
    op.in.pic_h = op.in.block_h = PW;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = fb;
    op.out.buffer_size = (size_t)PW * PH * 2;
    op.out.pic_w = PW;
    op.out.pic_h = PH;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;   // counter-clockwise: (x, y) -> (y, 1279 - x)
    op.scale_x = op.scale_y = 1.0f;
    op.byte_swap = true;                             // the canvas stores RGB565 big-endian
    op.mode = PPA_TRANS_MODE_BLOCKING;
    ppa_do_scale_rotate_mirror(ppa, &op);
    s_valid = true;
}

// Classic canvas: each canvas column is scaled into one 720-px panel row and
// copied into the framebuffer 5 times; unchanged columns are skipped.
static void presentClassic(M5Canvas& c, uint16_t* fb) {
    const int w = c.width(), h = c.height();
    const int x0 = (PH - w * ZOOM) / 2, y0 = (PW - h * ZOOM) / 2;
    static uint16_t* row = (uint16_t*)heap_caps_calloc(PW, 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    static uint16_t* prev = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!row || !prev) return;
    const uint16_t* src = (const uint16_t*)c.getBuffer();   // byte-swapped RGB565
    int lo = w, hi = -1;
    for (int x = 0; x < w; x++) {
        bool same = s_valid;
        for (int y = 0; y < h && same; y++) same = src[y * w + x] == prev[y * w + x];
        if (same) continue;
        lo = std::min(lo, x); hi = x;
        for (int y = 0; y < h; y++) {
            uint16_t px = prev[y * w + x] = src[y * w + x];
            px = __builtin_bswap16(px);
            uint16_t* o = row + y0 + y * ZOOM;
            for (int k = 0; k < ZOOM; k++) o[k] = px;
        }
        uint16_t* dst = fb + (size_t)(PH - (x0 + (x + 1) * ZOOM)) * PW;
        for (int k = 0; k < ZOOM; k++) memcpy(dst + k * PW, row, PW * 2);
    }
    s_valid = true;
    // columns lo..hi are panel rows PH - (x0 + (hi + 1) * ZOOM) .. PH - (x0 + lo * ZOOM) - 1
    if (hi >= 0) flush(fb, PH - (x0 + (hi + 1) * ZOOM), (hi - lo + 1) * ZOOM);
}

void presentRect(M5Canvas& c, int x, int y, int w, int h) {
    uint16_t* fb = framebuffer();
    auto* ppa = client(PPA_OPERATION_SRM);
    if (!fb || !ppa || !s_valid || c.width() != PH) { present(c); return; }
    // keep the block inside the canvas, in even sizes
    x = std::max(0, x) & ~1; y = std::max(0, y) & ~1;
    w = (std::min(PH, x + w) - x + 1) & ~1; h = (std::min(PW, y + h) - y + 1) & ~1;
    if (w <= 0 || h <= 0) return;
    ppa_srm_oper_config_t op = {};
    op.in.buffer = c.getBuffer();
    op.in.pic_w = PH;
    op.in.pic_h = PW;
    op.in.block_w = w;
    op.in.block_h = h;
    op.in.block_offset_x = x;
    op.in.block_offset_y = y;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = fb;
    op.out.buffer_size = (size_t)PW * PH * 2;
    op.out.pic_w = PW;
    op.out.pic_h = PH;
    op.out.block_offset_x = y;            // (x, y) -> (y, 1279 - x), as in presentWide
    op.out.block_offset_y = PH - x - w;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
    op.scale_x = op.scale_y = 1.0f;
    op.byte_swap = true;
    op.mode = PPA_TRANS_MODE_BLOCKING;
    ppa_do_scale_rotate_mirror(ppa, &op);
}

void present(M5Canvas& c) {
    uint16_t* fb = framebuffer();
    if (!fb || !c.getBuffer()) return;
    if (c.width() * c.height() == PW * PH) presentWide(c, fb);
    else presentClassic(c, fb);
}

}  // namespace board
#endif

#pragma once
#include <M5Cardputer.h>
#include <memory>
#include "utility/Adafruit_TCA8418/Adafruit_TCA8418.h"
#include "utility/Adafruit_TCA8418/Adafruit_TCA8418_registers.h"

// Replacement keyboard reader for the Cardputer ADV (TCA8418 key controller).
//
// The stock M5Cardputer reader only reads the chip's event FIFO while its
// interrupt flag is set, one event per update. A key-release event can be
// left sitting in the FIFO, so the key looks held forever (and auto-repeats)
// until some other key is pressed. This reader polls the FIFO count every
// frame and drains every pending event, so presses and releases never get stuck.
class PollingTCA8418Reader : public KeyboardReader {
public:
    void begin() override {
        _tca = std::make_unique<Adafruit_TCA8418>();
        if (!_tca->begin()) return;
        _tca->matrix(7, 8);
        _tca->flush();
        _ok = true;
    }

    void update() override {
        if (!_ok) return;
        for (int guard = 0; guard < 16; guard++) {
            if (_tca->available() == 0) break;
            handle(_tca->getEvent());
        }
        // clear any latched interrupt status so the chip keeps queueing
        _tca->writeRegister8(TCA8418_REG_INT_STAT, 0x03);
    }

private:
    std::unique_ptr<Adafruit_TCA8418> _tca;
    bool _ok = false;

    void handle(uint8_t ev) {
        uint8_t code = ev & 0x7F;
        if (code == 0 || code > 80) return;  // empty, or a GPIO event
        bool pressed = ev & 0x80;
        uint8_t idx = code - 1;
        uint8_t row = idx / 10, col = idx % 10;
        // remap the TCA8418 matrix to the original Cardputer key grid
        Point2D_t p;
        p.x = row * 2 + (col > 3 ? 1 : 0);
        p.y = col % 4;
        if (p.x > 13 || p.y > 3) return;
        auto it = std::find(_key_list.begin(), _key_list.end(), p);
        if (pressed) { if (it == _key_list.end()) _key_list.push_back(p); }
        else if (it != _key_list.end()) _key_list.erase(it);
    }
};

// transition.cpp
#include "transition.h"
#include "hal/hal_display.h"
#include "config.h"
#include "UI/theme.h"
#include <Arduino.h>

// Two 240x320 RGB565 buffers; allocation falls back to normal heap on Pi 5.
static uint16_t *_buf_old = nullptr;
static TransType _type = TRANS_NONE;
static int _step = 0;
static const int STEPS = 12;

void transition_start(TransType t) {
    if (!_buf_old) _buf_old = static_cast<uint16_t *>(ps_malloc(SCR_W * SCR_H * sizeof(uint16_t)));
    if (!_buf_old) return;
    tft.readRect(0, 0, SCR_W, SCR_H, _buf_old);
    _type = t;
    _step = 0;
}

bool transition_running() { return _type != TRANS_NONE; }

void transition_tick() {
    if (_type == TRANS_NONE) return;
    ++_step;
    const float progress = static_cast<float>(_step) / STEPS;
    if (_type == TRANS_SLIDE_LEFT || _type == TRANS_SLIDE_RIGHT) {
        const int offset = static_cast<int>(SCR_W * progress);
        for (int row = 0; row < SCR_H; ++row) {
            if (_type == TRANS_SLIDE_LEFT) {
                const int width = SCR_W - offset;
                if (width > 0) tft.pushImageDMA(0, row, width, 1, _buf_old + row * SCR_W + offset);
                if (offset > 0) tft.fillRect(width, row, offset, 1, T_BG);
            } else {
                const int width = SCR_W - offset;
                if (width > 0) tft.pushImageDMA(offset, row, width, 1, _buf_old + row * SCR_W);
                if (offset > 0) tft.fillRect(0, row, offset, 1, T_BG);
            }
        }
    } else if (_type == TRANS_FADE) {
        const uint8_t alpha = static_cast<uint8_t>(progress * 255);
        tft.fillScreen(tft.color565(0, alpha >> 2, 0));
    }
    if (_step >= STEPS) _type = TRANS_NONE;
}

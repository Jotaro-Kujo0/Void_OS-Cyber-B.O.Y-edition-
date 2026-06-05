// transition.cpp
#include "transition.h"
#include "hal/hal_display.h"
#include "config.h"
#include <Arduino.h>

// Two 480×320×2 byte buffers = 307,200 bytes each → uses PSRAM
static uint16_t *_buf_old = nullptr;
static uint16_t *_buf_new = nullptr;
static TransType _type    = TRANS_NONE;
static int       _step    = 0;
static const int STEPS    = 12;   // 12 frames = ~400ms at 30fps

void transition_start(TransType t) {
    if (!_buf_old) {
        _buf_old = (uint16_t*)ps_malloc(SCR_W * SCR_H * 2);
        _buf_new = (uint16_t*)ps_malloc(SCR_W * SCR_H * 2);
    }
    if (!_buf_old || !_buf_new) return;  // if PSRAM unavailable, skip
    // Capture current screen into _buf_old via readRect
    tft.readRect(0, 0, SCR_W, SCR_H, _buf_old);
    _type = t;
    _step = 0;
}

bool transition_running() { return _type != TRANS_NONE; }

void transition_tick() {
    if (_type == TRANS_NONE) return;
    _step++;
    float progress = (float)_step / STEPS;

    if (_type == TRANS_SLIDE_LEFT) {
        int offset = (int)(SCR_W * progress);
        // Old screen slides left
        tft.pushImageDMA(0, 0, SCR_W - offset, SCR_H,
                         _buf_old + offset);
    } else if (_type == TRANS_FADE) {
        // Simple fill; full fade needs alpha blending — skips for now
        uint8_t a = (uint8_t)(progress * 255);
        tft.fillScreen(tft.color565(0, a >> 2, 0));
    }

    if (_step >= STEPS) _type = TRANS_NONE;
}
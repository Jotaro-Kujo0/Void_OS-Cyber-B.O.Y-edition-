// hal_touch.cpp — touchscreen.
//
// Skeleton: ESP32 path uses XPT2046_BitbangSpi or the TFT-eSPI built-in
// touch helpers — wiring step:
//
//   1. `bodmer/TFT_eSPI @ ^2.5.43` already includes XPT2046 driver
//      if `TOUCH_CS` is set in platformio.ini (it is set to -1 today;
//      set it to the chip's CS pin to enable).
//   2. Call `tft.getTouch(&x, &y, &z);` in `hal_touch_tick()`. `z` is
//      pressure; gate threshold at HAL_TOUCH_Z_THRESHOLD.
//   3. On press, push an Event { EVT_TOUCH_DOWN, (x<<16)|y }. On
//      release, push { EVT_TOUCH_UP, x<<16|y }.
//
// Pi 5 path: read `/dev/input/event*` from a connected touchscreen
// (e.g. the official 7" Raspberry Pi touchscreen). Use libinput
// for hot-plug detection; skeleton here just returns false.

#include "hal_touch.h"
#include "os/events.h"
#include "config.h"
#include <cstring>

#ifndef VOIDOS_RPI5
#include <Arduino.h>
#include "hal_display.h"
static TouchPoint _last = {0,0,0};
static bool       _pressed = false;
#else
#endif

void hal_touch_init() {
#ifdef VOIDOS_RPI5
    // skim /dev/input/eventN once and pick the touchscreen device.
    // Real impl uses libinput_event_create() then evdev polling.
#else
#endif
}

void hal_touch_tick() {
#ifdef VOIDOS_RPI5
    // skeleton
#else
    TouchPoint p{};
    uint16_t tx = 0, ty = 0;
    if (tft.getTouch(&tx, &ty, HAL_TOUCH_Z_THRESHOLD)) {
        p.x = static_cast<int16_t>(tx);
        p.y = static_cast<int16_t>(ty);
        p.z = 1;
        if (!_pressed) {
            _pressed = true;
            _last = p;
            Event e{EVT_TOUCH_DOWN, (uint32_t)((p.x<<16) | (uint16_t)p.y)};
            events_push(e);
        }
    } else if (_pressed) {
        _pressed = false;
        Event e{EVT_TOUCH_UP, (uint32_t)((p.x<<16) | (uint16_t)p.y)};
        events_push(e);
    }
#endif
}

bool hal_touch_read(TouchPoint *out) {
    if (!out) return false;
#ifdef VOIDOS_RPI5
    return false;
#else
    *out = _last;
    return _pressed;
#endif
}

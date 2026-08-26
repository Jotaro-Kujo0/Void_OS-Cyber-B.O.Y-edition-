// hal_touch.h — touchscreen event source.
//
// Touch screen on this device is the secondary input (the user said
// "the physical screen is only for few specific stuff"). Pins exposed:
// the existing TFT data lines carry XPT2046 touch over SPI.
//
// hal_touch feeds press/release events into the same Event queue
// used by buttons. Use `e.type == EVT_TOUCH_DOWN/UP/COORD` if you
// need raw coordinates.

#pragma once
#include <stdint.h>

#define HAL_TOUCH_Z_THRESHOLD  200u

typedef struct {
    int16_t x, y, z;
} TouchPoint;

void   hal_touch_init();
void   hal_touch_tick();           // call from input_task()

bool   hal_touch_read(TouchPoint *out);

// hal_led_rgb.cpp — RGB LED state.
//
// ponytail: a single LED with semantic colours is more useful than
// four blink patterns. The driver is dead simple — write fixed PWM
// values per state. Hardware:
//   * ESP32 uses PWM on three GPIOs (R, G, B). Operator wires any
//     three free GPIOs (use ones not otherwise claimed, e.g.
//     PIN_HAPTIC, PIN_IR_ARRAY, PIN_RF_KILL — those are MOSFET gates
//     but you can hijack them transiently with PWM if MOSFET control
//     is not in use).
//   * Pi 5: a WS2812 or similar sink driven via the SPI MOSI pin or a
//     PWM pin. Default to PWM unless WS2812 is documented in BOM.

#include "hal_led_rgb.h"
#include "config.h"

#ifndef VOIDOS_RPI5
#include <Arduino.h>
static uint8_t _r=0, _g=0, _b=0;
#endif

static LedRgbState _cur = LEDRGB_IDLE;

void hal_led_set(LedRgbState s) {
    if (s == _cur) return;
    _cur = s;
#ifdef VOIDOS_RPI5
    // Pi 5 path: emit `system` call into a sink (PWM via sysfs or
    // ws2812-rpi via SPI). Skeleton: no-op. Wire via:
    //   * WS2812: `python3 -c "import board, neopixel; ..."` shell-out.
    //   * PWM sysfs: `echo 255 > /sys/class/pwm/pwmchip0/pwm0/duty_cycle`.
    // (See the BOM for the right path.)
    (void)s;
#else
    switch (s) {
        case LEDRGB_IDLE:    _r=0;   _g=128; _b=0;   break;  // soft green
        case LEDRGB_PASSIVE: _r=0;   _g=64;  _b=128; break;  // cyan
        case LEDRGB_ACTIVE:  _r=128; _g=80;  _b=0;   break;  // amber
        case LEDRGB_ALARM:   _r=200; _g=0;   _b=0;   break;  // red
    }
    // Hardware-specific write. Skeleton: define the PWM channels per
    // hardware revision in hal_pwm.h:
    //   ledcWrite(CHN_R, _r);
    //   ledcWrite(CHN_G, _g);
    //   ledcWrite(CHN_B, _b);
#endif
}
LedRgbState hal_led_get() { return _cur; }

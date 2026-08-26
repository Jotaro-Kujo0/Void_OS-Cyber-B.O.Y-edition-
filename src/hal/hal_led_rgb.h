// hal_led_rgb.h — semantic status LED.
//
// A single RGB LED assigns meaning to colour, not blinks:
//   * GREEN  : idle.        (low-risk operation)
//   * CYAN   : scanning/passive. (sniffer / monitor)
//   * AMBER  : active op.   (DEAUTH/KARMA/captive)
//   * RED    : alarm.       (handshake captured / detector)
//
// Apps call `hal_led_set(LEDRGB_STATE)` on every state transition.

#pragma once
#include <stdint.h>
typedef enum {
    LEDRGB_IDLE   = 0,
    LEDRGB_PASSIVE = 1,
    LEDRGB_ACTIVE  = 2,
    LEDRGB_ALARM   = 3,
} LedRgbState;

void hal_led_set(LedRgbState s);
LedRgbState hal_led_get();

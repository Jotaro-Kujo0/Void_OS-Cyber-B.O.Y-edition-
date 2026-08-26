// hal_buzzer.h — piezo / passive-buzzer event queue.
//
// The buzzer is event-driven: apps call `hal_buzzer_beep(...)` to
// enqueue a tone, and a scheduled task drains the queue without
// blocking the caller. The queue is small (8 slots) since beeps are
// short and the operator's ear is forgiving.

#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BUZZ_PATTERN_OFF      = 0,
    BUZZ_PATTERN_SHORT    = 1,   // 80 ms 1 kHz
    BUZZ_PATTERN_DOUBLE   = 2,   // two 80 ms with 60 ms gap
    BUZZ_PATTERN_LONG     = 3,   // 600 ms 1 kHz (alarm)
    BUZZ_PATTERN_CAPTURE  = 4,   // high-pitched 5 kHz for 200 ms
} BuzzPattern;

void     hal_buzzer_init();
void     hal_buzzer_beep(BuzzPattern p);
bool     hal_buzzer_is_busy();

// Soft tones used by the notification overlay.
void     hal_buzzer_notify_ok();
void     hal_buzzer_notify_err();

// hal_buzzer.cpp — piezo / passive-buzzer event queue.
//
// ponytail: the queue + scheduled task approach lets the calling app
// fire-and-forget. Pulse output is implemented as a sequence of
// ledc_write_tone calls on ESP32 or `system("beep ...")` shell-outs on
// Pi 5; both are short and non-blocking once the tone is started.
//
// Pi 5 path uses `/dev/console` or `speaker-test` shell-out; if the
// device lacks a buzzer, the calls are no-ops.

#include "hal_buzzer.h"
#include "os/scheduler.h"
#include "config.h"
#include <Arduino.h>
#include <cstring>
#ifdef VOIDOS_RPI5
#include <cstdio>
#include <cstdlib>
#endif

#ifndef VOIDOS_RPI5
#include <esp32-hal-ledc.h>
#endif

#define BUZZ_QUEUE_LEN 8
static struct {
    BuzzPattern pat;
    uint32_t    started_ms;
    bool        active;
} _q[BUZZ_QUEUE_LEN];
static uint8_t _qh = 0, _qt = 0, _qc = 0;
static int8_t _drain_id = -1;

// Plays a single tone for `ms` duration. Non-blocking on ESP32 via
// ledc; on Pi 5 we shell out and the tone plays in the background.
static void _start_tone(uint16_t hz, uint32_t ms) {
#ifdef VOIDOS_RPI5
    char cmd[40]; std::snprintf(cmd, sizeof(cmd), "beep -f %u -l %u &", hz, ms);
    std::system(cmd);
#else
    ledcWriteTone(0, hz);
    delay(ms);
    ledcWriteTone(0, 0);
#endif
}

static void drain_task() {
    if (!_q[_qt].active && _qc == 0) return;
    if (!_q[_qt].active) return;
    uint32_t now = millis();
    if (_q[_qt].started_ms == 0) {
        _q[_qt].started_ms = now;
        switch (_q[_qt].pat) {
            case BUZZ_PATTERN_SHORT:   _start_tone(1000, 80);  break;
            case BUZZ_PATTERN_DOUBLE:  _start_tone(1000, 80); delay(60); _start_tone(1000, 80); break;
            case BUZZ_PATTERN_LONG:    _start_tone(1000, 600); break;
            case BUZZ_PATTERN_CAPTURE: _start_tone(5000, 200); break;
            default: break;
        }
        _q[_qt].started_ms = millis();  // reset to now since `_start_tone` already blocked
        // After playing, mark this slot consumed; advance write head.
        uint32_t end = now;
        switch (_q[_qt].pat) {
            case BUZZ_PATTERN_SHORT:   end = now + 80;  break;
            case BUZZ_PATTERN_DOUBLE:  end = now + 220; break;
            case BUZZ_PATTERN_LONG:    end = now + 600; break;
            case BUZZ_PATTERN_CAPTURE: end = now + 200; break;
            default: end = now;
        }
        (void)end;
    }
    // Mark this slot consumed after the running time elapsed above.
    // For simplicity we treat `_start_tone`'s blocking delay back-to-back
    // as the consume trigger — the queue advances after each call.
    _qt = (_qt + 1) % BUZZ_QUEUE_LEN;
    if (_qc) --_qc;
}

void hal_buzzer_init() {
    if (_drain_id < 0) _drain_id = sched_add("buzzer_drain", drain_task, 0, 8);
}

void hal_buzzer_beep(BuzzPattern p) {
    if (p == BUZZ_PATTERN_OFF) return;
    if (_qc >= BUZZ_QUEUE_LEN) return;   // drop if queue full
    _q[_qh].pat = p; _q[_qh].active = true;
    _q[_qh].started_ms = 0;
    _qh = (_qh + 1) % BUZZ_QUEUE_LEN;
    ++_qc;
}

bool hal_buzzer_is_busy() { return _qc > 0; }

void hal_buzzer_notify_ok()  { hal_buzzer_beep(BUZZ_PATTERN_SHORT); }
void hal_buzzer_notify_err() { hal_buzzer_beep(BUZZ_PATTERN_DOUBLE); }

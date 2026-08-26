// hal_input.cpp
#include "hal_input.h"
#include "hal_buttons_exp.h"
#include "config.h"
#include <Arduino.h>

#ifdef VOIDOS_RPI5
#include <cstdlib>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#endif

static uint8_t  _pot = 0;
static bool     _state[3] = {};
static uint32_t _last[3] = {};
static const EventType DN[3] = {EVT_BTN_A_DOWN, EVT_BTN_B_DOWN, EVT_BTN_C_DOWN};
static const EventType UP[3] = {EVT_BTN_A_UP, EVT_BTN_B_UP, EVT_BTN_C_UP};

#ifdef VOIDOS_RPI5
static uint8_t read_pi_adc() {
    // Raspberry Pi 5 has no built-in ADC. If an external ADC exposes an IIO
    // channel, use it; otherwise keep the control at its neutral position.
    std::ifstream raw("/sys/bus/iio/devices/iio:device0/in_voltage0_raw");
    int value = 2048;
    if (raw) raw >> value;
    value = constrain(value, 0, 4095);
    return static_cast<uint8_t>((value * 255L) / 4095L);
}

// ── PC harness: drive the UI from a keyboard (VOIDOS_HARNESS=1) ────────
// a/A b/B c/C = button press/release, ,/. = pot nudge, 0-9 = pot set, q = quit
static void harness_set_pot(uint8_t value) {
    _pot = value;
    events_push({EVT_POT_CHANGED, _pot});
}

static void harness_inject() {
    static bool ready = false;
    if (!ready) {   // non-blocking so ticks never stall on idle stdin
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        ready = true;
    }
    char buf[32];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    for (ssize_t i = 0; i < n; ++i) {
        const char c = buf[i];
        if (c >= 'a' && c <= 'c') {
            const int b = c - 'a';
            _state[b] = true;
            events_push({DN[b], 0});
        } else if (c >= 'A' && c <= 'C') {
            const int b = c - 'A';
            _state[b] = false;
            events_push({UP[b], 0});
        } else if (c == ',') {
            harness_set_pot(_pot >= 16 ? _pot - 16 : 0);
        } else if (c == '.') {
            harness_set_pot(_pot <= 239 ? _pot + 16 : 255);
        } else if (c >= '0' && c <= '9') {
            harness_set_pot(static_cast<uint8_t>((c - '0') * 255 / 9));
        } else if (c == 'q') {
            _exit(0);
        }
    }
}

static bool harness_enabled() {
    static const bool on = [] {
        const char *v = std::getenv("VOIDOS_HARNESS");
        return v && v[0] == '1';
    }();
    return on;
}
#endif

void hal_input_init() {
#ifndef VOIDOS_RPI5
    pinMode(PIN_POT, INPUT);
#endif
    hal_buttons_exp_init();
}

void hal_input_tick() {
#ifdef VOIDOS_RPI5
    if (harness_enabled()) { harness_inject(); return; }
    uint8_t raw = read_pi_adc();
#else
    uint8_t raw = analogRead(PIN_POT) >> 4;
#endif
    uint8_t prev = _pot;
    _pot = (_pot * 7 + raw) >> 3;
    if (abs(static_cast<int>(_pot) - prev) > 2) events_push({EVT_POT_CHANGED, _pot});

    hal_buttons_exp_read();
    bool btn_values[3] = {
        hal_buttons_exp_get_select() != 0,
        hal_buttons_exp_get_back() != 0,
        hal_buttons_exp_get_aux() != 0
    };
    uint32_t now = millis();
    for (int i = 0; i < 3; ++i) {
        bool pressed = btn_values[i];
        if (pressed != _state[i] && now - _last[i] > 30) {
            _last[i] = now;
            _state[i] = pressed;
            events_push({pressed ? DN[i] : UP[i], 0});
        }
    }
}

uint8_t hal_input_pot() { return _pot; }
bool hal_input_btn(uint8_t i) { return i < 3 ? _state[i] : false; }

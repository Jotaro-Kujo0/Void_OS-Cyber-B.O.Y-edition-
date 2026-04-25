// hal_input.cpp
#include "hal_input.h"
#include "config.h"
#include <Arduino.h>

static uint8_t  _pot   = 0;
static bool     _state[3] = {};
static uint32_t _last[3]  = {};
static const uint8_t PINS[3] = {PIN_BTN_A, PIN_BTN_B, PIN_BTN_C};
static const EventType DN[3] = {EVT_BTN_A_DOWN, EVT_BTN_B_DOWN, EVT_BTN_C_DOWN};
static const EventType UP[3] = {EVT_BTN_A_UP,   EVT_BTN_B_UP,   EVT_BTN_C_UP};

void hal_input_init() {
    pinMode(PIN_POT, INPUT);
    for (int i = 0; i < 3; i++) {
        pinMode(PINS[i], INPUT_PULLUP);
    }
}

void hal_input_tick() {
    // EMA-smoothed ADC
    uint8_t raw = analogRead(PIN_POT) >> 4;
    uint8_t prev = _pot;
    _pot = (_pot * 7 + raw) >> 3;
    if (abs((int)_pot - prev) > 2)
        events_push({EVT_POT_CHANGED, _pot});

    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
        bool pressed = !digitalRead(PINS[i]);
        if (pressed != _state[i] && now - _last[i] > 30) {
            _last[i] = now;
            _state[i] = pressed;
            events_push({pressed ? DN[i] : UP[i], 0});
        }
    }
}

uint8_t hal_input_pot()         { return _pot; }
bool    hal_input_btn(uint8_t i){ return i < 3 ? _state[i] : false; }
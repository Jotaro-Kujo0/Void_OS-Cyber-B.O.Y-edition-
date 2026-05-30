// hal_input.cpp
#include "hal_input.h"
#include "hal_buttons_exp.h"
#include "config.h"
#include <Arduino.h>

static uint8_t  _pot   = 0;
static bool     _state[3] = {};
static uint32_t _last[3]  = {};
static const EventType DN[3] = {EVT_BTN_A_DOWN, EVT_BTN_B_DOWN, EVT_BTN_C_DOWN};
static const EventType UP[3] = {EVT_BTN_A_UP,   EVT_BTN_B_UP,   EVT_BTN_C_UP};

void hal_input_init() {
    pinMode(PIN_POT, INPUT);
    // Button reads now via PCF8574 I2C expander
    hal_buttons_exp_init();
}

void hal_input_tick() {
    // EMA-smoothed ADC (potentiometer on GPIO 34)
    uint8_t raw = analogRead(PIN_POT) >> 4;
    uint8_t prev = _pot;
    _pot = (_pot * 7 + raw) >> 3;
    if (abs((int)_pot - prev) > 2)
        events_push({EVT_POT_CHANGED, _pot});

    // Read buttons from PCF8574 expander
    hal_buttons_exp_read();
    
    // Map PCF8574 buttons: Select=A, Back=B, Aux=C
    bool btn_values[3] = {
        hal_buttons_exp_get_select(),  // A (P0)
        hal_buttons_exp_get_back(),    // B (P1)
        hal_buttons_exp_get_aux()      // C (P2)
    };

    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
        bool pressed = btn_values[i];
        if (pressed != _state[i] && now - _last[i] > 30) {
            _last[i] = now;
            _state[i] = pressed;
            events_push({pressed ? DN[i] : UP[i], 0});
        }
    }
}

uint8_t hal_input_pot()         { return _pot; }
bool    hal_input_btn(uint8_t i){ return i < 3 ? _state[i] : false; }
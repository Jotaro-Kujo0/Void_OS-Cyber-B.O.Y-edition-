#include "hal_buttons_exp.h"
#include <Wire.h>
#include <PCF8574.h>

// PCF8574 I2C: GPIO 21 SDA, 22 SCL, Address 0x20
// Port mapping:
// P0 = Select button
// P1 = Back button
// P2 = Aux button
// P3 = EC11 encoder A
// P4 = EC11 encoder B
// P5 = EC11 push
// P6 = SPDT kill switch

static PCF8574 _expander(0x20);
static uint8_t _port_state = 0xFF;

void hal_buttons_exp_init() {
    Wire.begin(21, 22);
    _expander.begin();
    _port_state = _expander.read8();
}

void hal_buttons_exp_read() {
    _port_state = _expander.read8();
}

// Returns 1 if button pressed (active low), 0 if released
uint8_t hal_buttons_exp_get_select() {
    return !(_port_state & (1 << 0));
}

uint8_t hal_buttons_exp_get_back() {
    return !(_port_state & (1 << 1));
}

uint8_t hal_buttons_exp_get_aux() {
    return !(_port_state & (1 << 2));
}

uint8_t hal_buttons_exp_get_encoder_a() {
    return !(_port_state & (1 << 3));
}

uint8_t hal_buttons_exp_get_encoder_b() {
    return !(_port_state & (1 << 4));
}

uint8_t hal_buttons_exp_get_encoder_push() {
    return !(_port_state & (1 << 5));
}

uint8_t hal_buttons_exp_get_kill_switch() {
    return !(_port_state & (1 << 6));
}

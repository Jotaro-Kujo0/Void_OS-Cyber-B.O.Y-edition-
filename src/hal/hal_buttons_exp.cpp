#include "hal_buttons_exp.h"
#include "config.h"

#ifdef VOIDOS_RPI5
#include <cerrno>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Raspberry Pi 5: I2C-1 uses BCM GPIO 2/3; PCF8574 remains at 0x20.
static int _i2c_fd = -1;
static uint8_t _port_state = 0xFF;

void hal_buttons_exp_init() {
    _i2c_fd = open("/dev/i2c-1", O_RDWR | O_CLOEXEC);
    if (_i2c_fd < 0 || ioctl(_i2c_fd, I2C_SLAVE, 0x20) < 0) {
        if (_i2c_fd >= 0) { close(_i2c_fd); _i2c_fd = -1; }
        return;
    }
    const uint8_t released = 0xFF;
    (void)write(_i2c_fd, &released, 1);
    hal_buttons_exp_read();
}

void hal_buttons_exp_read() {
    if (_i2c_fd >= 0) {
        uint8_t value = 0xFF;
        if (read(_i2c_fd, &value, 1) == 1) _port_state = value;
    }
}
#else
#include <Wire.h>
#include <PCF8574.h>

// PCF8574 I2C: GPIO 21 SDA, 22 SCL, Address 0x20
static PCF8574 _expander(0x20);
static uint8_t _port_state = 0xFF;

void hal_buttons_exp_init() { Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL); _expander.begin(); _port_state = _expander.read8(); }
void hal_buttons_exp_read() { _port_state = _expander.read8(); }
#endif

uint8_t hal_buttons_exp_get_select() { return !(_port_state & (1 << 0)); }
uint8_t hal_buttons_exp_get_back() { return !(_port_state & (1 << 1)); }
uint8_t hal_buttons_exp_get_aux() { return !(_port_state & (1 << 2)); }
uint8_t hal_buttons_exp_get_encoder_a() { return !(_port_state & (1 << 3)); }
uint8_t hal_buttons_exp_get_encoder_b() { return !(_port_state & (1 << 4)); }
uint8_t hal_buttons_exp_get_encoder_push() { return !(_port_state & (1 << 5)); }
uint8_t hal_buttons_exp_get_kill_switch() { return !(_port_state & (1 << 6)); }

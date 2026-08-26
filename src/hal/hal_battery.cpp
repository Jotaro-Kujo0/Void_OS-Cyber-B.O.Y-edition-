#include "hal_battery.h"

#ifdef VOIDOS_RPI5
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int _fd = -1;
static uint16_t _voltage = 0;
static uint8_t _soc = 0;

static bool read_register(uint8_t reg, uint16_t &value) {
    if (_fd < 0) return false;
    if (write(_fd, &reg, 1) != 1) return false;
    uint8_t bytes[2];
    if (read(_fd, bytes, 2) != 2) return false;
    value = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
    return true;
}

void hal_battery_init() {
    _fd = open("/dev/i2c-1", O_RDWR | O_CLOEXEC);
    if (_fd < 0 || ioctl(_fd, I2C_SLAVE, 0x36) < 0) {
        if (_fd >= 0) close(_fd);
        _fd = -1;
    }
}
uint16_t hal_battery_voltage() {
    uint16_t raw;
    if (read_register(0x02, raw)) _voltage = static_cast<uint16_t>((static_cast<uint32_t>(raw) * 1250u) / 1000u);
    return _voltage;
}
uint8_t hal_battery_soc() {
    uint16_t raw;
    if (read_register(0x04, raw)) _soc = static_cast<uint8_t>(raw >> 8);
    return _soc;
}
BatteryHealth hal_battery_health() { return _soc < 15 ? BAT_CRIT : (_soc < 30 ? BAT_WARN : BAT_GOOD); }

#else
#include <Arduino.h>
#include <DFRobot_MAX17043.h>
DFRobot_MAX17043 _fuel_gauge;
void hal_battery_init() { _fuel_gauge.begin(); }
uint16_t hal_battery_voltage() { return static_cast<uint16_t>(_fuel_gauge.readVoltage()); }
uint8_t hal_battery_soc() { float voltage=_fuel_gauge.readVoltage(); long mv=constrain(static_cast<long>(voltage*1000),3300L,4200L); return static_cast<uint8_t>(map(mv,3300,4200,0,100)); }
BatteryHealth hal_battery_health() { uint8_t soc=hal_battery_soc(); return soc<15?BAT_CRIT:(soc<30?BAT_WARN:BAT_GOOD); }
#endif

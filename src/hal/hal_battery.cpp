#include "hal_battery.h"
#include <Wire.h>
#include <DFRobot_MAX17043.h>

static DFRobot_MAX17043 _fuel_gauge;
static uint8_t _soc = 100;
static uint16_t _voltage = 4200;
static BatteryHealth _health = BAT_GOOD;

void hal_battery_init() {
    // Initialize I2C (GPIO 21 SDA, 22 SCL)
    Wire.begin(21, 22);
    
    // Initialize MAX17043 at default I2C address 0x36
    if (!_fuel_gauge.begin(Wire, 0x36)) {
        // Fallback: use default values if init fails
        _soc = 50;
        _voltage = 3700;
        _health = BAT_WARN;
    } else {
        // Initial read
        _soc = _fuel_gauge.getSOC();
        _voltage = _fuel_gauge.getVoltage();
    }
}

uint8_t hal_battery_soc() {
    _soc = _fuel_gauge.getSOC();
    return _soc;
}

uint16_t hal_battery_voltage() {
    _voltage = _fuel_gauge.getVoltage();
    return _voltage;
}

BatteryHealth hal_battery_health() {
    uint8_t soc = hal_battery_soc();
    
    if (soc < 10) {
        _health = BAT_CRIT;
    } else if (soc < 20) {
        _health = BAT_WARN;
    } else {
        _health = BAT_GOOD;
    }
    
    return _health;
}

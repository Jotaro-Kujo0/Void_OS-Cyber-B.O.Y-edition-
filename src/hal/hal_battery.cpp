#include <Arduino.h>
#include <DFRobot_MAX17043.h>
#include <Wire.h>

DFRobot_MAX17043 _fuel_gauge;

void hal_battery_init() {
    _fuel_gauge.begin();
}

uint16_t hal_battery_voltage() {
    return (uint16_t)_fuel_gauge.readVoltage();
}

uint8_t hal_battery_soc() {
    float voltage = _fuel_gauge.readVoltage();
    // LiPo battery: 3.3V is 0%, 4.2V is 100%
    // constrain the value to keep it between 0-100%
    uint8_t soc = map(constrain(voltage * 1000, 3300, 4200), 3300, 4200, 0, 100);
    return soc;
}
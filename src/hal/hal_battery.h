// hal_battery.h
#pragma once
#include <stdint.h>

typedef enum {
    BAT_GOOD,
    BAT_WARN,
    BAT_CRIT
} BatteryHealth;

void hal_battery_init();
uint8_t hal_battery_soc();           // State of charge 0-100%
uint16_t hal_battery_voltage();      // mV
BatteryHealth hal_battery_health();  // Health status

// hal_power.cpp
#include "hal_power.h"
#include "hal_display.h"
#include "hal_storage.h"
#include "config.h"
#include <Arduino.h>
#include <esp_sleep.h>

static uint32_t _last_activity = 0;
static bool     _dimmed = false;

void hal_power_init() {
    _last_activity = millis();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_A, 0);
}

void hal_power_activity() {
    _last_activity = millis();
    if (_dimmed) {
        uint8_t bl = hal_storage_get_u8(NVS_BL_KEY, BL_FULL);
        hal_display_bl_set(bl);
        _dimmed = false;
    }
}

void hal_power_tick() {
    uint32_t idle = millis() - _last_activity;
    if (!_dimmed && idle > DIM_TIMEOUT_MS) {
        hal_display_bl_set(BL_DIM);
        _dimmed = true;
    }
    if (idle > SLEEP_TIMEOUT_MS) {
        hal_storage_commit();
        esp_deep_sleep_start();  // wakes on BTN_A LOW
    }
}

bool hal_power_is_dimmed() { return _dimmed; }
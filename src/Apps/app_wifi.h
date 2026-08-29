// app_wifi.h — Wi-Fi/BLE audit UI, front-end for hal_wifi/hal_ble.
// Flat menu: scan, mgmt capture, EAPOL/PMKID, rogue AP, karma, inject,
// BLE scan/GATT. 1 Hz task flushes records to SD via hal_sdcard.

#pragma once
#include "os/events.h"

void app_wifi_init();
void app_wifi_tick();
void app_wifi_draw();
void app_wifi_event(Event e);
void app_wifi_suspend();
// app_wifi.h — Wi-Fi/BLE auditing application (slot APP_WIFI)
//
// ───────────────────────────────────────────────────────────────────────────
//  OVERVIEW
// ───────────────────────────────────────────────────────────────────────────
//
//  app_wifi is the front-end for the hal_wifi module. It exposes:
//
//      * Wardriving scan with GPS tagging         (hal_wifi_scan_*)
//      * 802.11 management-frame monitor           (hal_wifi_capture_consume)
//      * 4-way handshake & PMKID logging           (hal_wifi_handshake_*)
//      * Rogue AP / client auto-connect auditing    (hal_wifi_set_mode_ap)
//      * BLE device scan, advertisement capture    (hal_ble_scan_*)
//      * BLE GATT enumeration                      (hal_ble_gatt_*)
//
//  Each sub-mode is a single menu item. The status bar shows the current
//  mode and a "Wi-Fi: OFF" / "Wi-Fi: MON" / "Wi-Fi: STA" indicator that
//  comes from hal_wifi_get_mode().
//
//  The app uses the existing sched_add() interface to register a 1 Hz
//  housekeeping task that flushes captured records to the SD card via
//  hal_sdcard_open().
//
// ───────────────────────────────────────────────────────────────────────────
//  STATE MACHINE
// ───────────────────────────────────────────────────────────────────────────
//
//      MENU_WIFI        : user is choosing a sub-mode
//      MENU_SCAN        : scan in progress, drawing the AP list
//      MENU_CAPTURE     : monitor mode active, drawing live RSSI
//      MENU_HANDSHAKE   : waiting for an EAPOL frame
//      MENU_ROGUE_AP     : AP-mode active, advertising
//      MENU_BLE_SCAN    : BLE scan in progress
//      MENU_GATT        : GATT enumeration in progress
//
//  Transitions are driven by button events (EVT_BTN_A_DOWN to enter a
//  mode, EVT_BTN_B_DOWN to back out).
//
// ───────────────────────────────────────────────────────────────────────────
//  RESOURCE NOTES
// ───────────────────────────────────────────────────────────────────────────
//
//   * Heap: up to WIFI_AP_LOG × sizeof(WifiApRecord) ≈ 32 × 96 = 3 KB
//   * SD card writes: one open()/write() per scan; rate-limited to 1 Hz
//   * CPU: BLE scan ≈ 2 % on Pi 5; Wi-Fi scan ≈ 5 %
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include "os/events.h"

void app_wifi_init();
void app_wifi_tick();
void app_wifi_draw();
void app_wifi_event(Event e);
void app_wifi_suspend();
// app_hid.h — HID keystroke emulation (slot APP_HID)
//
// Sub-modes:
//   0. BLE (BadBLE)   — BLE HID over GATT keyboard
//   1. USB (CDC)      — USB HID keyboard gadget via configfs
//   2. BLE (PERIPH)   — pair as keyboard to a target device
//   3. APPLE (SPOOF)  — AirPods / Continuity proximity beacon spam
//
// Both transports share the same 8-byte boot-keyboard report format:
//   Byte 0: modifier bitmap
//   Byte 1: reserved (0)
//   Byte 2..7: up to 6 simultaneous keycodes
//
// Requires:
//   - bluez + bluetoothd (BLE modes)
//   - dtoverlay=dwc2 (USB HID gadget)
//   - /dev/hidg0 for USB keyboard gadget

#pragma once
#include "../os/events.h"

void app_hid_init();
void app_hid_tick();
void app_hid_draw();
void app_hid_event(Event e);
void app_hid_suspend();

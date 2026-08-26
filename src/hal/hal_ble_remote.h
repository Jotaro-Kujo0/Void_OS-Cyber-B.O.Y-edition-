// hal_ble_remote.h — BLE UART control channel.
//
// Device exposes a Nordic-style UART service (NUS UUID 6E400001-B5A3-
// F393-E0A9-E50E24DCCA9E). Paired phone runs a "remote" app that sends
// command lines over BLE; the device runs them through the same
// `hal_serial_term::dispatch`.

#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_ble_remote_init();
void hal_ble_remote_advertise();   // spin up the service
void hal_ble_remote_stop();

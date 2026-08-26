// hal_ble_remote.cpp — BLE UART remote SKELETON.
//
// =====================================================================
//  IMPLEMENTATION STEPS
// =====================================================================
//
//  ── ESP32 path ──────────────────────────────────────────────────────
//      * Use `NimBLE-Arduino` (HM-10-style GATT). Service UUID:
//
//          6E400001-B5A3-F393-E0A9-E50E24DCCA9E   (NUS primary service)
//          6E400002-...                          (TX char; phone writes here)
//          6E400003-...                          (RX char; phone reads here)
//
//      * The TX-ntfy handler turns bytes into `hal_serial_term::dispatch`
//        line-by-line.
//
//  ── Pi 5 path ──────────────────────────────────────────────────────
//      * `bluez` D-Bus API. `Bus("org.bluez")` →
//        `iface="org.bluez.GattManager1" / "org.bluez.LEAdvertisingManager1"`.
//      * Use `pybluez` (Python) or `gdbus` shell-out.
//
//  ── Phone-side app pairing ─────────────────────────────────────────
//
//      Deploy a small Flutter/React Native app on the operator's
//      phone. The phone discovers the device via BLE scan,
//      establishes a connection, then exposes a chat-like textbox
//      that sends the same line-protocol as the USB-CDC REPL.
//
//  ── SECURITY ────────────────────────────────────────────────────────
//
//      Pairing is unauthenticated by default. Real impl: bond on
//      first connect (use `BLEBond` for permanent key storage on
//      ESP32; BlueZ `TrustedDevice` on Linux). Even with no auth,
//      MITM is hard in a noisy office `2.4 GHz` environment.
//
//  =====================================================================

#include "hal_ble_remote.h"

static bool _on = false;

void hal_ble_remote_init() { _on = false; }
void hal_ble_remote_advertise() { _on = true;  /* placeholder */}
void hal_ble_remote_stop()      { _on = false; }

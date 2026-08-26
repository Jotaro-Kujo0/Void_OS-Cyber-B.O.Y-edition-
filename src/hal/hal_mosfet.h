// hal_mosfet.h — MOSFET switch driver for hardware RF isolation
//
// ───────────────────────────────────────────────────────────────────────────
//  ARCHITECTURE
// ───────────────────────────────────────────────────────────────────────────
//
//  Three MOSFET switches are exposed:
//
//      PIN_IR_ARRAY  — 940 nm IR LED array (always-on when stealth off)
//      PIN_RF_KILL   — CC1101 VCC line, used to physically disconnect the
//                      sub-GHz radio in dark mode
//      PIN_HAPTIC    — see hal_haptic.h; listed here so dark mode can
//                      mute the motor along with everything else
//
//  The "Hardware RF Isolation" stealth feature lives here. When dark mode
//  is engaged, hal_mosfet_set(MOSFET_RF, MOSFET_OFF) is called BEFORE the
//  CC1101 enters IDLE. That guarantees zero carrier-wave leakage — even
//  WOR / IDLE states leak a small amount of LO energy through the
//  antenna trace.
//
//  Driving a MOSFET from a Pi 5 GPIO is straightforward: the GPIO is
//  rated at 16 mA source, the IRLZ44N Vgs(th) is ~1 V, and the BOM
//  includes a 10 kΩ pull-down so the gate stays at 0 V during boot.
//  This module only needs to drive the GPIO high or low — no PWM, no
//  current limiting.
//
// ───────────────────────────────────────────────────────────────────────────
//  WHAT MUST BE ADDED BEFORE THIS MODULE IS USABLE
// ───────────────────────────────────────────────────────────────────────────
//
//  1. GPIO line reservation through the libgpiod character-device API
//     (same pattern used by tft_eSPI.cpp):
//
//         struct gpiohandle_request req = {};
//         req.lineoffsets[0] = PIN_RF_KILL;
//         req.flags = GPIOHANDLE_REQUEST_OUTPUT;
//         ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
//
//     Hold the line handle for the lifetime of the process. Release it
//     on SIGTERM so a reboot of the radio stack does not leak handles.
//
//  2. On ESP32 use digitalWrite() and pinMode(..., OUTPUT).
//
//  3. A hal_mosfet_isolate_all() helper that powers off every switch at
//     once. Called from SIGTERM handlers and from app_dark on entry.
//
// ───────────────────────────────────────────────────────────────────────────
//  RESOURCE NOTES (Pi 5, 4 GB)
// ───────────────────────────────────────────────────────────────────────────
//
//   * Three GPIO line handles: ~96 bytes each (kernel side: minimal)
//   * CPU: negligible
//   * Power savings when MOSFET_RF is OFF: ~25 mA (CC1101 TX/RX draw)
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MOSFET_IR_ARRAY = 0,   // PIN_IR_ARRAY
    MOSFET_RF       = 1,   // PIN_RF_KILL
    MOSFET_HAPTIC   = 2,   // PIN_HAPTIC (alias of hal_haptic_silence)
    MOSFET_COUNT    = 3,
} MosfetChannel;

typedef enum {
    MOSFET_OFF = 0,
    MOSFET_ON  = 1,
} MosfetState;

void     hal_mosfet_init();
void     hal_mosfet_set(MosfetChannel ch, MosfetState state);
MosfetState hal_mosfet_get(MosfetChannel ch);
void     hal_mosfet_isolate_all();   // all OFF — used in dark mode / SIGTERM
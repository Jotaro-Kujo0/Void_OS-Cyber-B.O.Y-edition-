// hal_wake_on_radio.cpp — Wake-on-Radio (CC1101) SKELETON.
//
// =====================================================================
//  IMPLEMENTATION GUIDE
// =====================================================================
//
//  ── ESP32 path ──────────────────────────────────────────────────────
//      `attachInterrupt(digitalPinToInterrupt(PIN_CC1101_GDO0), isr, RISING)`.
//
//      Inside the ISR, simply set a `volatile bool wor_flag = true`.
//      On the main loop, when true, wake from `esp_light_sleep_start()`.
//
//      Configure CC1101 in WOR (Wake-On-Radio) mode via SmartRC lib:
//       - `EleInf.chipId` set to CC1101
//       - `ELEINF.MODE = 0x05` (RX + WOR)
//       - `WOREVT1`, `WOREVT0` to set the polling interval
//       - `RCCTRL0` and `RCCTRL1` for the EVENT0/EVENT1 thresholds
//
//      Power: WOR @ 1 s polling draws ~7 mA on CC1101 (versus the ~1.4 µA
//      for ESP32 deep-sleep + RTC wake). Wake on packet: the device
//      reads the buffer in 6 ms, then re-arms.
//
//  ── Pi 5 path ──────────────────────────────────────────────────────
//      * Use `nc` listeners via `tcpdump -e -n -r <pcap>` from the
//        existing hal_wifi stream. The Pi 5 doesn't have CC1101 on
//        the same SoC; depends on whether you've got a Ham It Up
//        converter attached.
//
//  =====================================================================

#include "hal_wake_on_radio.h"
#include "config.h"

static bool _armed = false;
void hal_wor_init(void)   { _armed = false; }
void hal_wor_arm(void)    { _armed = true;  }
void hal_wor_disarm(void) { _armed = false; }

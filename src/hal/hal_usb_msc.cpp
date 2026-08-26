// hal_usb_msc.cpp — USB Mass Storage gadget SKELETON.
//
// =====================================================================
//  ADD TO MAKE WORK
// =====================================================================
//
//  ── ESP32 path ─────────────────────────────────────────────────────
//      * tinyusb library (https://github.com/haywire/tinyusb) with
//        the `CFG_TUSB_MSC=1` build flag.
//      * Register the SD card or SPIFFS as the backing store
//        (`tud_msc_start()`).
//      * Vendor/Product ID: `0x2E8A / 0x0007` (Raspberry Pi
//        Foundation) avoids Windows driver requests; pick a unique
//        ID from the vendor's range.
//
//  ── Pi 5 path ──────────────────────────────────────────────────────
//      * `configfs` + `libcomposite` (`/sys/kernel/config/usb_gadget/`).
//      * `mkdir functions/mass_storage.usb0` then bind
//        `lun.0/file=/var/lib/void-os/loot`.
//      * `echo "VID:PID" > idVendor / idProduct` then `echo 1 > UDC`.
//
//  ── Bring-up checklist ────────────────────────────────────────────
//
//      [ ] Pull-up on D+/D-.
//      [ ] 5V supplied via the USB-C cable.
//      [ ] Pull-down on CC1/CC2 for "UFP" announcement.
//      [ ] Mass-storage off when the device is field-deployed (the
//          wireless side does the work).
//
//  =====================================================================

#include "hal_usb_msc.h"
static bool _msc_on = false;

void hal_usb_msc_init() {
    _msc_on = false;
}

bool hal_usb_msc_start() {
    _msc_on = true;
    return true;
}

void hal_usb_msc_stop() { _msc_on = false; }

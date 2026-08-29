// hal_usb_msc.h — USB Mass Storage gadget.
//
// The device appears to a host as a USB stick whose contents are the
// loot directory. Operators pull loot off the device by plugging it
// into a laptop and `cp /Volumes/void-os/* ~/case/`.
//
//  * Raspberry Pi 5: on `hal_usb_msc_start()` a fresh FAT32 backing
//    image is created, the loot root is copied into it, and the image
//    is exposed through configfs/libcomposite (`dtoverlay=dwc2`,
//    `dr_mode=otg`). `hal_usb_msc_stop()` detaches the gadget and
//    tears the image down. Mass storage stays off in the field — the
//    wireless side does the work.
//
//  * ESP32: no configfs, so this is intentionally a no-op stub. USB
//    gadget features are Linux-only on this codebase.
#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_usb_msc_init();
bool hal_usb_msc_start();   // expose LOOT root as a USB drive
void hal_usb_msc_stop();

// Subsystem stats — surfaced in app_stat. `active` reflects whether the
// gadget is currently bound; the capacity/free figures refer to the USB
// drive (the FAT backing image), not the host filesystem.
typedef struct {
    bool     active;        // gadget bound and visible to a host
    uint32_t files_count;   // files staged on the USB drive (loot root)
    uint32_t capacity_kb;   // total capacity of the backing image
    uint32_t free_kb;       // free space remaining on the image
} UsbMscStats;
UsbMscStats hal_usb_msc_stats();

// Pure computation: turn raw gathered values (image capacity in kB, used
// kB as reported by `du -sk`, file count from `find`) into a UsbMscStats
// struct with free_kb = max(capacity - used, 0). Target-agnostic, so it
// can be unit-tested with captured du/find output.
UsbMscStats hal_usb_msc_stats_from(uint32_t capacity_kb, uint32_t used_kb,
                                    uint32_t files_count, bool active);
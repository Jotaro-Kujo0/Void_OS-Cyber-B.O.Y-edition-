// hal_usb_msc.h — USB Mass Storage gadget.
//
// The device appears to a host as a USB stick whose contents are the
// loot directory. Operators pull loot off the device by plugging it
// into a laptop and `cp /Volumes/void-os/* ~/case/`.
#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_usb_msc_init();
bool hal_usb_msc_start();   // expose LOOT root as a USB drive
void hal_usb_msc_stop();

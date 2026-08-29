// app_hid.h — HID keystroke emulation (BadBLE/USB/Apple spoof).
// Shared 8-byte boot-keyboard report. Needs bluez + dtoverlay=dwc2
// + /dev/hidg0.

#pragma once
#include "../os/events.h"

void app_hid_init();
void app_hid_tick();
void app_hid_draw();
void app_hid_event(Event e);
void app_hid_suspend();

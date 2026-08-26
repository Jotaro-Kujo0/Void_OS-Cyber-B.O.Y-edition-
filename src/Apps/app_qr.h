// app_qr.h — slot APP_QR: QR-code generator.
//
// Sub-modes:
//   0. URL — pre-saved target string in hal_storage["qr_text"].
//   1. GEN — generates a Bitmap and writes `/sd/qr.bmp` (Pi 5
//            variant path can also drop into /var/lib/void-os/qr.bmp).
//   2. SHOW — re-renders the produced image on the device screen.
//
// The specialized "video" affordance of this app is: scan a QR displayed
// on the device with an operator's phone, the phone opens the target
// URL without ever typing it.

#pragma once
#include "../os/events.h"

void app_qr_init();
void app_qr_tick();
void app_qr_draw();
void app_qr_event(Event e);
void app_qr_suspend();

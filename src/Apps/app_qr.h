// app_qr.h — QR-code generator. URL / GEN (/sd/qr.bmp) / SHOW.

#pragma once
#include "../os/events.h"

void app_qr_init();
void app_qr_tick();
void app_qr_draw();
void app_qr_event(Event e);
void app_qr_suspend();

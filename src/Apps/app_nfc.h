// app_nfc.h
#pragma once
#include "os/events.h"

void app_nfc_init();
void app_nfc_tick();
void app_nfc_draw();
void app_nfc_event(Event e);
void app_nfc_suspend();

// app_nfc.h — NFC/RFID audit via PN532.
// Modes: READ, EMUL, KEYS, DICT, HIST, WRITE, ISO14.

#pragma once
#include "../os/events.h"

void app_nfc_init();
void app_nfc_tick();
void app_nfc_draw();
void app_nfc_event(Event e);
void app_nfc_suspend();

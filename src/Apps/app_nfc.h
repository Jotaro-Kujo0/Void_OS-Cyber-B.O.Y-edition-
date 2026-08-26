// app_nfc.h — NFC & RFID auditing via PN532 (slot APP_NFC)
//
// Sub-modes:
//   0. READ   — read UID + NDEF records from ISO14443 tags
//   1. EMUL   — emulate a saved UID to test reader auth
//   2. KEYS   — MIFARE Classic key dictionary attack
//   3. DICT   — open wordlist for key audit
//   4. HIST   — last-read tag history
//   5. WRITE  — write NDEF / MIFARE payload to a tag
//   6. ISO14  — ISO 14443-A crypto1 nested/darkside attack
//
// Legal posture: KEYS + EMUL require hold-A for 3 s ("AUDIT" banner).

#pragma once
#include "../os/events.h"

void app_nfc_init();
void app_nfc_tick();
void app_nfc_draw();
void app_nfc_event(Event e);
void app_nfc_suspend();

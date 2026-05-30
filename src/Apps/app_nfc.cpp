#include "app_nfc.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "os/scheduler.h"
#include "hal/hal_storage.h"
#include "config.h"
#include <Wire.h>
#include <string.h>
#include <stdio.h>

// PN532 I2C mode: GPIO 21 SDA, 22 SCL, Address 0x24
// TODO: Integrate with proper PN532 library (see BOM for ElectronicCats-PN532)
// For now, this is a stub implementation

static bool _hw_ok = false;

typedef struct {
    char    uid_str[24];
    char    type[18];
    uint8_t block4[16];
    bool    has_block;
} NfcRecord;

#define NFC_LOG_MAX 8
static NfcRecord _log[NFC_LOG_MAX];
static uint8_t   _log_count = 0;
static NfcRecord _latest;
static bool      _scanning  = false;
static char      _status[32] = "READY";
static uint32_t  _frame = 0;

static void nfc_task() {
    if (!_scanning || !_hw_ok) return;
    // TODO: Implement NFC scanning when library is integrated
}

static void save_nfc(const NfcRecord &r) {
    if (_log_count >= NFC_LOG_MAX) {
        for (int i = 0; i < NFC_LOG_MAX - 1; i++)
            _log[i] = _log[i+1];
        _log_count = NFC_LOG_MAX - 1;
    }
    _log[_log_count++] = r;
}

void app_nfc_init() {
    _hw_ok = false;
    _log_count = 0;
    _scanning = false;
    strncpy(_status, "INIT", sizeof(_status));
}

void app_nfc_tick() {
    _frame++;
    nfc_task();
}

void app_nfc_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_text(20, 30, "[NFC Reader]", T_FG, T_BG, FONT_MD);
    draw_text(20, 60, "Status:", T_FG, T_BG, FONT_SM);
    draw_text(80, 60, _status, T_ACCENT, T_BG, FONT_SM);
    
    if (_log_count == 0) {
        draw_text(20, 100, "No tags detected.", T_ERR, T_BG, FONT_MD);
    } else {
        draw_textf(20, 100, T_ACCENT, T_BG, FONT_SM, "Tags: %d", _log_count);
        if (_log_count > 0) {
            draw_textf(20, 120, T_FG, T_BG, FONT_SM, "Latest: %s", _latest.uid_str);
        }
    }
}

void app_nfc_event(Event e) {
    if (e.type == EVT_BTN_A_DOWN) {
        _scanning = !_scanning;
        strncpy(_status, _scanning ? "SCANNING" : "READY", sizeof(_status));
    }
}

void app_nfc_suspend() {
    _scanning = false;
}

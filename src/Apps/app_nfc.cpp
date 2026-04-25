#include "app_nfc.h"
#include "ui/draw.h"
#include "ui/theme.h"
#include "os/scheduler.h"
#include "hal/hal_storage.h"
#include "config.h"
#include <PN532_SPI.h>
#include <PN532.h>
#include <string.h>
#include <stdio.h>

static PN532_SPI _spi(SPI, PIN_PN532_CS);
static PN532     _nfc(_spi);
static bool      _hw_ok = false;

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
    uint8_t uid[7], uid_len;
    if (!_nfc.readPassiveTargetID(
            PN532_MIFARE_ISO14443A, uid, &uid_len, 50)) return;

    NfcRecord r = {};
    r.uid_str[0] = 0;
    for (int i=0; i<uid_len; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X%s", uid[i], i<uid_len-1?":":"");
        strncat(r.uid_str, b, sizeof(r.uid_str)-strlen(r.uid_str)-1);
    }
    strncpy(r.type,
            uid_len==4 ? "MIFARE Classic" :
            uid_len==7 ? "MIFARE ULight"  : "ISO14443A",
            sizeof(r.type));
    r.has_block = _nfc.mifareclassic_ReadDataBlock(4, r.block4);
    _latest = r;
    snprintf(_status, sizeof(_status), "FOUND: %s", r.type);
    if (_log_count < NFC_LOG_MAX) {
        _log[_log_count] = r;
        // persist
        char key[12];
        snprintf(key, sizeof(key), "nfc%d", _log_count);
        hal_storage_set_blob(key, &r, sizeof(NfcRecord));
        _log_count++;
    }
    _scanning = false;
}

static void load_nfc_log() {
    _log_count = 0;
    for (int i=0; i<NFC_LOG_MAX; i++) {
        char key[12]; snprintf(key, sizeof(key), "nfc%d", i);
        size_t sz = sizeof(NfcRecord);
        if (hal_storage_get_blob(key, &_log[_log_count], &sz))
            _log_count++;
    }
}

void app_nfc_init() {
    _scanning = false;
    _nfc.begin();
    _hw_ok = (_nfc.getFirmwareVersion() != 0);
    if (_hw_ok) _nfc.SAMConfig();
    else strncpy(_status, "PN532 ERROR", sizeof(_status));
    load_nfc_log();
    sched_add("nfc_scan", nfc_task, 200, 4);
}

void app_nfc_suspend() { _scanning = false; }

void app_nfc_event(Event e) {
    if (e.type == EVT_BTN_A_DOWN) {
        _scanning = !_scanning;
        strncpy(_status, _scanning ? "SCANNING..." : "STOPPED",
                sizeof(_status));
    }
    if (e.type == EVT_BTN_C_DOWN) {
        // Clear log
        _log_count = 0;
        for (int i=0;i<NFC_LOG_MAX;i++) {
            char key[12]; snprintf(key,sizeof(key),"nfc%d",i);
            hal_storage_set_str(key, "");
        }
        strncpy(_status, "LOG CLEARED", sizeof(_status));
    }
    _frame++;
}

void app_nfc_tick() { _frame++; }

void app_nfc_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "NFC / RFID", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W-60, 8, T_DIM, T_PANEL, FONT_SM,
               "%d saved", _log_count);

    if (!_hw_ok) {
        draw_text(20, 60, "PN532 not detected.", T_ERR, T_BG, FONT_MD);
        draw_text(20, 90, "Check wiring: CS/MOSI/MISO/SCK", T_DIM, T_BG, FONT_SM);
        goto footer;
    }

    if (_scanning) {
        // Animated rings
        for (int r=15; r<=55; r+=13) {
            int eff = (r + (int)(_frame*3)) % 55;
            draw_circle(80, 140, eff, 0x0340);
        }
        draw_fcircle(80, 140, 10, T_FG);
        draw_text(100, 134, "SCANNING...", T_FG, T_BG, FONT_SM);
    }

    // Latest scan
    if (_latest.uid_str[0]) {
        draw_text(8, 40, "LAST:", T_DIM, T_BG, FONT_SM);
        draw_textf(48, 40, T_FG, T_BG, FONT_SM, "%s", _latest.type);
        draw_textf(8, 54, T_FG, T_BG, FONT_SM, "UID: %s", _latest.uid_str);
        if (_latest.has_block) {
            char hb[40]; hb[0]=0;
            for (int i=0;i<16;i++) {
                char b[3];
                snprintf(b,sizeof(b),"%02X",_latest.block4[i]);
                strncat(hb,b,3);
                if(i==7) strncat(hb," ",2);
            }
            draw_textf(8, 68, T_DIM, T_BG, FONT_SM, "BLK4: %s", hb);
        }
    }

    // Log list
    draw_hline(8, 90, SCR_W-16, T_BORDER);
    draw_text(8, 96, "LOG:", T_DIM, T_BG, FONT_SM);
    for (int i=0; i<_log_count && i<7; i++) {
        draw_textf(8, 108+i*14, i==_log_count-1?T_FG:T_DIM, T_BG,
                   FONT_SM, "[%d] %s  %s", i,
                   _log[i].type, _log[i].uid_str);
    }
    if (_log_count == 0)
        draw_text(8, 108, "no scans yet", T_BORDER, T_BG, FONT_SM);

    footer:
    draw_hline(0, SCR_H-26, SCR_W, T_BORDER);
    draw_fill(0, SCR_H-26, SCR_W, 26, T_PANEL);
    draw_text(8, SCR_H-16, _status, T_FG, T_PANEL, FONT_SM);
    draw_text(SCR_W-188, SCR_H-16,
              "[A]SCAN  [C]CLEAR LOG  [B]BACK",
              T_DIM, T_PANEL, FONT_SM);
}
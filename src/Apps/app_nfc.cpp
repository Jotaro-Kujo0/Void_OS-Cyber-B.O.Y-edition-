// app_nfc.cpp — NFC & RFID auditing (PN532, Raspberry Pi 5)
//
// Sub-modes:
//   0. READ   — poll UID + parse NDEF
//   1. EMUL   — emulate DS1990 / ISO14443 UID
//   2. KEYS   — MIFARE Classic key dictionary attack
//   3. DICT   — stream keys from wordlist
//   4. HIST   — last-read history
//   5. WRITE  — write NDEF/MIFARE payload
//   6. ISO14  — crypto1 nested/darkside

#include "app_nfc.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../config.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef VOIDOS_RPI5
// PN532 over I2C — adjust path to match your HAL layout
// If you don't have pn532_linux.h yet, comment this out
// and stub the _pn532 calls below.
#if __has_include("rpi5/pn532_linux.h")
#include "rpi5/pn532_linux.h"
#define NFC_HW_AVAILABLE 1
#endif
#endif

//menu
static const char *LABELS[] = {
    "READ (NDEF)0", //0
    "EMUL (UID)", //1
     "KEYS  (Mifare)",  // 2
    "DICT  (default)", // 3
    "HIST  (last)",    // 4
    "WRITE (NFC)",     // 5
    "ISO14 (CRYPTO)",  // 6
};
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);

//NFC tag log
#define NFC_TAG_LOG_MAX 32
#define NFC_UID_LEN     24

struct NfcRecord {
    char    uid_str[NFC_UID_LEN];
    char    type[18];
    uint8_t block4[16];
    bool    has_block;
};

static NfcRecord _tag_log[NFC_TAG_LOG_MAX];
static uint8_t   _log_count = 0;


//state
static uint8_t  _row       = 0;
static bool     _armed     = false;
static bool     _scanning  = false;
static char     _status[32] = "READY";
static char     _last_uid[NFC_UID_LEN] = {};
static uint32_t _frame     = 0;
static uint8_t  _hist_scroll = 0;

// Audit hold timer
static uint32_t _hold_start = 0;
static bool     _hold_arming = false;

// MIFARE default key dictionary (32 common keys, offline fallback)
static const uint8_t MIFARE_DEFAULT_KEYS[][6] = {
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, // factory default
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5}, // MAD key
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}, // NFC Forum
    {0x00,0x00,0x00,0x00,0x00,0x00}, // blank
    {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
    {0x4D,0x3A,0x99,0xC3,0x51,0xDD}, // MAD1
    {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
    {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}, // AABBCCDDEEFF
    {0x01,0x02,0x03,0x04,0x05,0x06},
    {0xAB,0xCD,0xEF,0x12,0x34,0x56},
    {0x11,0x22,0x33,0x44,0x55,0x66},
    {0x10,0x20,0x30,0x40,0x50,0x60},
    {0x00,0x00,0x00,0x00,0x00,0x01},
    {0x42,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x01,0x00},
    {0x00,0x01,0x00,0x00,0x00,0x00},
    {0x53,0x43,0x48,0x45,0x4D,0x45}, // SCHEME
    {0x12,0x34,0x56,0x78,0x9A,0xBC},
    {0xDE,0xAD,0xBE,0xEF,0xCA,0xFE},
    {0xCA,0xFE,0xBA,0xBE,0xDE,0xAD},
    {0x00,0x00,0x00,0x00,0x00,0x02},
    {0x00,0x00,0x00,0x00,0x00,0x03},
    {0x00,0x00,0x00,0x00,0x00,0x04},
    {0x00,0x00,0x00,0x00,0x00,0x05},
    {0xFF,0xFF,0x00,0x00,0xFF,0xFF},
    {0xFF,0x00,0xFF,0x00,0xFF,0x00},
    {0x00,0xFF,0x00,0xFF,0x00,0xFF},
    {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
    {0xFF,0xEE,0xDD,0xCC,0xBB,0xAA},
    {0x12,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x12,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x12,0x00},
};
static const uint8_t MIFARE_N_KEYS = sizeof(MIFARE_DEFAULT_KEYS) / sizeof(MIFARE_DEFAULT_KEYS[0]);

//PM532 hal abstraction
#ifdef NFC_HW_AVAILABLE
static Pn532I2c _pn532;
#endif
static bool     _hw_ok = false;
static uint8_t  _stub_uid[4] = {0xDE, 0xAD, 0xBE, 0xEF};
// Last raw UID bytes (for EMUL mode)
static uint8_t  _last_uid_raw[10] = {};
static uint8_t  _last_uid_len = 0;

static bool nfc_hw_open() {
#ifdef NFC_HW_AVAILABLE
    _hw_ok = _pn532.open();
#else
    _hw_ok = false;
#endif
    return _hw_ok;
}

static void nfc_hw_close() {
#ifdef NFC_HW_AVAILABLE
    _pn532.close();
#endif
}

static bool nfc_poll_uid(uint8_t *out, uint8_t *out_len) {
#ifdef NFC_HW_AVAILABLE
    if (!_hw_ok) return false;
    Pn532Uid uid;
    if (_pn532.poll_uid(uid, 100)) {
        *out_len = uid.length;
        for (uint8_t i = 0; i < uid.length && i < 10; ++i)
            out[i] = uid.bytes[i];
        return true;
    }
    return false;
#else
    // Stub: synthesize a tag every ~3 seconds for testing
    static uint32_t last_stub = 0;
    uint32_t now = _frame;
    if (now - last_stub > 90) {
        last_stub = now;
        *out_len = 4;
        memcpy(out, _stub_uid, 4);
        return true;
    }
    return false;
#endif
}

static bool nfc_mifare_auth(uint8_t block, const uint8_t key[6]) {
#ifdef NFC_HW_AVAILABLE
    if (!_hw_ok) return false;
    return _pn532.mifare_auth(block, key);
#else
    (void)block; (void)key;
    return false;
#endif
}

static bool nfc_mifare_read_block(uint8_t block, uint8_t *out) {
#ifdef NFC_HW_AVAILABLE
    if (!_hw_ok) return false;
    return _pn532.mifare_read(block, out);
#else
    (void)block;
    memset(out, 0, 16);
    return false;
#endif
}

static bool nfc_emulate_uid(const uint8_t *uid, uint8_t len) {
#ifdef NFC_HW_AVAILABLE
    if (!_hw_ok) return false;
    return _pn532.emulate_uid(uid, len);
#else
    (void)uid; (void)len;
    return false;
#endif
}

static void nfc_emulate_stop() {
#ifdef NFC_HW_AVAILABLE
    if (_hw_ok) _pn532.emulate_stop();
#endif
}

//log helpers
static void log_tag(const uint8_t *uid, uint8_t uid_len, const char *type) {
    if (_log_count >= NFC_TAG_LOG_MAX) {
        // Shift log down, drop oldest
        memmove(&_tag_log[0], &_tag_log[1],
                sizeof(NfcRecord) * (NFC_TAG_LOG_MAX - 1));
        _log_count = NFC_TAG_LOG_MAX - 1;
    }
    NfcRecord *r = &_tag_log[_log_count];
    r->uid_str[0] = '\0';
    for (uint8_t i = 0; i < uid_len; ++i) {
        char part[4];
        snprintf(part, sizeof(part), "%02X%s", uid[i],
                 (i + 1 == uid_len) ? "" : ":");
        strncat(r->uid_str, part,
                sizeof(r->uid_str) - strlen(r->uid_str) - 1);
    }
    strncpy(r->type, type, sizeof(r->type) - 1);
    r->type[sizeof(r->type) - 1] = '\0';
    r->has_block = false;
    ++_log_count;
}

static void uid_to_str(const uint8_t *uid, uint8_t len,
                        char *out, uint8_t out_sz) {
    out[0] = '\0';
    for (uint8_t i = 0; i < len; ++i) {
        char part[4];
        snprintf(part, sizeof(part), "%02X%s", uid[i],
                 (i + 1 == len) ? "" : ":");
        strncat(out, part, out_sz - strlen(out) - 1);
    }
}

//scan tick every frame
static void nfc_scan_tick() {
    if (!_scanning || !_armed) return;
    if (_row == 0) {
        // READ: poll for tags
        uint8_t uid[10], uid_len = 0;
        if (nfc_poll_uid(uid, &uid_len)) {
            char uid_s[NFC_UID_LEN];
            uid_to_str(uid, uid_len, uid_s, sizeof(uid_s));
            if (strcmp(_last_uid, uid_s) != 0) {
                strncpy(_last_uid, uid_s, sizeof(_last_uid) - 1);
                _last_uid[sizeof(_last_uid) - 1] = '\0';
                _last_uid_len = uid_len > sizeof(_last_uid_raw) ? sizeof(_last_uid_raw) : uid_len;
                memcpy(_last_uid_raw, uid, _last_uid_len);
                log_tag(uid, uid_len, "ISO14443");
                snprintf(_status, sizeof(_status), "TAG: %s", uid_s);
            }
        }
    } else if (_row == 2) {
        // KEYS: iterate default dictionary against block 4
        static uint8_t key_idx = 0;
        static bool    key_done = false;
        if (!key_done && _last_uid[0]) {
            if (nfc_mifare_auth(4, MIFARE_DEFAULT_KEYS[key_idx])) {
                uint8_t block_data[16];
                if (nfc_mifare_read_block(4, block_data)) {
                    // Log success
                    snprintf(_status, sizeof(_status),
                             "KEY[%u] OK! blk4:", key_idx);
                }
            }
            ++key_idx;
            if (key_idx >= MIFARE_N_KEYS) {
                key_done = true;
                snprintf(_status, sizeof(_status),
                         "DICT DONE %u keys", MIFARE_N_KEYS);
            }
        }
    }
}

// lifecycle
void app_nfc_init() {
    _row = 0;
    _armed = false;
    _scanning = false;
    _log_count = 0;
    _hist_scroll = 0;
    _last_uid[0] = '\0';
    _last_uid_len = 0;
    nfc_hw_open();
    snprintf(_status, sizeof(_status),
             _hw_ok ? "READY" : "PN532 OFFLINE");
}

void app_nfc_tick() {
    ++_frame;
    nfc_scan_tick();

    // Audit hold-A timer: 3 seconds = ~90 frames at 30 FPS
    if (_hold_arming && _armed) {
        if (_frame - _hold_start > 90) {
            _hold_arming = false;
            // Actually start the armed mode
            _scanning = (_row == 0);
            snprintf(_status, sizeof(_status), "AUDIT ARMED");
        }
    }
}

void app_nfc_suspend() {
    _scanning = false;
    _armed = false;
    if (_row == 1) nfc_emulate_stop();
    nfc_hw_close();
}

//event handler
void app_nfc_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        if (_row == 1 && _armed) nfc_emulate_stop();
        _armed = false;
        _scanning = false;
        _hold_arming = false;
        snprintf(_status, sizeof(_status), "READY");
        return;
    }

    if (e.type == EVT_POT_CHANGED) {
        uint8_t new_row = (e.data * N_ROWS) / 256;
        if (new_row >= N_ROWS) new_row = N_ROWS - 1;
        if (new_row != _row) {
            if (_row == 1 && _armed) nfc_emulate_stop();
            _row = new_row;
        }
        if (_row == 4) {
            // POT scrolls history
            _hist_scroll = (e.data * (_log_count > 6 ? _log_count - 6 : 0)) / 256;
        }
        return;
    }

    if (e.type == EVT_BTN_A_DOWN) {
        // KEYS (row 2) and EMUL (row 1) require 3-second hold
        if (_row == 1 || _row == 2) {
            if (!_armed) {
                _hold_arming = true;
                _hold_start = _frame;
                _armed = true;
                snprintf(_status, sizeof(_status), "HOLD A 3s...");
                return;
            }
        }
        _armed = true;
        _hold_arming = false;
        switch (_row) {
            case 0: // READ
                _scanning = true;
                snprintf(_status, sizeof(_status), "SCANNING...");
                break;
            case 1: // EMUL
                if (_last_uid_len)
                    nfc_emulate_uid(_last_uid_raw, _last_uid_len);
                else
                    nfc_emulate_uid(_stub_uid, 4);
                snprintf(_status, sizeof(_status), "EMULATING UID");
                break;
            case 2: // KEYS
                _scanning = true;
                snprintf(_status, sizeof(_status), "KEY AUDIT...");
                break;
            case 3: // DICT
                snprintf(_status, sizeof(_status),
                         "%u keys loaded", MIFARE_N_KEYS);
                break;
            case 4: // HIST
                _hist_scroll = 0;
                snprintf(_status, sizeof(_status),
                         "%u tags logged", _log_count);
                break;
            case 5: // WRITE
                snprintf(_status, sizeof(_status), "WRITE: TBD");
                break;
            case 6: // ISO14
                snprintf(_status, sizeof(_status), "CRYPTO: TBD");
                break;
        }
        return;
    }

    if (e.type == EVT_BTN_C_DOWN) {
        if (_row == 4) {
            _log_count = 0;
            _last_uid[0] = '\0';
            snprintf(_status, sizeof(_status), "HIST CLEARED");
        }
    }
}

//draw
void app_nfc_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "NFC / RFID", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 36, 8, T_DIM, T_PANEL, FONT_SM, "log:%u", _log_count);

    // Menu rows
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + (int)i * 26;
        if (y > SCR_H - 80) break;
        draw_textf(8, y,
                   (_armed && i == _row) ? T_WARN :
                   (i == _row ? T_FG : T_DIM),
                   T_BG, FONT_SM, "%s %s",
                   (i == _row ? ">" : " "), LABELS[i]);
    }

    // Info panel
    int info_y = SCR_H - 76;
    if (_row == 4 && _log_count > 0) {
        // History view
        uint8_t start = _hist_scroll;
        if (start + 6 > _log_count) {
            start = (_log_count > 6) ? _log_count - 6 : 0;
        }
        uint8_t shown = _log_count - start;
        if (shown > 6) shown = 6;
        for (uint8_t i = 0; i < shown; ++i) {
            NfcRecord *r = &_tag_log[start + i];
            draw_textf(8, info_y + (int)i * 11, T_DIM, T_BG, FONT_SM,
                       "%-24s %s", r->uid_str, r->type);
        }
    } else if (_row == 2) {
        // Key audit info
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
                   "Dictionary: %u default keys", MIFARE_N_KEYS);
        draw_text(8, info_y + 12, "Block 4 target, brute MIFARE", T_DIM, T_BG, FONT_SM);
        draw_text(8, info_y + 24, "Hold A 3s to start audit", T_DIM, T_BG, FONT_SM);
    } else if (_row == 1) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
                   "Emulate UID: %s", _last_uid[0] ? _last_uid : "(scan a tag first)");
        draw_text(8, info_y + 12, "Hold A 3s to arm emulator", T_DIM, T_BG, FONT_SM);
    } else {
        // Default info
        draw_textf(8, info_y, T_DIM, T_BG, FONT_SM, "%s", _status);
        if (_last_uid[0]) {
            draw_textf(8, info_y + 12, T_DIM, T_BG, FONT_SM,
                       "Last: %s", _last_uid);
        }
    }

    // Status bar
    draw_textf(8, SCR_H - 28, _armed ? T_WARN : T_DIM, T_BG, FONT_SM,
               "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12,
              "[A] ARM/SEL  [B] BACK  [C] CLR  POT=row",
              T_DIM, T_BG, FONT_SM);
}
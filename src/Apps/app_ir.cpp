#include "app_ir.h"
#include "ui/draw.h"
#include "ui/theme.h"
#include "hal/hal_storage.h"
#include "config.h"
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <string.h>
#include <stdio.h>

static IRrecv    _rx(PIN_IR_RX, 1024, 50, true);
static IRsend    _tx(PIN_IR_TX);
static decode_results _result;

#define IR_SLOTS 8
typedef struct {
    uint64_t code;
    uint16_t protocol;
    uint16_t bits;
    char     label[14];
} IRRecord;

static IRRecord  _slots[IR_SLOTS];
static uint8_t   _count = 0;
static uint8_t   _cur   = 0;
static char      _status[32] = "READY";
static bool      _learning   = false;
static uint32_t  _frame      = 0;

static void load_ir() {
    _count = 0;
    for (int i=0; i<IR_SLOTS; i++) {
        char key[12]; snprintf(key, sizeof(key), "ir%d", i);
        size_t sz = sizeof(IRRecord);
        if (hal_storage_get_blob(key, &_slots[_count], &sz))
            _count++;
    }
}

static void save_ir(IRRecord &r) {
    if (_count >= IR_SLOTS) { strncpy(_status,"SLOTS FULL",sizeof(_status)); return; }
    _slots[_count] = r;
    char key[12]; snprintf(key, sizeof(key), "ir%d", _count);
    hal_storage_set_blob(key, &r, sizeof(IRRecord));
    _count++;
    snprintf(_status, sizeof(_status), "SAVED [%d]", _count-1);
}

void app_ir_init() {
    _tx.begin();
    _rx.enableIRIn();
    load_ir();
    strncpy(_status, "READY", sizeof(_status));
}

void app_ir_suspend() { _rx.disableIRIn(); }
void app_ir_resume()  { _rx.enableIRIn();  }

void app_ir_event(Event e) {
    if (e.type == EVT_BTN_A_DOWN) {
        _learning = !_learning;
        strncpy(_status, _learning ? "LEARNING..." : "STOPPED",
                sizeof(_status));
        if (_learning) _rx.resume();
    }
    if (e.type == EVT_BTN_C_DOWN && _count > 0) {
        // Send currently selected slot
        IRRecord &r = _slots[_cur];
        _tx.send((decode_type_t)r.protocol, r.code, r.bits);
        snprintf(_status, sizeof(_status), "SENT [%d] %s", _cur, r.label);
    }
    if (e.type == EVT_POT_CHANGED && _count > 0) {
        _cur = (e.data * _count) / 256;
        if (_cur >= _count) _cur = _count - 1;
    }
    _frame++;
}

void app_ir_tick() {
    _frame++;
    if (!_learning) return;
    if (_rx.decode(&_result)) {
        if (_result.decode_type != UNKNOWN &&
            _result.value != REPEAT_64BIT) {
            IRRecord r;
            r.code     = _result.value;
            r.protocol = (uint16_t)_result.decode_type;
            r.bits     = _result.bits;
            snprintf(r.label, sizeof(r.label),
                     "%s", typeToString(_result.decode_type).c_str());
            save_ir(r);
            _learning = false;
        }
        _rx.resume();
    }
}

void app_ir_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "IR REMOTE", T_FG, T_PANEL, FONT_SM);

    if (_learning) {
        // Pulse indicator
        int r = 10 + (int)(_frame % 16);
        draw_circle(40, 80, r,   T_FG);
        draw_circle(40, 80, r+8, T_BORDER);
        draw_text(60, 74, "WAITING FOR IR...", T_FG, T_BG, FONT_SM);
    }

    // Slots
    draw_hline(8, 100, SCR_W-16, T_BORDER);
    draw_text(8, 106, "LIBRARY:", T_DIM, T_BG, FONT_SM);
    for (int i=0; i<_count; i++) {
        bool sel = (i == _cur);
        draw_textf(8, 118+i*14,
                   sel ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s[%d] %-10s %08llX (%db)",
                   sel?">":" ", i,
                   _slots[i].label,
                   (unsigned long long)_slots[i].code,
                   _slots[i].bits);
    }
    if (_count == 0)
        draw_text(8, 118, "no codes saved — press A to learn",
                  T_BORDER, T_BG, FONT_SM);

    draw_hline(0, SCR_H-26, SCR_W, T_BORDER);
    draw_fill(0, SCR_H-26, SCR_W, 26, T_PANEL);
    draw_text(8, SCR_H-16, _status, T_FG, T_PANEL, FONT_SM);
    draw_text(SCR_W-196, SCR_H-16,
              "[A]LEARN  [C]SEND  POT=select  [B]BACK",
              T_DIM, T_PANEL, FONT_SM);
}
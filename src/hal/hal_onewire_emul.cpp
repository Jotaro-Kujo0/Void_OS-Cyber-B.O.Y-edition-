// hal_onewire_emul.cpp — skeleton. See hal_onewire_emul.h for the full API + roadmap.
//
// Base behaviour: every read returns false; the slot list is empty. The
// real driver bit-bangs PIN_ONEWIRE using libgpiod "set-line-value" and
// decodes the DS1990A 1-Wire protocol at ~30 µs granularity.

#include "hal_onewire_emul.h"

static Ds1990Mode _mode = DS1990_OFF;
static uint8_t    _slot_count = 0;

void     hal_onewire_emul_init()                  { _mode = DS1990_OFF; _slot_count = 0; }
void     hal_onewire_emul_tick()                  {}

void     hal_onewire_emul_set_mode(Ds1990Mode m)  { _mode = m; }
Ds1990Mode hal_onewire_emul_get_mode()            { return _mode; }

bool     hal_onewire_emul_pop(IButtonRom *)      { return false; }
bool     hal_onewire_emul_save(const IButtonRom *r) { (void)r; return false; }
bool     hal_onewire_emul_recall(uint8_t, IButtonRom *) { return false; }
uint8_t  hal_onewire_emul_slot_count()            { return _slot_count; }

void     hal_onewire_emul_format(const IButtonRom *r, char *out, uint8_t out_len) {
    (void)r;
    if (out_len) out[0] = 0;
}
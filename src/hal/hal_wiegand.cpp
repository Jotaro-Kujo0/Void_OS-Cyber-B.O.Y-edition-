// hal_wiegand.cpp — skeleton. See hal_wiegand.h for the full API + roadmap.
//
// Base behaviour: capture is disabled and every pop() returns false. The
// real driver polls PIN_WIEGAND_D0/D1 every ~200 µs, verifies even/odd
// parity, and commits a frame when the inter-bit gap exceeds 100 ms.

#include "hal_wiegand.h"

static bool _enabled = false;

void     hal_wiegand_init()                          {}
void     hal_wiegand_tick()                          {}

bool     hal_wiegand_pop(WiegandFrame *)             { return false; }
void     hal_wiegand_format(const WiegandFrame *f, char *out, uint8_t out_len) {
    // Skeleton: just zero the output. The real formatter is a hex dump
    // of `f->bits` with `f->length` nibbles of leading zero.
    (void)f;
    if (out_len) out[0] = 0;
}

uint32_t hal_wiegand_total_frames()                  { return 0; }
uint32_t hal_wiegand_parity_errors()                 { return 0; }

void     hal_wiegand_set_enabled(bool enabled)       { _enabled = enabled; }
bool     hal_wiegand_is_enabled()                    { return _enabled; }
// hal_pocsag.h — POCSAG pager decoder (slot-independent).
//
// Pure bit-level decoder: feed demodulated bits in, pop decoded messages
// out. No radio or platform dependencies, so it is unit-testable on its
// own. POCSAG is NRZ 2-FSK; see hal_pocsag.cpp for the protocol notes.

#pragma once
#include <stdint.h>
#include "hal_radio.h"   // RadioPocsagMsg

void hal_pocsag_reset();
void hal_pocsag_feed_bit(uint8_t bit, uint32_t now_ms);
bool hal_pocsag_pop(RadioPocsagMsg *out);

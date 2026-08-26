// hal_mosfet.cpp — skeleton. See hal_mosfet.h for the full API + roadmap.
//
// Base behaviour: every MOSFET is OFF. The real driver reserves a libgpiod
// line handle per channel and writes high/low.

#include "hal_mosfet.h"

static bool _state[MOSFET_COUNT] = {};

void hal_mosfet_init()                       { for (auto &s : _state) s = false; }
void hal_mosfet_set(MosfetChannel ch, MosfetState st) {
    if (ch < MOSFET_COUNT) _state[ch] = (st == MOSFET_ON);
}
MosfetState hal_mosfet_get(MosfetChannel ch) {
    return (ch < MOSFET_COUNT && _state[ch]) ? MOSFET_ON : MOSFET_OFF;
}
void hal_mosfet_isolate_all()                { for (auto &s : _state) s = false; }
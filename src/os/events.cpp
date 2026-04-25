// events.cpp
#include "events.h"

static volatile Event   _q[EQ_SIZE];
static volatile uint8_t _h = 0, _t = 0;

void events_init()  { _h = _t = 0; }
void events_clear() { _h = _t = 0; }

void IRAM_ATTR events_push(Event e) {
    uint8_t next = (_t + 1) % EQ_SIZE;
    if (next != _h) { _q[_t] = e; _t = next; }
}

Event events_pop() {
    if (_h == _t) return {EVT_NONE, 0};
    Event e = _q[_h]; _h = (_h + 1) % EQ_SIZE; return e;
}

bool events_empty() { return _h == _t; }
#include "events.h"

// Define the variables here, NOT in the header
#define EVENT_BUF_SIZE 16
static Event _q[EVENT_BUF_SIZE];
static uint8_t _h = 0; // head
static uint8_t _t = 0; // tail

void events_init() {
    _h = 0;
    _t = 0;
}

void events_push(Event e) {
    _q[_t] = e;
    _t = (_t + 1) % EVENT_BUF_SIZE;
}

Event events_pop() {
    Event e = _q[_h];
    _h = (_h + 1) % EVENT_BUF_SIZE;
    return e;
}

bool events_empty() {
    return _h == _t;
}
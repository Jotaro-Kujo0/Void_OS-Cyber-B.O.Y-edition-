#include "events.h"

#define EVENT_BUF_SIZE 16
static Event _q[EVENT_BUF_SIZE];
static uint8_t _h = 0;
static uint8_t _t = 0;
static uint8_t _count = 0;

void events_init() { _h = 0; _t = 0; _count = 0; }

void events_push(Event e) {
    // Drop the oldest event when the producer outruns the UI task.
    if (_count == EVENT_BUF_SIZE) {
        _h = static_cast<uint8_t>((_h + 1) % EVENT_BUF_SIZE);
        --_count;
    }
    _q[_t] = e;
    _t = static_cast<uint8_t>((_t + 1) % EVENT_BUF_SIZE);
    ++_count;
}

Event events_pop() {
    if (_count == 0) return {EVT_NONE, 0};
    Event e = _q[_h];
    _h = static_cast<uint8_t>((_h + 1) % EVENT_BUF_SIZE);
    --_count;
    return e;
}

bool events_empty() { return _count == 0; }

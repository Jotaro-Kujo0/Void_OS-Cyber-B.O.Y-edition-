#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>

typedef enum {
    EVT_NONE,
    EVT_BTN_A_DOWN,
    EVT_BTN_B_DOWN,
    EVT_BTN_C_DOWN,
    EVT_BTN_A_UP,
    EVT_BTN_B_UP,
    EVT_BTN_C_UP,
    EVT_POT_CHANGED,
    EVT_TOUCH_DOWN,
    EVT_TOUCH_UP,
    EVT_TOUCH_COORD,
} EventType;

typedef struct {
    EventType type;
    uint32_t  data;
} Event;

void events_init();
void events_push(Event e);
Event events_pop();
bool events_empty();

#endif // EVENTS_H
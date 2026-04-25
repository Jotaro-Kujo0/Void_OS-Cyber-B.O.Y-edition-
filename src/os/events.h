// events.h
#pragma once
#include <stdint.h>

typedef enum : uint8_t {
    EVT_NONE = 0,
    EVT_BTN_A_DOWN, EVT_BTN_A_UP,
    EVT_BTN_B_DOWN, EVT_BTN_B_UP,
    EVT_BTN_C_DOWN, EVT_BTN_C_UP,
    EVT_POT_CHANGED,
    EVT_TICK,
    EVT_WAKE,
} EventType;

typedef struct { EventType type; uint8_t data; } Event;

#define EQ_SIZE 64

void  events_init();
void  events_push(Event e);
Event events_pop();
bool  events_empty();
void  events_clear();
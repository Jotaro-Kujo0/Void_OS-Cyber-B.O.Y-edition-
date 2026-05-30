// screen.h
#pragma once
#include <stddef.h>
#include "widget.h"
#define SCREEN_MAX_WIDGETS 16
#define SCREEN_STACK_DEPTH  8

typedef struct Screen Screen;
struct Screen {
    Widget  *widgets[SCREEN_MAX_WIDGETS];
    uint8_t  widget_count;
    uint8_t  focused_idx;   // focused widget

    void (*on_enter)(Screen *self); // when screen becomes active
    void (*on_exit) (Screen *self); // when leaving
};

// ---- Screen stack (your navigation history) ----
static Screen *_stack[SCREEN_STACK_DEPTH];
static int     _stack_top = -1;

static inline Screen *screen_current(void) {
    return (_stack_top >= 0) ? _stack[_stack_top] : NULL;
}

void screen_push(Screen *s) {
    if (_stack_top < SCREEN_STACK_DEPTH - 1) {
        _stack[++_stack_top] = s;
        if (s->on_enter) s->on_enter(s);
    }
}

void screen_pop(void) {
    if (_stack_top >= 0) {
        Screen *s = _stack[_stack_top--];
        if (s->on_exit) s->on_exit(s);
        Screen *next = screen_current();
        if (next && next->on_enter) next->on_enter(next);
    }
}

void screen_add_widget(Screen *s, Widget *w) {
    if (s->widget_count < SCREEN_MAX_WIDGETS)
        s->widgets[s->widget_count++] = w;
}

void screen_draw(Screen *s, uint16_t *fb) {
    for (uint8_t i = 0; i < s->widget_count; i++)
        widget_draw(s->widgets[i], fb);
}

void screen_dispatch_event(Screen *s, Event e) {
    // Tab focus with encoder button combo, or just give to focused widget
    if (s->widget_count == 0) return;
    Widget *fw = s->widgets[s->focused_idx];
    fw->focused = true;
    widget_event(fw, e);
}
// widget.h — the foundation of the UI
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- Event types ----
typedef enum {
    EVT_NONE = 0,
    EVT_ENC_UP,       // rotary encoder clockwise
    EVT_ENC_DOWN,     // rotary encoder counter-clockwise
    EVT_BTN_A,        // confirm / select
    EVT_BTN_B,        // back / cancel
    EVT_BTN_C,        // context action
    EVT_TICK,         // called every frame for animations
} EventType;

typedef struct { EventType type; } Event;

// ---- The widget struct ----
// Every widget in your OS is one of these.
typedef struct Widget Widget;
struct Widget {
    int16_t  x, y;          // position on screen
    int16_t  w, h;          // size
    bool     focused;        // receives input events when true
    bool     visible;

    // Every widget implements these — or leaves them NULL
    void  (*draw)    (Widget *self, uint16_t *fb);
    bool  (*on_event)(Widget *self, Event e);
    void  (*destroy) (Widget *self);  // free any dynamic sub-data

    void  *data;             // widget-specific data (cast inside draw/on_event)
};

// Convenience: draw a widget if visible
static inline void widget_draw(Widget *w, uint16_t *fb) {
    if (w && w->visible && w->draw) w->draw(w, fb);
}
static inline bool widget_event(Widget *w, Event e) {
    if (w && w->focused && w->on_event) return w->on_event(w, e);
    return false;
}
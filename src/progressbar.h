// widgets/progressbar.h
#pragma once
#include "widget.h"

typedef struct {
    float    value;      // 0.0 to 1.0
    uint16_t fg_color;
    uint16_t bg_color;
    bool     animated;   // smoothly lerp toward target
    float    target;
} ProgressBarData;

extern void fill_rect(uint16_t *fb, int x, int y,
                      int w, int h, uint16_t color);
extern void draw_rect_outline(uint16_t *fb, int x, int y,
                               int w, int h, uint16_t color);

static void progressbar_draw(Widget *self, uint16_t *fb) {
    ProgressBarData *d = (ProgressBarData *)self->data;

    // Background
    fill_rect(fb, self->x, self->y, self->w, self->h, d->bg_color);

    // Fill (clamp value 0–1)
    float v = d->value;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    int fill_w = (int)(self->w * v);
    if (fill_w > 0)
        fill_rect(fb, self->x, self->y, fill_w, self->h, d->fg_color);

    // Border
    draw_rect_outline(fb, self->x, self->y, self->w, self->h, d->fg_color);
}

static bool progressbar_event(Widget *self, Event e) {
    ProgressBarData *d = (ProgressBarData *)self->data;
    if (e.type == EVT_TICK && d->animated) {
        // Smooth lerp toward target (tweak 0.15f for speed)
        d->value += (d->target - d->value) * 0.15f;
    }
    return false; // progress bars don't consume events
}

static inline Widget make_progressbar(int16_t x, int16_t y,
                                       int16_t w, int16_t h,
                                       uint16_t fg, uint16_t bg) {
    static ProgressBarData _d;
    _d = (ProgressBarData){
        .value = 0.5f, .target = 0.5f,
        .fg_color = fg, .bg_color = bg,
        .animated = true
    };
    return (Widget){
        .x=x, .y=y, .w=w, .h=h,
        .visible=true, .focused=false,
        .draw = progressbar_draw,
        .on_event = progressbar_event,
        .data = &_d
    };
}
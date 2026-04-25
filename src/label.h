// widgets/label.h
#pragma once
#include "widget.h"

typedef struct {
    const char *text;
    uint16_t    color;
    uint8_t     font_scale; // 1 = normal, 2 = double size
} LabelData;

// Forward declarations (your font renderer)
extern void draw_string(uint16_t *fb, int x, int y,
                        const char *str, uint16_t color, uint8_t scale);

static void label_draw(Widget *self, uint16_t *fb) {
    LabelData *d = (LabelData *)self->data;
    draw_string(fb, self->x, self->y, d->text, d->color, d->font_scale);
}

// Factory function — creates a label widget
static inline Widget make_label(int16_t x, int16_t y,
                                const char *text, uint16_t color) {
    static LabelData _d; // one static per widget in simple cases
    _d = (LabelData){ .text = text, .color = color, .font_scale = 1 };
    return (Widget){
        .x = x, .y = y, .w = 0, .h = 8,
        .visible = true, .focused = false,
        .draw = label_draw,
        .data = &_d
    };
}
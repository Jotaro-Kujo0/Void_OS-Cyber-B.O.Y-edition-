// widgets/menu.h
#pragma once
#include "widget.h"
#define MENU_MAX_ITEMS 16

typedef struct {
    const char *items[MENU_MAX_ITEMS];
    uint8_t     count;
    uint8_t     selected;   // currently highlighted
    uint8_t     scroll_off; // for long lists
    uint16_t    fg, bg, sel_color;
    void (*on_select)(uint8_t index); // callback when A pressed
} MenuData;

extern void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
extern void draw_string(uint16_t *fb, int x, int y,
                         const char *s, uint16_t color, uint8_t scale);

#define MENU_ROW_H 14
#define MENU_VISIBLE_ROWS 10

static void menu_draw(Widget *self, uint16_t *fb) {
    MenuData *d = (MenuData *)self->data;
    fill_rect(fb, self->x, self->y, self->w, self->h, d->bg);

    for (uint8_t i = 0; i < MENU_VISIBLE_ROWS; i++) {
        uint8_t idx = d->scroll_off + i;
        if (idx >= d->count) break;

        int row_y = self->y + i * MENU_ROW_H;
        uint16_t txt_color = d->fg;

        if (idx == d->selected) {
            // Highlight bar
            fill_rect(fb, self->x, row_y, self->w, MENU_ROW_H, d->sel_color);
            txt_color = d->bg; // invert text on selection
        }
        draw_string(fb, self->x + 4, row_y + 3,
                    d->items[idx], txt_color, 1);
    }
}

static bool menu_event(Widget *self, Event e) {
    MenuData *d = (MenuData *)self->data;

    if (e.type == EVT_ENC_DOWN && d->selected < d->count - 1) {
        d->selected++;
        // Scroll down if selection goes off screen
        if (d->selected >= d->scroll_off + MENU_VISIBLE_ROWS)
            d->scroll_off++;
        return true;
    }
    if (e.type == EVT_ENC_UP && d->selected > 0) {
        d->selected--;
        if (d->selected < d->scroll_off)
            d->scroll_off--;
        return true;
    }
    if (e.type == EVT_BTN_A) {
        if (d->on_select) d->on_select(d->selected);
        return true;
    }
    return false;
}
// main.c
#include "st7789.h"
#include "widget.h"
#include "screen.h"
//#include "screens/stat_screen.h"
//#include "screens/menu_screen.h"

// Two framebuffers
static uint16_t fb0[ST7789_WIDTH * ST7789_HEIGHT];
static uint16_t fb1[ST7789_WIDTH * ST7789_HEIGHT];
static uint16_t *back_buf  = fb0;
static uint16_t *front_buf = fb1;

int main(void) {
    // --- Init hardware ---
    st7789_init();
    gpio_init_buttons();   // your button/encoder setup
    st7789_init_dma();

    // --- Build first screen ---
    Screen *main_menu = create_main_menu_screen(); // your factory fn
    screen_push(main_menu);

    // --- Main loop ---
    while (1) {
        // 1. Clear back buffer
        for (int i = 0; i < ST7789_WIDTH * ST7789_HEIGHT; i++)
            back_buf[i] = COLOR_BLACK;

        // 2. Dispatch any queued input events
        Event e = input_poll(); // reads from your ISR ring buffer
        if (e.type != EVT_NONE) {
            if (e.type == EVT_BTN_B)
                screen_pop();     // B always goes back
            else
                screen_dispatch_event(screen_current(), e);
        }

        // 3. Tick all widgets (for animations)
        Event tick = { .type = EVT_TICK };
        screen_dispatch_event(screen_current(), tick);

        // 4. Draw current screen
        screen_draw(screen_current(), back_buf);

        // 5. Wait for previous DMA to finish, then swap & send
        while (!st7789_flush_done());
        uint16_t *tmp = front_buf;
        front_buf = back_buf;
        back_buf  = tmp;
        st7789_flush_dma(front_buf);
    }
}
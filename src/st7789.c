// st7789.c
#include "st7789.h"
#include "hardware/spi.h"   // swap for your MCU's SPI header
#include "hardware/gpio.h"
#include "pico/time.h"

#define PIN_CS  17
#define PIN_DC  20
#define PIN_RST 21
#define PIN_BL  22
#define SPI_PORT spi0

// --- low-level helpers ---

static inline void cs_low()  { gpio_put(PIN_CS, 0); }
static inline void cs_high() { gpio_put(PIN_CS, 1); }
static inline void dc_cmd()  { gpio_put(PIN_DC, 0); } // command mode
static inline void dc_data() { gpio_put(PIN_DC, 1); } // data mode

static void write_cmd(uint8_t cmd) {
    cs_low();
    dc_cmd();
    spi_write_blocking(SPI_PORT, &cmd, 1);
    cs_high();
}

static void write_data(uint8_t *data, size_t len) {
    cs_low();
    dc_data();
    spi_write_blocking(SPI_PORT, data, len);
    cs_high();
}

static void write_data8(uint8_t d) {
    write_data(&d, 1);
}

static void write_data16(uint16_t d) {
    uint8_t buf[2] = { d >> 8, d & 0xFF }; // big-endian!
    write_data(buf, 2);
}

void st7789_init(void) {
    // SPI at 62.5 MHz (RP2040 max for ST7789)
    spi_init(SPI_PORT, 62500000);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);

    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS,  GPIO_OUT); gpio_put(PIN_CS,  1);
    gpio_init(PIN_DC);  gpio_set_dir(PIN_DC,  GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_init(PIN_BL);  gpio_set_dir(PIN_BL,  GPIO_OUT); gpio_put(PIN_BL, 1);

    // Hardware reset
    gpio_put(PIN_RST, 1); sleep_ms(10);
    gpio_put(PIN_RST, 0); sleep_ms(10);
    gpio_put(PIN_RST, 1); sleep_ms(120);

    write_cmd(0x01);  // SWRESET — software reset
    sleep_ms(150);
    write_cmd(0x11);  // SLPOUT — wake from sleep
    sleep_ms(120);

    // COLMOD — pixel format: 0x55 = 16-bit RGB565
    write_cmd(0x3A); write_data8(0x55);

    // MADCTL — memory data access control
    // 0x00 = portrait, top-left origin
    // 0x60 = landscape. Flip bits 5,6,7 to rotate.
    write_cmd(0x36); write_data8(0x00);

    // Porch setting (required for stability)
    write_cmd(0xB2);
    uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    write_data(porch, 5);

    // Gate control
    write_cmd(0xB7); write_data8(0x35);

    // VCOM setting
    write_cmd(0xBB); write_data8(0x19);

    // LCM control
    write_cmd(0xC0); write_data8(0x2C);

    // VDV/VRH enable
    write_cmd(0xC2); write_data8(0x01);
    write_cmd(0xC3); write_data8(0x12);
    write_cmd(0xC4); write_data8(0x20);

    // Frame rate: 60Hz
    write_cmd(0xC6); write_data8(0x0F);

    // Power control
    write_cmd(0xD0);
    uint8_t pwr[] = {0xA4, 0xA1};
    write_data(pwr, 2);

    // Positive gamma
    write_cmd(0xE0);
    uint8_t pgamma[] = {0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23};
    write_data(pgamma, 14);

    // Negative gamma
    write_cmd(0xE1);
    uint8_t ngamma[] = {0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23};
    write_data(ngamma, 14);

    write_cmd(0x21);  // INVON — inversion (needed for correct colors on most ST7789 panels)
    write_cmd(0x29);  // DISPON — display on
    sleep_ms(120);
}

void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // CASET — column address (x range)
    write_cmd(0x2A);
    write_data16(x0);
    write_data16(x1);

    // RASET — row address (y range)
    write_cmd(0x2B);
    write_data16(y0);
    write_data16(y1);

    // RAMWR — begin writing pixels
    write_cmd(0x2C);
}

void st7789_flush(uint16_t *fb) {
    st7789_set_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    // Now send all 153,600 bytes
    cs_low();
    dc_data();
    // Each pixel is 2 bytes, big-endian
    // IMPORTANT: swap bytes if your CPU is little-endian
    spi_write_blocking(SPI_PORT, (uint8_t*)fb, ST7789_BUFSIZE * 2);
    cs_high();
}

static uint16_t fb0[ST7789_BUFSIZE];
static uint16_t fb1[ST7789_BUFSIZE];
static uint16_t *back  = fb0;  // you draw here
static uint16_t *front = fb1;  // DMA sends this

// Draw a pixel into the back buffer
void fb_set_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= ST7789_WIDTH) return;
    if (y < 0 || y >= ST7789_HEIGHT) return;
    back[y * ST7789_WIDTH + x] = color;
}

// Clear the back buffer
void fb_clear(uint16_t color) {
    for (int i = 0; i < ST7789_BUFSIZE; i++) back[i] = color;
}

// Swap buffers and send to display
void fb_present(void) {
    uint16_t *tmp = front;
    front = back;
    back  = tmp;
    st7789_flush(front);
}

int main(void) {
    st7789_init();

    while (1) {
        fb_clear(COLOR_BLACK);

        // All your draw calls write into `back`
        fb_set_pixel(120, 160, COLOR_GREEN);
        draw_rect(10, 10, 100, 50, COLOR_GREEN);
        draw_text("STATS", 20, 20, COLOR_GREEN);

        fb_present(); // swap + DMA to display
    }
} 
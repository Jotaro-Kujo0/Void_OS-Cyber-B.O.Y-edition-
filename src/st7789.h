// st7789.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ST7789_WIDTH  240
#define ST7789_HEIGHT 320
#define ST7789_BUFSIZE (ST7789_WIDTH * ST7789_HEIGHT)

// RGB888 → RGB565 packing (call this at compile time for constants)
#define RGB(r,g,b) (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

// Colors
#define COLOR_BLACK   RGB(0,0,0)
#define COLOR_WHITE   RGB(255,255,255)
#define COLOR_GREEN   RGB(57,255,20)   // Pipboy green

void st7789_init(void);
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void st7789_write_pixels(uint16_t *buf, uint32_t count);
void st7789_flush(uint16_t *framebuffer); // sends full screen

#include "hardware/dma.h"

static int dma_chan;

void st7789_init_dma(void) {
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_dreq(&cfg, spi_get_dreq(SPI_PORT, true));
    dma_channel_configure(dma_chan, &cfg,
        &spi_get_hw(SPI_PORT)->dr,  // destination: SPI TX FIFO
        NULL,                        // source: set per-transfer
        ST7789_BUFSIZE * 2,          // bytes
        false                        // don't start yet
    );
}

void st7789_flush_dma(uint16_t *fb) {
    st7789_set_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    cs_low(); dc_data();
    dma_channel_set_read_addr(dma_chan, fb, true); // true = start
}

bool st7789_flush_done(void) {
    return !dma_channel_is_busy(dma_chan);
}
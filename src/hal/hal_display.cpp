// hal_display.cpp
#include "hal_display.h"
#include "config.h"

TFT_eSPI tft = TFT_eSPI();

void hal_display_init() {
    tft.init();
    tft.setRotation(1);             // landscape
    tft.fillScreen(TFT_BLACK);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_BL, 0);
    hal_display_bl_set(BL_FULL);
}

void hal_display_bl_set(uint8_t val) {
    ledcWrite(0, val);
}

uint16_t hal_display_color(uint8_t r, uint8_t g, uint8_t b) {
    return tft.color565(r, g, b);
}

void hal_display_set_window(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h) {
    tft.setAddrWindow(x, y, w, h);
}

void hal_display_push_pixels(const uint16_t *buf, uint32_t count) {
    tft.startWrite();
    tft.pushPixels((uint16_t *)buf, count);
    tft.endWrite();
}

void hal_display_push_pixel(uint16_t color, uint32_t count) {
    tft.startWrite();
    tft.pushColor(color, count);
    tft.endWrite();
}
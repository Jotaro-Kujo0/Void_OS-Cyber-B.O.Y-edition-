// hal_display.h
#pragma once
#include <stdint.h>
#include <TFT_eSPI.h> // Include the TFT_eSPI library for display control
// Arduino.h may not be available in non-Arduino build environments
#ifdef ARDUINO
#include <Arduino.h>
#endif

// Forward-declare TFT_eSPI to avoid pulling in TFT_eSPI.h and Arduino.h
// from this header. The actual TFT_eSPI definition should be provided by the
// HAL implementation or platform-specific source file.
// Note: TFT_eSPI is exposed here for use by other files that need display access
class TFT_eSPI;
extern TFT_eSPI tft;

void     hal_display_init();
void     hal_display_bl_set(uint8_t val);   // 0–255
uint16_t hal_display_color(uint8_t r, uint8_t g, uint8_t b);
void     hal_display_set_window(uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h);
void     hal_display_push_pixels(const uint16_t *buf, uint32_t count);
void     hal_display_push_pixel(uint16_t color, uint32_t count);
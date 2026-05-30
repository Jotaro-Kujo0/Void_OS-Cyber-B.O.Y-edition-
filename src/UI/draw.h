// draw.h
#pragma once
#include <stdint.h>

void draw_init();
void draw_pixel  (int x, int y, uint16_t c);
void draw_hline  (int x, int y, int w, uint16_t c);
void draw_vline  (int x, int y, int h, uint16_t c);
void draw_rect   (int x, int y, int w, int h, uint16_t c);
void draw_fill   (int x, int y, int w, int h, uint16_t c);
void draw_circle (int cx, int cy, int r, uint16_t c);
void draw_fcircle(int cx, int cy, int r, uint16_t c);
void draw_line   (int x0, int y0, int x1, int y1, uint16_t c);
void draw_clear  (uint16_t c);

// Text — wraps TFT_eSPI, bg=0x0000 means transparent bg
void draw_text(int x, int y, const char *s,
               uint16_t fg, uint16_t bg, uint8_t sz);

// Formatted text — printf-style
void draw_textf(int x, int y, uint16_t fg, uint16_t bg,
                uint8_t sz, const char *fmt, ...);

// Progress bar (filled + border)
void draw_bar(int x, int y, int w, int h,
              float v,       // 0.0–1.0
              uint16_t fg, uint16_t bg);

// Sprite blit with transparency key
void draw_sprite(int dest_x, int dest_y,
                 const uint16_t *data,
                 int src_x, int src_y,
                 int frame_w, int frame_h,
                 int sheet_w,
                 uint16_t transparent_key);

// JPG image loading and rendering (TFT_eSPI TJPG decoder)
// Returns true if successful, false if file not found or decode failed
bool draw_jpg(int x, int y, const char *filename);
bool draw_jpg_scaled(int x, int y, int scale, const char *filename);
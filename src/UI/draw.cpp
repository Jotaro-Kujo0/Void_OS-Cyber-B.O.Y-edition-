// draw.cpp
#include "draw.h"
#include "theme.h"
#include "hal/hal_display.h"
#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>

void draw_init()  { hal_display_init(); }
void draw_clear(uint16_t c)  { tft.fillScreen(c); }
void draw_pixel(int x,int y,uint16_t c) { tft.drawPixel(x,y,c); }
void draw_hline(int x,int y,int w,uint16_t c) { tft.drawFastHLine(x,y,w,c); }
void draw_vline(int x,int y,int h,uint16_t c) { tft.drawFastVLine(x,y,h,c); }
void draw_rect (int x,int y,int w,int h,uint16_t c) { tft.drawRect(x,y,w,h,c); }
void draw_fill (int x,int y,int w,int h,uint16_t c) { tft.fillRect(x,y,w,h,c); }
void draw_circle (int cx,int cy,int r,uint16_t c) { tft.drawCircle(cx,cy,r,c); }
void draw_fcircle(int cx,int cy,int r,uint16_t c) { tft.fillCircle(cx,cy,r,c); }
void draw_line(int x0,int y0,int x1,int y1,uint16_t c) { tft.drawLine(x0,y0,x1,y1,c); }

void draw_text(int x, int y, const char *s,
               uint16_t fg, uint16_t bg, uint8_t sz) {
    tft.setTextColor(fg, bg);
    tft.setTextSize(sz);
    tft.setCursor(x, y);
    tft.print(s);
}

void draw_textf(int x, int y, uint16_t fg, uint16_t bg,
                uint8_t sz, const char *fmt, ...) {
    char buf[64];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    draw_text(x, y, buf, fg, bg, sz);
}

void draw_bar(int x, int y, int w, int h,
              float v, uint16_t fg, uint16_t bg) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    tft.fillRect(x, y, w, h, bg);
    int fw = (int)(w * v);
    if (fw > 0) tft.fillRect(x, y, fw, h, fg);
    tft.drawRect(x, y, w, h, fg);
}

void draw_sprite(int dx, int dy,
                 const uint16_t *data,
                 int sx, int sy,
                 int fw, int fh, int sw,
                 uint16_t tk) {
    tft.startWrite();
    for (int row = 0; row < fh; row++) {
        for (int col = 0; col < fw; col++) {
            uint16_t px = pgm_read_word(
                &data[(sy + row) * sw + sx + col]);
            if (px == tk) continue;   // transparent
            tft.drawPixel(dx + col, dy + row, px);
        }
    }
    tft.endWrite();
}
// JPG loading.
//
// Both targets now route through the unified `media.h` dispatcher
// (`Media` handle + `media_render`). The dispatcher's Pi 5 path
// pipes JPG -> ImageMagick -> BMP and recurses through the BMP
// decoder; ESP32 path uses BitBank's `TJpgDec` (see media.cpp for
// platformio.deps). This keeps `draw_jpg` as a thin shim used by
// the existing sprite/character code and the new media API.

#include "media.h"

bool draw_jpg(int x, int y, const char *filename) {
    Media *m = media_open(filename);
    if (!m) return false;
    bool ok = media_render(m, x, y);
    media_close(m);
    return ok;
}

bool draw_jpg_scaled(int x, int y, int scale, const char *filename) {
    (void)scale;
    return draw_jpg(x, y, filename);
}

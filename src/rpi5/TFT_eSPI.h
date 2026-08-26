#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif
#ifndef TFT_WHITE
#define TFT_WHITE 0xFFFF
#endif
#ifndef TFT_GREEN
#define TFT_GREEN 0x07E0
#endif

class TFT_eSPI {
public:
    TFT_eSPI();
    ~TFT_eSPI();

    void init();
    void setRotation(uint8_t rotation);
    void fillScreen(uint16_t color);
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b) const;

    void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void startWrite();
    void endWrite();
    void pushPixels(uint16_t *data, uint32_t count);
    void pushColor(uint16_t color, uint32_t count);
    void pushImageDMA(int x, int y, int w, int h, const uint16_t *data);
    void readRect(int x, int y, int w, int h, uint16_t *data) const;

    void drawPixel(int x, int y, uint16_t color);
    void drawFastHLine(int x, int y, int w, uint16_t color);
    void drawFastVLine(int x, int y, int h, uint16_t color);
    void drawRect(int x, int y, int w, int h, uint16_t color);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void drawCircle(int x, int y, int radius, uint16_t color);
    void fillCircle(int x, int y, int radius, uint16_t color);
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color);

    void setTextColor(uint16_t foreground, uint16_t background);
    void setTextSize(uint8_t size);
    void setCursor(int x, int y);
    void print(const char *text);

    void present();

private:
    int _spi_fd;
    uint8_t _rotation;
    uint16_t _window_x;
    uint16_t _window_y;
    uint16_t _window_w;
    uint16_t _window_h;
    uint16_t _text_fg;
    uint16_t _text_bg;
    uint8_t _text_size;
    int _cursor_x;
    int _cursor_y;
    std::vector<uint16_t> _framebuffer;

    void command(uint8_t value);
    void data(const uint8_t *bytes, std::size_t length);
    void reset_panel();
    void draw_char(int x, int y, char character);
    void draw_circle_point(int cx, int cy, int x, int y, uint16_t color);
    bool in_bounds(int x, int y) const;
};

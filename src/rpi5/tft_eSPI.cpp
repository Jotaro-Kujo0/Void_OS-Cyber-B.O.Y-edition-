#include "TFT_eSPI.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../config.h"
#include "Arduino.h"

namespace {
constexpr const char *SPI_DEVICE = "/dev/spidev0.0";
constexpr int SPI_SPEED_HZ = 40000000;
constexpr int GPIO_DC = PIN_TFT_DC;
constexpr int GPIO_RESET = PIN_TFT_RST;
static int gpio_chip_fd = -1;
static int gpio_dc_fd = -1;
static int gpio_reset_fd = -1;

int request_gpio_line(int gpio) {
    // Pi 5 exposes the RP1 GPIO controller as gpiochip4 on Raspberry Pi OS.
    // Try it first, then fall back to other gpiochips for alternate kernels.
    const int chips[] = {4, 0, 1, 2, 3, 5, 6, 7, 8};
    for (int chip : chips) {
        const std::string path = "/dev/gpiochip" + std::to_string(chip);
        int chip_fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (chip_fd < 0) continue;
        gpiohandle_request request{};
        request.lineoffsets[0] = static_cast<unsigned int>(gpio);
        request.flags = GPIOHANDLE_REQUEST_OUTPUT;
        request.default_values[0] = 0;
        std::strncpy(request.consumer_label, "void-os", GPIO_MAX_NAME_SIZE - 1);
        if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &request) == 0) {
            if (gpio_chip_fd < 0) gpio_chip_fd = chip_fd;
            else close(chip_fd);
            return request.fd;
        }
        close(chip_fd);
    }
    return -1;
}

void gpio_write(int gpio, int value) {
    int &line_fd = (gpio == GPIO_DC) ? gpio_dc_fd : gpio_reset_fd;
    if (line_fd < 0) line_fd = request_gpio_line(gpio);
    if (line_fd >= 0) {
        gpiohandle_data data{};
        data.values[0] = value ? 1 : 0;
        (void)ioctl(line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    }
}

void gpio_prepare(int gpio) {
    // Requesting the line lazily in gpio_write avoids requiring root and works
    // with the modern GPIO character-device API used by Raspberry Pi OS.
    (void)gpio;
}

int rgb565_to_ansi256(uint16_t c) {
    // Nearest 6x6x6 cube entry of the xterm 256-color palette.
    const int r = ((c >> 11) & 0x1F) * 255 / 31;
    const int g = ((c >> 5) & 0x3F) * 255 / 63;
    const int b = (c & 0x1F) * 255 / 31;
    return 16 + 36 * ((r * 5 + 127) / 255) + 6 * ((g * 5 + 127) / 255) + ((b * 5 + 127) / 255);
}

void ascii_preview(const std::vector<uint16_t> &fb) {
    // Headless-test screen: VOIDOS_DUMP_ASCII=1 renders the framebuffer to
    // stderr with half-block glyphs (one line per two pixel rows).
    static const bool enabled = [] {
        const char *v = std::getenv("VOIDOS_DUMP_ASCII");
        return v && v[0] == '1';
    }();
    if (!enabled) return;
    static const int every = [] {
        const char *v = std::getenv("VOIDOS_DUMP_EVERY");
        const int e = v ? std::atoi(v) : 30;
        return e > 0 ? e : 1;
    }();
    static unsigned counter = 0;
    if (++counter % every != 0) return;

    const int cols = 64;
    const int bw = (SCR_W + cols - 1) / cols;   // pixels per block column
    std::fputs("\x1b[?25l\x1b[H", stderr);      // hide cursor, home
    for (int y = 0; y < SCR_H / 2; ++y) {
        for (int x = 0; x < cols; ++x) {
            const int px = x * bw < SCR_W ? x * bw : SCR_W - 1;
            const uint16_t top = fb[y * 2 * SCR_W + px];
            const uint16_t bot = fb[(y * 2 + 1) * SCR_W + px];
            std::fprintf(stderr, "\x1b[38;5;%dm\x1b[48;5;%dm\xE2\x96\x80",
                         rgb565_to_ansi256(top), rgb565_to_ansi256(bot));
        }
        std::fputs("\x1b[0m\n", stderr);
    }
}

const std::array<uint8_t, 5> glyph(char c) {
    // Compact 5x7 font for the text used by the UI. Unknown characters are boxes.
    switch (c) {
        case 'A': return {0x7E,0x11,0x11,0x11,0x7E}; case 'B': return {0x7F,0x49,0x49,0x49,0x36};
        case 'C': return {0x3E,0x41,0x41,0x41,0x22}; case 'D': return {0x7F,0x41,0x41,0x22,0x1C};
        case 'E': return {0x7F,0x49,0x49,0x49,0x41}; case 'F': return {0x7F,0x09,0x09,0x09,0x01};
        case 'G': return {0x3E,0x41,0x49,0x49,0x7A}; case 'H': return {0x7F,0x08,0x08,0x08,0x7F};
        case 'I': return {0x00,0x41,0x7F,0x41,0x00}; case 'J': return {0x20,0x40,0x41,0x3F,0x01};
        case 'K': return {0x7F,0x08,0x14,0x22,0x41}; case 'L': return {0x7F,0x40,0x40,0x40,0x40};
        case 'M': return {0x7F,0x02,0x0C,0x02,0x7F}; case 'N': return {0x7F,0x04,0x08,0x10,0x7F};
        case 'O': return {0x3E,0x41,0x41,0x41,0x3E}; case 'P': return {0x7F,0x09,0x09,0x09,0x06};
        case 'Q': return {0x3E,0x41,0x51,0x21,0x5E}; case 'R': return {0x7F,0x09,0x19,0x29,0x46};
        case 'S': return {0x46,0x49,0x49,0x49,0x31}; case 'T': return {0x01,0x01,0x7F,0x01,0x01};
        case 'U': return {0x3F,0x40,0x40,0x40,0x3F}; case 'V': return {0x1F,0x20,0x40,0x20,0x1F};
        case 'W': return {0x3F,0x40,0x38,0x40,0x3F}; case 'X': return {0x63,0x14,0x08,0x14,0x63};
        case 'Y': return {0x07,0x08,0x70,0x08,0x07}; case 'Z': return {0x61,0x51,0x49,0x45,0x43};
        case '0': return {0x3E,0x45,0x49,0x51,0x3E}; case '1': return {0x00,0x21,0x7F,0x01,0x00};
        case '2': return {0x21,0x43,0x45,0x49,0x31}; case '3': return {0x42,0x41,0x51,0x69,0x46};
        case '4': return {0x0C,0x14,0x24,0x7F,0x04}; case '5': return {0x72,0x51,0x51,0x51,0x4E};
        case '6': return {0x1E,0x29,0x49,0x49,0x06}; case '7': return {0x40,0x47,0x48,0x50,0x60};
        case '8': return {0x36,0x49,0x49,0x49,0x36}; case '9': return {0x30,0x49,0x49,0x4A,0x3C};
        case '-': return {0x08,0x08,0x08,0x08,0x08}; case '_': return {0x40,0x40,0x40,0x40,0x40};
        case '.': return {0x00,0x60,0x60,0x00,0x00}; case ':': return {0x00,0x36,0x36,0x00,0x00};
        case '/': return {0x20,0x10,0x08,0x04,0x02}; case '%': return {0x63,0x13,0x08,0x64,0x63};
        case '[': return {0x00,0x7F,0x41,0x41,0x00}; case ']': return {0x00,0x41,0x41,0x7F,0x00};
        case '>': return {0x41,0x22,0x14,0x08,0x00}; case '?': return {0x02,0x01,0x51,0x09,0x06};
        case ' ': return {0,0,0,0,0};
        default: return {0x7F,0x41,0x41,0x41,0x7F};
    }
}
}

TFT_eSPI::TFT_eSPI()
    : _spi_fd(-1), _rotation(0), _window_x(0), _window_y(0), _window_w(SCR_W),
      _window_h(SCR_H), _text_fg(TFT_WHITE), _text_bg(TFT_BLACK), _text_size(1),
      _cursor_x(0), _cursor_y(0), _framebuffer(SCR_W * SCR_H, TFT_BLACK) {}

TFT_eSPI::~TFT_eSPI() {
    if (_spi_fd >= 0) close(_spi_fd);
}

void TFT_eSPI::init() {
    gpio_prepare(GPIO_DC);
    gpio_prepare(GPIO_RESET);
    gpio_write(GPIO_RESET, 0);
    delay(20);
    gpio_write(GPIO_RESET, 1);
    delay(120);

    _spi_fd = open(SPI_DEVICE, O_RDWR | O_CLOEXEC);
    if (_spi_fd >= 0) {
        uint8_t mode = SPI_MODE_0;
        uint8_t bits = 8;
        uint32_t speed = SPI_SPEED_HZ;
        (void)ioctl(_spi_fd, SPI_IOC_WR_MODE, &mode);
        (void)ioctl(_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
        (void)ioctl(_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
        command(0x01); delay(10);
        command(0x3A); const uint8_t pixel_format = 0x55; data(&pixel_format, 1);
        command(0x36); const uint8_t memory_access = 0x48; data(&memory_access, 1);
        command(0x11); delay(120);
        command(0x29);
    } else {
        Serial.println("Raspberry Pi: /dev/spidev0.0 unavailable; using framebuffer-only mode");
    }
}

void TFT_eSPI::setRotation(uint8_t rotation) { _rotation = rotation & 3; }
void TFT_eSPI::fillScreen(uint16_t color) { std::fill(_framebuffer.begin(), _framebuffer.end(), color); }

uint16_t TFT_eSPI::color565(uint8_t r, uint8_t g, uint8_t b) const {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void TFT_eSPI::command(uint8_t value) {
    if (_spi_fd < 0) return;
    gpio_write(GPIO_DC, 0);
    (void)write(_spi_fd, &value, 1);
}

void TFT_eSPI::data(const uint8_t *bytes, std::size_t length) {
    if (_spi_fd < 0 || !bytes || length == 0) return;
    gpio_write(GPIO_DC, 1);
    (void)write(_spi_fd, bytes, length);
}

void TFT_eSPI::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    _window_x = x; _window_y = y; _window_w = w; _window_h = h;
}
void TFT_eSPI::startWrite() {}
void TFT_eSPI::endWrite() {}

void TFT_eSPI::pushPixels(uint16_t *pixels, uint32_t count) {
    if (!pixels) return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t index = static_cast<uint32_t>(_window_y) * SCR_W + _window_x + i;
        if (index < _framebuffer.size()) _framebuffer[index] = pixels[i];
    }
}

void TFT_eSPI::pushColor(uint16_t color, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t index = static_cast<uint32_t>(_window_y) * SCR_W + _window_x + i;
        if (index < _framebuffer.size()) _framebuffer[index] = color;
    }
}

void TFT_eSPI::pushImageDMA(int x, int y, int w, int h, const uint16_t *pixels) {
    if (!pixels) return;
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) drawPixel(x + col, y + row, pixels[row * w + col]);
}

void TFT_eSPI::readRect(int x, int y, int w, int h, uint16_t *out) const {
    if (!out) return;
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) {
            const int px = x + col, py = y + row;
            out[row * w + col] = in_bounds(px, py) ? _framebuffer[py * SCR_W + px] : TFT_BLACK;
        }
}

bool TFT_eSPI::in_bounds(int x, int y) const { return x >= 0 && x < SCR_W && y >= 0 && y < SCR_H; }
void TFT_eSPI::drawPixel(int x, int y, uint16_t color) { if (in_bounds(x,y)) _framebuffer[y * SCR_W + x] = color; }
void TFT_eSPI::drawFastHLine(int x, int y, int w, uint16_t color) { for (int i=0;i<w;++i) drawPixel(x+i,y,color); }
void TFT_eSPI::drawFastVLine(int x, int y, int h, uint16_t color) { for (int i=0;i<h;++i) drawPixel(x,y+i,color); }
void TFT_eSPI::drawRect(int x, int y, int w, int h, uint16_t color) { drawFastHLine(x,y,w,color); drawFastHLine(x,y+h-1,w,color); drawFastVLine(x,y,h,color); drawFastVLine(x+w-1,y,h,color); }
void TFT_eSPI::fillRect(int x, int y, int w, int h, uint16_t color) { for (int row=0;row<h;++row) drawFastHLine(x,y+row,w,color); }

void TFT_eSPI::draw_circle_point(int cx, int cy, int x, int y, uint16_t color) {
    drawPixel(cx+x,cy+y,color); drawPixel(cx-x,cy+y,color); drawPixel(cx+x,cy-y,color); drawPixel(cx-x,cy-y,color);
    drawPixel(cx+y,cy+x,color); drawPixel(cx-y,cy+x,color); drawPixel(cx+y,cy-x,color); drawPixel(cx-y,cy-x,color);
}
void TFT_eSPI::drawCircle(int cx, int cy, int radius, uint16_t color) {
    int x=0,y=radius,d=3-2*radius; while(y>=x){draw_circle_point(cx,cy,x,y,color); x++; if(d>0){y--;d=d+4*(x-y)+10;}else d=d+4*x+6;}
}
void TFT_eSPI::fillCircle(int cx, int cy, int radius, uint16_t color) { for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) if(x*x+y*y<=radius*radius) drawPixel(cx+x,cy+y,color); }
void TFT_eSPI::drawLine(int x0,int y0,int x1,int y1,uint16_t color) { int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy; for(;;){drawPixel(x0,y0,color);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}} }

void TFT_eSPI::setTextColor(uint16_t foreground, uint16_t background) { _text_fg=foreground; _text_bg=background; }
void TFT_eSPI::setTextSize(uint8_t size) { _text_size = size ? size : 1; }
void TFT_eSPI::setCursor(int x, int y) { _cursor_x=x; _cursor_y=y; }
void TFT_eSPI::draw_char(int x, int y, char character) {
    const auto bits = glyph(character);
    const int scale = _text_size;
    for (int col=0;col<5;++col) for (int row=0;row<7;++row) {
        const bool on = bits[col] & (1u << row);
        if (on) fillRect(x+col*scale,y+row*scale,scale,scale,_text_fg);
        else if (_text_bg != 0) fillRect(x+col*scale,y+row*scale,scale,scale,_text_bg);
    }
}
void TFT_eSPI::print(const char *text) {
    if (!text) return;
    for (const char *p=text;*p;++p) { if(*p=='\n'){_cursor_x=0;_cursor_y+=8*_text_size;continue;} draw_char(_cursor_x,_cursor_y,*p); _cursor_x+=6*_text_size; }
}

void TFT_eSPI::present() {
    ascii_preview(_framebuffer);

    // Headless-test frame dump (PC without a display).
    // VOIDOS_DUMP_DIR = output dir, VOIDOS_DUMP_EVERY = Nth frame (1 = every).
    static const char *dump_dir = []{
        const char *d = std::getenv("VOIDOS_DUMP_DIR");
        return (d && d[0]) ? d : nullptr;
    }();
    if (dump_dir) {
        static const int every = []{
            const char *v = std::getenv("VOIDOS_DUMP_EVERY");
            int e = v ? std::atoi(v) : 30;
            return e > 0 ? e : 1;
        }();
        static unsigned counter = 0;
        if (++counter % every == 0) {
            char path[256];
            std::snprintf(path, sizeof(path), "%s/frame_%06u.ppm", dump_dir, counter);
            FILE *fp = std::fopen(path, "wb");
            if (fp) {
                std::fprintf(fp, "P6\n%d %d\n255\n", SCR_W, SCR_H);
                for (uint16_t c : _framebuffer) {
                    uint8_t px[3] = {
                        static_cast<uint8_t>(((c >> 11) & 0x1F) << 3),
                        static_cast<uint8_t>(((c >> 5) & 0x3F) << 2),
                        static_cast<uint8_t>((c & 0x1F) << 3)};
                    std::fwrite(px, 1, 3, fp);
                }
                std::fclose(fp);
            }
        }
    }
    if (_spi_fd < 0) return;
    uint8_t col_data[4] = {0, 0, static_cast<uint8_t>((SCR_W-1) >> 8), static_cast<uint8_t>(SCR_W-1)};
    uint8_t row_data[4] = {0, 0, static_cast<uint8_t>((SCR_H-1) >> 8), static_cast<uint8_t>(SCR_H-1)};
    command(0x2A); data(col_data,4); command(0x2B); data(row_data,4); command(0x2C);
    gpio_write(GPIO_DC, 1);
    std::vector<uint8_t> bytes(_framebuffer.size() * 2);
    for (std::size_t i=0;i<_framebuffer.size();++i) { bytes[2*i]=_framebuffer[i]>>8; bytes[2*i+1]=_framebuffer[i]&0xFF; }
    (void)write(_spi_fd, bytes.data(), bytes.size());
}

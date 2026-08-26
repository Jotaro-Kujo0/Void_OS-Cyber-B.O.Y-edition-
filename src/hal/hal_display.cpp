// hal_display.cpp
#include "hal_display.h"
#include "config.h"
#ifdef VOIDOS_RPI5
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#endif

TFT_eSPI tft = TFT_eSPI();

void hal_display_init() {
    tft.init();
    // The UI is designed for the native 240x320 portrait orientation.
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
#ifdef VOIDOS_RPI5
    hal_display_bl_set(BL_FULL);
#else
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_BL, 5000, 8);
#else
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_BL, 0);
#endif
    hal_display_bl_set(BL_FULL);
#endif
}

void hal_display_bl_set(uint8_t val) {
#ifdef VOIDOS_RPI5
    // Walk /sys/class/backlight/* and write `val` (0..255 scaled to 0..max).
    // The Pi 5 ILI9341 carrier exposes `backlight` via the DPI panel or the
    // optional GPIO-PWM helper; the kernel names vary by board, so we try
    // every entry until one accepts the write.
    //
    // The "max" file tells us the brightness scale (often 255 on the Pi).
    // If absent we assume 255 so val maps 1:1 to sysfs.
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        char path[128];
        std::snprintf(path, sizeof(path),
                      "/sys/class/backlight/%.90s/max", e->d_name);
        char maxbuf[16] = "255";
        int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) { ::read(fd, maxbuf, sizeof(maxbuf) - 1); ::close(fd); }
        int maxv = std::atoi(maxbuf);
        if (maxv <= 0) maxv = 255;
        int scaled = (val * maxv) / 255;
        std::snprintf(path, sizeof(path),
                      "/sys/class/backlight/%.85s/brightness", e->d_name);
        char valbuf[16];
        std::snprintf(valbuf, sizeof(valbuf), "%d", scaled);
        fd = ::open(path, O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            ::write(fd, valbuf, std::strlen(valbuf));
            ::close(fd);
        }
    }
    closedir(d);
#else
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_BL, val);
#else
    ledcWrite(0, val);
#endif
#endif
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

void hal_display_present() {
#ifdef VOIDOS_RPI5
    tft.present();
#endif
}

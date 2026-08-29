// app_qr.cpp — QR via qrcodegen (nayuki). matrix->TFT + BMP save.

#include "app_qr.h"

// ─── Fixed include paths (file lives in src/Apps/, headers in src/) ───
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../config.h"
#include "../os/events.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cstdlib>   // malloc / free

// ─── nayuki QR-Code-generator (namespace is qrcodegen) ───────────────
#include "qrcodegen.hpp"
using namespace qrcodegen;

// ─── Constants ────────────────────────────────────────────────────────
static const char*  LABELS[]    = { "URL", "GEN", "SHOW" };
static const uint8_t N_ROWS     = 3;
static const int     QR_SCALE_MIN = 4;
static const int     QR_SCALE_MAX = 8;

// ─── State ────────────────────────────────────────────────────────────
static uint8_t  _row         = 0;
static char     _url[128]    = "https://example.com";
static char     _status[32]  = "READY";

// QR framebuffer (allocated on gen, freed on suspend)
static uint8_t* _qr_fb       = nullptr;
static int      _qr_fb_w     = 0;
static int      _qr_fb_h     = 0;

// ─── BMP writer (24-bit, saved to SD) ─────────────────────────────────
static bool save_bmp_24(const char* path, const uint8_t* rgb565,
                         int w, int h) {
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;

    uint32_t row_bytes = w * 3;
    uint32_t pad       = (4 - (row_bytes % 4)) % 4;
    uint32_t img_size  = (row_bytes + pad) * h;
    uint32_t file_size = 14 + 40 + img_size;

    // File header
    fputc('B', fp); fputc('M', fp);
    fwrite(&file_size, 4, 1, fp);
    uint32_t reserved = 0;
    fwrite(&reserved, 4, 1, fp);
    uint32_t offset = 54;
    fwrite(&offset, 4, 1, fp);
    // DIB header
    uint32_t dib_size = 40;
    fwrite(&dib_size, 4, 1, fp);
    int32_t w32 = w, h32 = h;
    fwrite(&w32, 4, 1, fp);
    fwrite(&h32, 4, 1, fp);
    uint16_t planes = 1, bpp = 24;
    fwrite(&planes, 2, 1, fp);
    fwrite(&bpp, 2, 1, fp);
    fwrite(&reserved, 4, 1, fp);  // compression
    fwrite(&reserved, 4, 1, fp);  // image size
    fwrite(&reserved, 4, 1, fp);  // x ppm
    fwrite(&reserved, 4, 1, fp);  // y ppm
    fwrite(&reserved, 4, 1, fp);  // colors used
    fwrite(&reserved, 4, 1, fp);  // important

    // Pixel data (bottom-up, BGR888)
    uint8_t pad_buf[3] = {0, 0, 0};
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            uint16_t px = reinterpret_cast<const uint16_t*>(rgb565)[y * w + x];
            uint8_t r = ((px >> 11) & 0x1F) << 3;
            uint8_t g = ((px >> 5)  & 0x3F) << 2;
            uint8_t b = ( px        & 0x1F) << 3;
            uint8_t bgr[3] = { b, g, r };
            fwrite(bgr, 1, 3, fp);
        }
        if (pad) fwrite(pad_buf, 1, pad, fp);
    }
    fclose(fp);
    return true;
}

// ─── QR generation → framebuffer ─────────────────────────────────────
static bool generate_qr_framebuffer(const char* text) {
    if (_qr_fb) { free(_qr_fb); _qr_fb = nullptr; }

    QrCode::Ecc ecc = QrCode::Ecc::MEDIUM;
    QrCode qr = QrCode::encodeText(text, ecc);

    int modules = qr.getSize();       // FIXED: was qr.size (private)
    int scale   = (modules <= 25) ? QR_SCALE_MIN : QR_SCALE_MAX;
    int fb_w    = modules * scale;
    int fb_h    = modules * scale;

    // Clamp to screen
    if (fb_w > SCR_W) { scale = SCR_W / modules; fb_w = modules * scale; }
    if (fb_h > SCR_H - 60) { scale = (SCR_H - 60) / modules; fb_h = modules * scale; }

    _qr_fb = static_cast<uint8_t*>(malloc(fb_w * fb_h * 2));
    if (!_qr_fb) return false;
    _qr_fb_w = fb_w;
    _qr_fb_h = fb_h;

    // Render modules → RGB565 pixels
    uint16_t* px = reinterpret_cast<uint16_t*>(_qr_fb);
    for (int my = 0; my < modules; ++my) {
        for (int mx = 0; mx < modules; ++mx) {
            uint16_t color = qr.getModule(mx, my) ? 0x0000 : 0xFFFF;
            for (int py = 0; py < scale; ++py) {
                for (int px_x = 0; px_x < scale; ++px_x) {
                    px[(my * scale + py) * fb_w + (mx * scale + px_x)] = color;
                }
            }
        }
    }
    return true;
}

// ─── App lifecycle ────────────────────────────────────────────────────
void app_qr_init() {
    char buf[128];
    if (hal_storage_get_str("qr_text", buf, sizeof(buf)))
        std::snprintf(_url, sizeof(_url), "%s", buf);
    else
        hal_storage_set_str("qr_text", _url);
}

void app_qr_tick() {}

void app_qr_suspend() {
    if (_qr_fb) { free(_qr_fb); _qr_fb = nullptr; }
}

void app_qr_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) return;

    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }

    if (e.type == EVT_BTN_A_DOWN) {
        switch (_row) {
            case 0: {
                static const char* presets[] = {
                    "https://example.com",
                    "http://10.0.0.1:8080/captive",
                    "https://github.com/Jotaro-Kujo0/Void_OS-Cyber-B.O.Y-edition-"
                };
                static uint8_t idx = 0;
                idx = (idx + 1) % 3;
                std::snprintf(_url, sizeof(_url), "%s", presets[idx]);
                hal_storage_set_str("qr_text", _url);
                std::snprintf(_status, sizeof(_status), "URL set");
                break;
            }
            case 1: {
                if (generate_qr_framebuffer(_url)) {
                    char path[80];
                    time_t t = time(nullptr);
                    struct tm* tm = localtime(&t);
                    std::snprintf(path, sizeof(path),
                        "/mnt/sdcard/loot/qr_%04d%02d%02d_%02d%02d%02d.bmp",
                        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                        tm->tm_hour, tm->tm_min, tm->tm_sec);
                    save_bmp_24(path, _qr_fb, _qr_fb_w, _qr_fb_h);
                    std::snprintf(_status, sizeof(_status), "OK %dx%d",
                                  _qr_fb_w, _qr_fb_h);
                } else {
                    std::snprintf(_status, sizeof(_status), "ERR: alloc");
                }
                break;
            }
            case 2:
                std::snprintf(_status, sizeof(_status), "shown");
                break;
        }
    }
}

void app_qr_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "QR / GEN", T_FG, T_PANEL, FONT_SM);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "%.40s", _url);

    if (_qr_fb) {
        int qx = (SCR_W - _qr_fb_w) / 2;
        int qy = info_y + 18;
        uint16_t* px = reinterpret_cast<uint16_t*>(_qr_fb);
        for (int y = 0; y < _qr_fb_h; ++y)
            for (int x = 0; x < _qr_fb_w; ++x)
                draw_pixel(qx + x, qy + y, px[y * _qr_fb_w + x]);
    } else {
        // Placeholder finder patterns
        int qx = 90, qy = info_y + 18, mod = 6;
        draw_fill(qx, qy, 21*mod, 21*mod, 0xFFFF);
        draw_fill(qx, qy, 7*mod, 7*mod, 0x0000);
        draw_fill(qx+mod, qy+mod, 5*mod, 5*mod, 0xFFFF);
        draw_fill(qx+2*mod, qy+2*mod, 3*mod, 3*mod, 0x0000);
        draw_fill(qx+14*mod, qy, 7*mod, 7*mod, 0x0000);
        draw_fill(qx+15*mod, qy+mod, 5*mod, 5*mod, 0xFFFF);
        draw_fill(qx+16*mod, qy+2*mod, 3*mod, 3*mod, 0x0000);
        draw_fill(qx, qy+14*mod, 7*mod, 7*mod, 0x0000);
        draw_fill(qx+mod, qy+15*mod, 5*mod, 5*mod, 0xFFFF);
        draw_fill(qx+2*mod, qy+16*mod, 3*mod, 3*mod, 0x0000);
    }

    draw_textf(8, SCR_H - 28, T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] ACT  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
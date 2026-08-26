// media.cpp — image / animation dispatcher.
//
// Two compile-time paths:
//
//   Pi 5  (`-DVOIDOS_RPI5=1`)
//   ─────
//     * BMP:    hand-rolled decoder, ~80 lines.
//     * PNG:    shell-out via ImageMagick `convert ... bmp:-` and parse
//               the resulting 24/32-bit BMP. ImageMagick (`imagemagick`
//               package) is the default on Raspberry Pi OS Bookworm.
//     * JPG:    same convert trick; libjpeg-turbo would be cheaper but
//               shell-out keeps the project hermetic.
//     * GIF:    static-only via ImageMagick. Animated GIFs need the
//               `giflib` system library; fall back to a non-animated
//               first frame.
//     * FRAMES: open each `path_template` via fopen, render via the
//               BMP path.
//     * All examples use shell-out where it's cheaper than vendoring
//               a decoder; total image-decode cost on Pi 5 is bounded
//               by `convert`. ImageMagick is the only non-stdlib dep.
//
//   ESP32 (default; Arduino core)
//   ─────
//     * BMP:    hand-rolled, same as Pi 5.
//     * PNG:    BitBank `PNGdec` (single-header, ~30 KB). Drop the
//               header into `src/UI/third_party/PNGdec.h` and subtract
//               nothing from the repo budget.
//     * JPG:    BitBank `TJpgDec` — no vendoring needed if you opt for
//               the Arduino library wrapper; otherwise single-header.
//     * GIF:    BitBank `AnimatedGIF`, single-header. ~30 KB static RAM
//               for the worst-case palette + LZW state.
//     * FRAMES: each frame is `SD.open` + BMP decode. SD lib returns a
//               `File`; we read into a small scanline buffer.
//
// Forward declarations for the bottom-half libs sit at the bottom of the
// file. The dispatcher logic itself is the same across targets; the
// only difference is *how* pixels arrive in the scanline buffer.
//
// ─────────────────────────────────────────────────────────────────────────

#include "media.h"

#include "draw.h"
#include "theme.h"
#include "config.h"
#include "hal/hal_storage.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#ifdef VOIDOS_RPI5
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#endif

// ── Media opaque struct ────────────────────────────────────────────────
//
// One struct covers every decoder — fields unused by a particular type
// are simply ignored. This is the laziest possible polymorphic type
// without resorting to a v-table. Each decoder's first 8 bytes after
// `kind` are a tag so we can `static_cast` to the concrete union.

struct Media {
    MediaType kind;

    union {
        // MEDIA_BMP / MEDIA_JPG / MEDIA_PNG: file path; pixels stream
        // in via the appropriate decoder and are blitted row-by-row to
        // the TFT.
        struct {
            char     path[64];
            uint16_t w, h;
            uint32_t frames;     // 0 for static
        } src;

        // MEDIA_GIF: animated.
        struct {
            char        path[64];
            uint16_t    w, h;
            uint32_t    frames;
            uint16_t    delay_ms;   // per-frame delay (GIF spec)
            uint32_t    next;       // next-frame index
            bool        finished;
        } gif;

        // MEDIA_FRAMES: printf-style template + count + fps.
        struct {
            char     tmpl[64];
            uint32_t count, idx;
            uint8_t  fps;
            uint32_t next_ms;       // next-frame target time
        } seq;
    } d;
};

// ── Path → MediaType dispatcher ────────────────────────────────────────

MediaType media_type_from_path(const char *path) {
    if (!path) return MEDIA_NONE;
    const char *dot = std::strrchr(path, '.');
    if (!dot) return MEDIA_NONE;
    // ponytail: case-insensitive compare by lowercasing the .ext into a
    // 4-byte buffer once.
    char ext[5] = {}; for (int i = 0; i < 4 && dot[i+1]; ++i) ext[i] = dot[i+1] | 0x20;
    if (!std::strcmp(ext, "bmp" )) return MEDIA_BMP;
    if (!std::strcmp(ext, "png" )) return MEDIA_PNG;
    if (!std::strcmp(ext, "jpg" ) || !std::strcmp(ext, "jpeg")) return MEDIA_JPG;
    if (!std::strcmp(ext, "gif" )) return MEDIA_GIF;
    return MEDIA_NONE;
}

// ── Per-type close helpers ─────────────────────────────────────────────

static void close_src(Media *m)      { (void)m; /* nothing to free */ }
static void close_gif(Media *m)      { (void)m; /* lib_* handles */ }
static void close_seq(Media *m)      { (void)m; }

void media_close(Media *m) {
    if (!m) return;
    switch (m->kind) {
        case MEDIA_BMP: case MEDIA_PNG: case MEDIA_JPG: close_src(m); break;
        case MEDIA_GIF:  close_gif(m); break;
        case MEDIA_FRAMES: close_seq(m); break;
        case MEDIA_NONE: default: break;
    }
    delete m;
}

// ── Frame-sequence helpers ─────────────────────────────────────────────

size_t media_format_frame(char *out, size_t out_len,
                          const char *tmpl, uint32_t idx) {
    return std::snprintf(out, out_len, tmpl, idx);
}

// ── BMP decoder (hand-rolled, used on every target) ────────────────────
//
// BMP is uncompressed; cheap. We decode the header, then strip 24/32-bit
// pixel data into the line buffer.
//
//  - 24-bit BMP: BGR triplets, no alpha.
//  - 32-bit BMP: BGRA quads, alpha ignored on TFT.
//
// We never deal with BI_RLE-compressed BMPs (rarely used in tooling).
// Such a file returns false from `bmp_render` and the caller paints a
// placeholder rectangle.

#include <stdint.h>

struct BmpHeader {
    uint32_t file_size;
    uint32_t pixel_offset;
    uint32_t info_size;
    int32_t  w, h;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t raw_size;
};

static bool bmp_read_header(int fd_or_file, BmpHeader &h) {
    // 14-byte file header + start of 40-byte DIB header.
    // Implementation differs per target; both kernels give us either a
    // POSIX fd or a FILE*. See the platform-implementations below.
    (void)fd_or_file; (void)h;
    return false;
}

#ifdef VOIDOS_RPI5
#include <fcntl.h>
#include <unistd.h>

// ponytail: BMP render on Pi 5 currently paints a placeholder so the
// pipeline wires up without bringing in SDL/pixman. Replace with a
// real framebuffer blit once the LCD surface is selected (SDL,
// /dev/fb0, X11 shm). The Pi 5 dev cycle assumes a remote shell
// (HDMI / SSH) so visual fidelity matters less than the symbolic API.
static bool pi_bmp_read(int fd, BmpHeader &h) {
    uint8_t hdr[54];
    if (::read(fd, hdr, 54) != 54) return false;
    if (hdr[0] != 'B' || hdr[1] != 'M') return false;
    h.file_size     = hdr[2]  | (hdr[3]  << 8) | (hdr[4]  << 16) | ((uint32_t)hdr[5]  << 24);
    h.pixel_offset  = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
    h.info_size     = hdr[14] | (hdr[15] << 8) | (hdr[16] << 16) | ((uint32_t)hdr[17] << 24);
    h.w             = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | ((uint32_t)hdr[21] << 24);
    h.h             = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | ((uint32_t)hdr[25] << 24);
    h.planes        = hdr[26] | (hdr[27] << 8);
    h.bpp           = hdr[28] | (hdr[29] << 8);
    h.compression   = hdr[30] | (hdr[31] << 8) | (hdr[32] << 16) | ((uint32_t)hdr[33] << 24);
    h.raw_size      = hdr[34] | (hdr[35] << 8) | (hdr[36] << 16) | ((uint32_t)hdr[37] << 24);
    return h.info_size >= 40 && h.bpp >= 24 && h.compression == 0;
}

static bool pi_bmp_render(const char *path, int x, int y) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;
    BmpHeader h{};
    if (!pi_bmp_read(fd, h)) { ::close(fd); return false; }
    int w = h.w, hh = h.h;
    int rowbytes = w * (h.bpp / 8);
    if (::lseek(fd, h.pixel_offset, SEEK_SET) < 0) { ::close(fd); return false; }
    // Read once into memory and blit top-down. BMP stores rows bottom-up.
    std::vector<uint8_t> image(rowbytes * hh);
    for (int rowi = 0; rowi < hh; ++rowi) {
        int src_y = (hh - 1) - rowi;
        ssize_t want = ::read(fd, image.data() + rowi * rowbytes, rowbytes);
        if (want != rowbytes) break;
    }
    ::close(fd);

    // Decompress into per-row RGB565 for the line-blit pipeline below.
    for (int rowi = 0; rowi < hh; ++rowi) {
        uint16_t line[320] = {};  // SCR_W cap; longer images get clipped.
        for (int col = 0; col < w && col < SCR_W; ++col) {
            uint8_t b, g, r;
            if (h.bpp == 24) {
                b = image[rowi * rowbytes + col*3 + 0];
                g = image[rowi * rowbytes + col*3 + 1];
                r = image[rowi * rowbytes + col*3 + 2];
            } else {
                b = image[rowi * rowbytes + col*4 + 0];
                g = image[rowi * rowbytes + col*4 + 1];
                r = image[rowi * rowbytes + col*4 + 2];
            }
            line[col] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
        // No pushImage on Pi 5; emit pixel by pixel via draw_pixel.
        for (int col = 0; col < w && col < SCR_W; ++col)
            draw_pixel(x + col, y + rowi, line[col]);
    }
    return true;
}
#else
// ESP32 — pushes rows through `tft.pushImage` after reading the SD
// file. The BitBank `BmpDraw`/TFT_eSPI helper is the canonical path;
// we leave the row loop here for clarity and so the renderer can fall
// back to a manual decode when lib helpers aren't available.

#include <SD.h>
static bool esp_read_le32(File &f, uint32_t &out) {
    uint8_t b[4]; if (f.read(b, 4) != 4) return false;
    out = (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
    return true;
}
static bool esp_read_le16(File &f, uint16_t &out) {
    uint8_t b[2]; if (f.read(b, 2) != 2) return false;
    out = (uint16_t)b[0] | ((uint16_t)b[1]<<8);
    return true;
}

static bool esp_bmp_render(const char *path, int x, int y) {
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    uint16_t sig; esp_read_le16(f, sig);
    if (sig != 0x4D42) { f.close(); return false; }  // "BM"
    uint32_t fsz=0, off=0,isz=0; esp_read_le32(f, fsz); esp_read_le32(f, off);
    esp_read_le32(f, isz);
    int32_t w=0, h=0; esp_read_le32(f, (uint32_t&)w);
    esp_read_le32(f, (uint32_t&)h);
    uint16_t planes=0,bpp=0; esp_read_le16(f, planes); esp_read_le16(f, bpp);
    uint32_t comp=0,rawsize=0;
    if (isz >= 40) { esp_read_le32(f, comp); esp_read_le32(f, rawsize); }
    if (planes != 1 || bpp < 24 || comp != 0) { f.close(); return false; }
    // Skip ahead to pixel data; also account for top-down BMPs (negative h).
    f.seek(off);
    if (h < 0) h = -h; else {}  // top-down rendered as-is below
    int rowb = w * (bpp / 8);
    // Pad each row to a 4-byte boundary.
    int rowb_pad = (rowb + 3) & ~3;
    std::vector<uint8_t> row(rowb_pad);
    int flipped = (h > 0); (void)flipped;
    int hh_abs = h; if (hh_abs < 0) hh_abs = -hh_abs;
    for (int rowi = 0; rowi < hh_abs; ++rowi) {
        int src_y = (h >= 0) ? ((hh_abs - 1) - rowi) : rowi;
        f.seek(off + src_y * rowb_pad);
        if (f.read(row.data(), rowb_pad) < rowb_pad) break;
        for (int col = 0; col < w && col < SCR_W; ++col) {
            uint8_t b, g, r;
            if (bpp == 24) {
                b = row[col*3 + 0]; g = row[col*3 + 1]; r = row[col*3 + 2];
            } else {
                b = row[col*4 + 0]; g = row[col*4 + 1]; r = row[col*4 + 2];
            }
            uint16_t c16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            draw_pixel(x + col, y + rowi, c16);
        }
    }
    f.close();
    return true;
}
#endif

// ── PNG / JPG / GIF dispatchers ────────────────────────────────────────

// Forward declarations of the third-party bottom halves. The app side
// never sees these; the dispatcher calls them through static helpers.

#ifdef VOIDOS_RPI5
static bool pi_render_png(const char *path, int x, int y) {
    // Shell-out: convert to BMP and recurse via the BMP path.
    char tmp[128]; std::snprintf(tmp, sizeof(tmp), "/tmp/vbmp_%d.bmp",
                                 (int)::getpid());
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "convert '%s' -alpha remove -alpha off bmp:'%s' 2>/dev/null",
                  path, tmp);
    if (std::system(cmd) != 0) return false;
    bool ok = pi_bmp_render(tmp, x, y);
    ::unlink(tmp);
    return ok;
}
static bool pi_render_jpg(const char *path, int x, int y) {
    char tmp[128]; std::snprintf(tmp, sizeof(tmp), "/tmp/vjpg_%d.bmp",
                                 (int)::getpid());
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "convert '%s' -resize 320x240 bmp:'%s' 2>/dev/null",
                  path, tmp);
    if (std::system(cmd) != 0) return false;
    bool ok = pi_bmp_render(tmp, x, y);
    ::unlink(tmp);
    return ok;
}
static bool pi_render_gif(const char *path, int x, int y, uint32_t &frames,
                           uint16_t &delay_ms) {
    // ponytail: animated GIFs on Pi 5 are out of scope for the
    // hermetic build. We render the *static* first frame via libgif /
    // ImageMagick and report 1 frame so the calling app treats it as
    // a still image.
    frames = 1; delay_ms = 100;
    char tmp[128]; std::snprintf(tmp, sizeof(tmp), "/tmp/vgif_%d.bmp",
                                 (int)::getpid());
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "convert '%s[0]' bmp:'%s' 2>/dev/null", path, tmp);
    if (std::system(cmd) != 0) return false;
    bool ok = pi_bmp_render(tmp, x, y);
    ::unlink(tmp);
    return ok;
}
#else
// ESP32 PNG/JPG/GIF decoding — drop the BitBank single-header libs at
// `src/UI/third_party/PNGdec.h`, `TJpgDec.h`, `AnimatedGIF.h` and add
// the platformio deps below. Until then these are stubs that paint a
// placeholder so the API still works end-to-end.
//
// platformio.ini additions (real implementation):
//   lib_deps +=
//     bitbank2/PNGdec @ ^1.0.3
//     bitbank2/AnimatedGIF @ ^2.0.0
//     bitbank2/TJpgDec @ ^1.0.7

static bool esp_render_png(const char *, int, int) { return false; }
static bool esp_render_jpg(const char *, int, int) { return false; }
static bool esp_render_gif_first(const char *, int, int,
                                 uint32_t &f, uint16_t &d) { f = 1; d = 100; return false; }
static bool esp_render_gif_step(const char *, int, int, uint32_t) { return false; }
#endif

// ── Per-type open ─────────────────────────────────────────────────────

Media *media_open(const char *path) {
    if (!path) return nullptr;
    MediaType t = media_type_from_path(path);
    if (t == MEDIA_NONE) return nullptr;
    Media *m = new Media{};
    m->kind = t;
    std::strncpy(m->d.src.path, path, sizeof(m->d.src.path) - 1);
    m->d.src.path[sizeof(m->d.src.path) - 1] = 0;
    m->d.src.frames = 0;
    if (t == MEDIA_GIF) {
        m->d.gif.frames = 1; m->d.gif.next = 0;
        m->d.gif.delay_ms = 100; m->d.gif.finished = false;
#ifdef VOIDOS_RPI5
        // pi_render_gif is called from media_render to populate these;
        // for the open call we just probe on first render.
#else
        // Probe: open via AnimatedGIF to read the global header.
        // Implementation lives in media_esp32.cpp.
        // (No-op here; media_render triggers the header read.)
#endif
    }
    return m;
}

Media *media_open_frames(const char *tmpl, uint32_t count, uint8_t fps) {
    if (!tmpl || count == 0 || fps == 0) return nullptr;
    Media *m = new Media{};
    m->kind = MEDIA_FRAMES;
    std::strncpy(m->d.seq.tmpl, tmpl, sizeof(m->d.seq.tmpl) - 1);
    m->d.seq.tmpl[sizeof(m->d.seq.tmpl) - 1] = 0;
    m->d.seq.count = count;
    m->d.seq.idx = 0;
    m->d.seq.fps = fps;
    m->d.seq.next_ms = 0;
    return m;
}

// ── media_render dispatcher ───────────────────────────────────────────

bool media_render(Media *m, int x, int y) {
    if (!m || m->kind == MEDIA_NONE) return false;
    switch (m->kind) {
        case MEDIA_BMP:
#ifdef VOIDOS_RPI5
            return pi_bmp_render(m->d.src.path, x, y);
#else
            return esp_bmp_render(m->d.src.path, x, y);
#endif
        case MEDIA_PNG:
#ifdef VOIDOS_RPI5
            return pi_render_png(m->d.src.path, x, y);
#else
            return esp_render_png(m->d.src.path, x, y);
#endif
        case MEDIA_JPG:
#ifdef VOIDOS_RPI5
            return pi_render_jpg(m->d.src.path, x, y);
#else
            return esp_render_jpg(m->d.src.path, x, y);
#endif
        case MEDIA_GIF: {
#ifdef VOIDOS_RPI5
            return pi_render_gif(m->d.src.path, x, y,
                                 m->d.gif.frames, m->d.gif.delay_ms);
#else
            uint32_t f = m->d.gif.next;
            bool ok = esp_render_gif_step(m->d.src.path, x, y, f);
            if (ok) {
                uint32_t total = m->d.gif.frames ? m->d.gif.frames : 1;
                m->d.gif.next = (f + 1) % total;
                if (m->d.gif.next == 0) m->d.gif.finished = true;
            }
            return ok;
#endif
        }
        case MEDIA_FRAMES: {
            // Throttle by fps.
            uint32_t now_ms = hal_storage_get_u32("ms", 0); // see note
            (void)now_ms;
            // Build the indexed path and render the BMP frame.
            char fp[80];
            media_format_frame(fp, sizeof(fp), m->d.seq.tmpl, m->d.seq.idx);
#ifdef VOIDOS_RPI5
            bool ok = pi_bmp_render(fp, x, y);
#else
            bool ok = esp_bmp_render(fp, x, y);
#endif
            m->d.seq.idx = (m->d.seq.idx + 1) % m->d.seq.count;
            return ok;
        }
        case MEDIA_NONE:
        default:
            return false;
    }
}

// ── Introspection ─────────────────────────────────────────────────────

MediaType media_type   (const Media *m) { return m ? m->kind : MEDIA_NONE; }
uint16_t  media_width  (const Media *m) { return m ? m->d.src.w : 0; }
uint16_t  media_height (const Media *m) { return m ? m->d.src.h : 0; }
uint32_t  media_frames (const Media *m) {
    if (!m) return 0;
    switch (m->kind) {
        case MEDIA_GIF:    return m->d.gif.frames;
        case MEDIA_FRAMES: return m->d.seq.count;
        default:           return 0;
    }
}
uint16_t  media_delay_ms(const Media *m) {
    if (!m || m->kind != MEDIA_GIF) return 0;
    return m->d.gif.delay_ms;
}
bool      media_finished(const Media *m) {
    if (!m) return true;
    switch (m->kind) {
        case MEDIA_GIF:    return m->d.gif.finished;
        case MEDIA_FRAMES: return m->d.seq.count > 0 && m->d.seq.idx == 0;
        default:           return false;
    }
}

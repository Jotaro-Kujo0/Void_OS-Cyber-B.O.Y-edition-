// media.h — unified image/animation renderer.
// Opaque Media* handle: open, blit each frame, close.
// Handles BMP/PNG/JPG/GIF + frame-sequence "video" by path template.
// Streams scanline-by-scanline; only GIF keeps small palette state.

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MEDIA_NONE  = 0,
    MEDIA_BMP   = 1,
    MEDIA_PNG   = 2,
    MEDIA_JPG   = 3,
    MEDIA_GIF   = 4,
    MEDIA_FRAMES = 5,   // sequence of files via a path template
} MediaType;

typedef struct Media Media;

// ── File-based opens ────────────────────────────────────────────────────

// Single-shot open for static images (BMP/PNG/JPG).
// Animated GIF: open + render repeatedly; the dispatcher loops at the
// GIF's native frame delay.
Media *media_open (const char *path);

// Frame-sequence "video". `path_template` uses %04d (or any printf
// int) for the frame index. `count` is the number of frames.
// `fps` is the rendering rate; the actual rate is bounded by decode time.
Media *media_open_frames (const char *path_template,
                          uint32_t count, uint8_t fps);

// ── Lifecycle ───────────────────────────────────────────────────────────

bool   media_render (Media *m, int x, int y);   // draws (next) frame
void   media_close  (Media *m);

// ── Introspection ───────────────────────────────────────────────────────

MediaType media_type    (const Media *m);
uint16_t  media_width   (const Media *m);     // 0 if unknown
uint16_t  media_height  (const Media *m);     // 0 if unknown
uint32_t  media_frames  (const Media *m);     // 0 = single-frame
uint16_t  media_delay_ms(const Media *m);     // per-frame delay (GIF)
bool      media_finished(const Media *m);     // true once at end-of-sequence

// ── Path helpers ────────────────────────────────────────────────────────

// Returns the inferred MediaType given a path string's extension.
// Returns MEDIA_NONE if the extension is unknown. Used by apps that want
// to skip a path before opening it.
MediaType media_type_from_path(const char *path);

// Build "/sd/anim/frame_0001.bmp"-style paths for frame-sequence video.
size_t media_format_frame(char *out, size_t out_len,
                          const char *path_template, uint32_t frame_idx);

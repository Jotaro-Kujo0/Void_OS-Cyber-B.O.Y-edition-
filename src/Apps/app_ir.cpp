// app_ir.cpp — IR via LIRC. Modes: LIB / TVBG / LEARN.
// Needs dtoverlay=gpio-ir(+tx) pins 23/22 in config.txt.

#include "app_ir.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

#ifdef VOIDOS_RPI5
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/lirc.h>
#endif

#ifdef VOIDOS_RPI5

// ─── Constants ────────────────────────────────────────────────────────
#define LIRC_DEV       "/dev/lirc0"
#define IR_SLOTS       8
#define IR_CAPTURE_MAX 1024
#define TVBG_MAX_PAIRS 256
#define TVBG_DB_PATH   "/mnt/void-os/ir/tvbg_power_codes.bin"

// ─── TV-B-Gone binary record (must match file format exactly) ────────
struct TvbgCode {
    uint16_t freq;
    uint16_t code_count;
    uint16_t pairs[TVBG_MAX_PAIRS];
    char     label[24];
};

// ─── Saved code slot ─────────────────────────────────────────────────
struct IRRecord {
    uint64_t code;
    uint16_t protocol;
    uint16_t bits;
    char     label[14];
};

// ─── Sub-modes ────────────────────────────────────────────────────────
enum SubMode { MODE_LIB = 0, MODE_TVBG = 1, MODE_LEARN = 2 };

// ─── State ────────────────────────────────────────────────────────────
static SubMode  _mode        = MODE_LIB;
static char     _status[64]  = "READY";
static int      _lirc_fd     = -1;
static uint32_t _frame       = 0;

// Library state
static IRRecord _slots[IR_SLOTS];
static uint8_t  _count       = 0;
static uint8_t  _cur         = 0;

// TV-B-Gone state
static FILE*    _tvbg_file   = nullptr;
static bool     _tvbg_busy   = false;

// Learn state
static bool     _learning    = false;
static uint32_t _cap_buf[IR_CAPTURE_MAX];
static int      _cap_count   = 0;
static uint32_t _cap_last_ns = 0;

// ─── Helpers ──────────────────────────────────────────────────────────

static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void lirc_init() {
    if (_lirc_fd >= 0) close(_lirc_fd);
    _lirc_fd = open(LIRC_DEV, O_RDWR | O_NONBLOCK);
    if (_lirc_fd < 0)
        snprintf(_status, sizeof(_status), "LIRC OPEN FAIL: %m");
    else
        snprintf(_status, sizeof(_status), "READY");
}

static bool lirc_tx(uint32_t* pulses, size_t count, uint32_t freq_hz) {
    if (_lirc_fd < 0) return false;
    ioctl(_lirc_fd, LIRC_SET_SEND_CARRIER, &freq_hz);
    ssize_t written = write(_lirc_fd, pulses, count * sizeof(uint32_t));
    return written > 0;
}

static void load_library() {
    _count = 0;
    for (int i = 0; i < IR_SLOTS; i++) {
        char key[12];
        snprintf(key, sizeof(key), "ir%d", i);
        size_t sz = sizeof(IRRecord);
        if (hal_storage_get_blob(key, &_slots[_count], &sz))
            _count++;
    }
}

static void save_slot(IRRecord& r) {
    if (_count >= IR_SLOTS) {
        snprintf(_status, sizeof(_status), "SLOTS FULL");
        return;
    }
    _slots[_count] = r;
    char key[12];
    snprintf(key, sizeof(key), "ir%d", _count);
    hal_storage_set_blob(key, &r, sizeof(IRRecord));
    snprintf(_status, sizeof(_status), "SAVED [%d]", _count);
    _count++;
}

// ─── Cap waveform: save to /mnt/void-os/ir/capture_<UTC>.txt ─────────
static void save_capture() {
    char path[64];
    time_t now = time(nullptr);
    snprintf(path, sizeof(path), "/mnt/void-os/ir/capture_%lu.txt", (unsigned long)now);
    FILE* fp = fopen(path, "w");
    if (!fp) { snprintf(_status, sizeof(_status), "SAVE FAIL"); return; }
    fprintf(fp, "# IR capture — %lu pulses\n", (unsigned long)_cap_count);
    for (int i = 0; i < _cap_count; i++) {
        bool on = (i % 2 == 0);
        fprintf(fp, "%s %u\n", on ? "ON" : "OFF", _cap_buf[i]);
    }
    fclose(fp);
    snprintf(_status, sizeof(_status), "SAVED %d pulses", _cap_count);
}

// ─── TV-B-Gone: transmit next code from file ──────────────────────────
static void tvbg_step() {
    if (!_tvbg_file) {
        _tvbg_file = fopen(TVBG_DB_PATH, "rb");
        if (!_tvbg_file) {
            snprintf(_status, sizeof(_status), "DB NOT FOUND");
            _tvbg_busy = false;
            return;
        }
    }
    TvbgCode c;
    if (fread(&c, sizeof(c), 1, _tvbg_file) != 1) {
        snprintf(_status, sizeof(_status), "TVBG DONE");
        fclose(_tvbg_file);
        _tvbg_file = nullptr;
        _tvbg_busy = false;
        return;
    }
    uint32_t p[TVBG_MAX_PAIRS];
    for (uint16_t i = 0; i < c.code_count && i < TVBG_MAX_PAIRS; i++)
        p[i] = c.pairs[i];
    lirc_tx(p, c.code_count, c.freq);
    snprintf(_status, sizeof(_status), "TX: %s", c.label);
}

// ─── Waveform renderer (fills bottom half of screen) ──────────────────
static void draw_waveform() {
    int y_base = STATS_H + 40;
    int w_max  = SCR_W - 16;
    int h_max  = SCR_H - y_base - 20;

    draw_hline(8, y_base + h_max / 2, w_max, T_BORDER);
    if (_cap_count == 0) return;

    // Find max pulse duration for scaling
    uint32_t max_val = 1;
    for (int i = 0; i < _cap_count; i++)
        if (_cap_buf[i] > max_val) max_val = _cap_buf[i];

    int x = 8;
    int dx = w_max / _cap_count;
    if (dx < 1) dx = 1;

    for (int i = 0; i < _cap_count && x < 8 + w_max; i++) {
        int h = (int)((uint64_t)_cap_buf[i] * h_max / 2 / max_val);
        bool on = (i % 2 == 0); // even = IR on (pulse)
        int y = on ? y_base + h_max / 2 - h : y_base + h_max / 2;
        draw_fill(x, y, dx > 1 ? dx - 1 : 1, h, on ? T_FG : T_DIM);
        x += dx;
    }
}

void app_ir_init() {
    _mode = MODE_LIB;
    _count = 0;
    _cur = 0;
    _learning = false;
    _tvbg_busy = false;
    _tvbg_file = nullptr;
    _cap_count = 0;
    load_library();
    lirc_init();
}

void app_ir_suspend() {
    _learning = false;
    _tvbg_busy = false;
    if (_tvbg_file) { fclose(_tvbg_file); _tvbg_file = nullptr; }
    if (_lirc_fd >= 0) { close(_lirc_fd); _lirc_fd = -1; }
}

void app_ir_resume() {
    lirc_init();
}

void app_ir_event(Event e) {
    // ── Mode scroll via pot ───────────────────────────────────────
    if (e.type == EVT_POT_CHANGED) {
        _mode = (SubMode)((e.data * 3) / 256);
        if (_mode > MODE_LEARN) _mode = MODE_LEARN;
        if (_mode == MODE_TVBG && _tvbg_file) {
            fclose(_tvbg_file); _tvbg_file = nullptr;
        }
        return;
    }

    // ── Button A: mode-specific action ────────────────────────────
    if (e.type == EVT_BTN_A_DOWN) {
        switch (_mode) {

        case MODE_LIB:
            // Play selected code
            if (_count > 0 && _cur < _count) {
                IRRecord& r = _slots[_cur];
                uint32_t p[2];
                p[0] = 1000;  // placeholder pulse
                p[1] = 500;   // placeholder space
                lirc_tx(p, 2, 38000);
                snprintf(_status, sizeof(_status), "SENT [%d] %s", _cur, r.label);
            }
            break;

        case MODE_TVBG:
            if (!_tvbg_busy) {
                _tvbg_busy = true;
                tvbg_step();
            } else {
                _tvbg_busy = false;
                snprintf(_status, sizeof(_status), "TVBG STOPPED");
            }
            break;

        case MODE_LEARN:
            _learning = !_learning;
            _cap_count = 0;
            _cap_last_ns = now_ns();
            snprintf(_status, sizeof(_status), _learning ? "LEARNING..." : "READY");
            break;
        }
    }

    // ── Button B: back / stop ─────────────────────────────────────
    if (e.type == EVT_BTN_B_DOWN) {
        _learning = false;
        _tvbg_busy = false;
        snprintf(_status, sizeof(_status), "READY");
    }

    // ── Button C: save captured waveform ──────────────────────────
    if (e.type == EVT_BTN_C_DOWN && _learning && _cap_count > 0) {
        save_capture();
        _learning = false;
    }
}

void app_ir_tick() {
    _frame++;

    // ── TV-B-Gone: advance to next code every 500ms ──────────────
    if (_tvbg_busy && (_frame % 5 == 0)) {
        tvbg_step();
    }

    // ── Learn mode: read LIRC pulses ─────────────────────────────
    if (_learning && _lirc_fd >= 0) {
        uint32_t data;
        ssize_t n = read(_lirc_fd, &data, sizeof(data));
        if (n == sizeof(data)) {
            if (_cap_count < IR_CAPTURE_MAX) {
                _cap_buf[_cap_count++] = data & LIRC_VALUE_MASK;
            } else {
                _learning = false;
                snprintf(_status, sizeof(_status), "BUFFER FULL (%d)", IR_CAPTURE_MAX);
            }
        }
    }
}

void app_ir_draw() {
    // ── Header ────────────────────────────────────────────────────
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "IR REMOTE", T_FG, T_PANEL, FONT_SM);

    // ── Mode tabs ─────────────────────────────────────────────────
    const char* tabs[] = { "LIB", "TVBG", "LEARN" };
    for (int i = 0; i < 3; i++) {
        bool sel = (_mode == i);
        draw_text(8 + i * 40, STATS_H + 4,
          tabs[i], sel ? T_FG : T_DIM, T_BG, FONT_SM);
    }
    draw_hline(0, STATS_H + 20, SCR_W, T_BORDER);

    // ── Mode-specific content ─────────────────────────────────────
    int y = STATS_H + 24;

    if (_mode == MODE_LIB) {
        // Show saved library
        draw_text(8, y, "LIBRARY:", T_DIM, T_BG, FONT_SM);
        y += 14;
        for (int i = 0; i < _count; i++) {
            bool sel = (i == _cur);
            draw_textf(8, y, sel ? T_FG : T_DIM, T_BG, FONT_SM,
                       "%s[%d] %-10s %08llX",
                       sel ? ">" : " ", i, _slots[i].label,
                       (unsigned long long)_slots[i].code);
            y += 14;
        }
        if (_count == 0)
            draw_text(8, y, "no codes saved", T_BORDER, T_BG, FONT_SM);

    } else if (_mode == MODE_TVBG) {
        // TV-B-Gone status
        draw_text(8, y, "TV-B-GONE", T_FG, T_BG, FONT_SM);
        y += 16;
        draw_text(8, y, _tvbg_busy ? "TRANSMITTING..." : "Press [A] to cycle",
                  _tvbg_busy ? T_WARN : T_DIM, T_BG, FONT_SM);
        y += 16;
        draw_text(8, y, "DB: tvbg_power_codes.bin", T_DIM, T_BG, FONT_SM);

    } else if (_mode == MODE_LEARN) {
        // IR capture with waveform
        if (_learning) {
            int r = 10 + (int)(_frame % 16);
            draw_circle(40, y + 20, r, T_FG);
            draw_circle(40, y + 20, r + 8, T_BORDER);
            draw_text(60, y + 14, "LISTENING...", T_FG, T_BG, FONT_SM);
        } else {
            draw_text(8, y, "Press [A] to learn", T_DIM, T_BG, FONT_SM);
        }
        draw_textf(8, y + 40, T_DIM, T_BG, FONT_SM,
                   "%d / %d pulses", _cap_count, IR_CAPTURE_MAX);

        // Draw waveform if we have captures
        if (_cap_count > 0) draw_waveform();
    }

    // ── Footer ────────────────────────────────────────────────────
    draw_hline(0, SCR_H - 28, SCR_W, T_BORDER);
    draw_fill(0, SCR_H - 28, SCR_W, 28, T_PANEL);
    draw_textf(8, SCR_H - 22, T_FG, T_PANEL, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] ACT  [B] STOP  [C] SAVE  POT=mode",
              T_DIM, T_BG, FONT_SM);
}

#else  // ESP32 — IR needs Linux LIRC (/dev/lirc0)
static char _stub_status[24] = "IR: LINUX ONLY";

void app_ir_init() {
    std::snprintf(_stub_status, sizeof(_stub_status), "IR: LINUX ONLY");
}
void app_ir_tick() {}
void app_ir_suspend() {}
void app_ir_resume() {}
void app_ir_event(Event e) { (void)e; }
void app_ir_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_text(8, 8, "IR REMOTE", T_FG, T_BG, FONT_SM);
    draw_text(8, 44, _stub_status, T_WARN, T_BG, FONT_SM);
    draw_text(8, 62, "Requires /dev/lirc0", T_DIM, T_BG, FONT_SM);
}
#endif  // VOIDOS_RPI5

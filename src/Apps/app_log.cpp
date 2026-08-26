// app_log.cpp — Field logger & PCAP/CSV/GPX writer (Raspberry Pi 5)
//
// Five sub-modes: PCAP (WiFi), CSV (wardriving), GPX (track),
// HEAT (RSSI heatmap), WORD (wordlist stream).
// All files written to /mnt/void-os via stdio.

#include "app_log.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_gps.h"
#include "../config.h"
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef VOIDOS_RPI5
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <dirent.h>
#endif

#ifdef VOIDOS_RPI5

// ─── Constants ────────────────────────────────────────────────────────
#define CAPTURE_DIR     "/mnt/void-os/captures"
#define GPX_DIR         "/mnt/void-os/gpx"
#define HEAT_DIR        "/mnt/void-os/heatmap"
#define WORDLIST_DIR    "/mnt/void-os/wordlists"
#ifndef PCAP_SNAP_LEN
#define PCAP_SNAP_LEN   65535
#endif
#define PCAP_LINKTYPE   127     // LINKTYPE_IEEE802_11_RADIO

// ─── Sub-modes ────────────────────────────────────────────────────────
static const char *LABELS[] = {
    "PCAP  (WIFI)",
    "CSV   (WARD)",
    "GPX   (TRACK)",
    "HEAT  (RSSI)",
    "WORD  (DICT)",
};
static const uint8_t N_ROWS = 5;

// ─── State ────────────────────────────────────────────────────────────
static uint8_t  _row        = 0;
static bool     _active     = false;
static char     _status[48] = "READY";

// Current open file handle
static FILE*    _fp         = nullptr;
static char     _fname[128] = "";

// GPX state
static time_t   _gpx_start  = 0;

// Wordlist state
static char     _wl_lines[8][64];
static uint8_t  _wl_count   = 0;
static uint8_t  _wl_cur     = 0;
static FILE*    _wl_fp      = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────
static uint64_t utc_now() {
    return (uint64_t)time(nullptr);
}

static void ensure_dir(const char* dir) {
    mkdir(dir, 0755);
}

static void close_file() {
    if (_fp) { fclose(_fp); _fp = nullptr; }
}

// ─── PCAP ─────────────────────────────────────────────────────────────

static void pcap_write_global_header(FILE* fp) {
    uint32_t magic    = 0xA1B2C3D4;
    uint16_t ver_maj  = 2;
    uint16_t ver_min  = 4;
    uint32_t reserved = 0;
    uint32_t snaplen  = PCAP_SNAP_LEN;
    uint32_t linktype = PCAP_LINKTYPE;

    fwrite(&magic,    4, 1, fp);
    fwrite(&ver_maj,  2, 1, fp);
    fwrite(&ver_min,  2, 1, fp);
    fwrite(&reserved, 4, 1, fp);
    fwrite(&snaplen,  4, 1, fp);
    fwrite(&linktype, 4, 1, fp);
}

static void pcap_start() {
    ensure_dir(CAPTURE_DIR);
    snprintf(_fname, sizeof(_fname), "%s/wifi_%lu.pcap",
             CAPTURE_DIR, utc_now());
    _fp = fopen(_fname, "wb");
    if (_fp) {
        pcap_write_global_header(_fp);
        snprintf(_status, sizeof(_status), "PCAP: REC");
    } else {
        snprintf(_status, sizeof(_status), "PCAP: ERR %m");
        _active = false;
    }
}

// Called by app_wifi when a frame is available.
void app_log_pcap_write(const uint8_t* frame, uint32_t len,
                         uint32_t ts_sec, uint32_t ts_usec) {
    if (!_fp || !_active || _row != 0) return;
    uint32_t incl_len = (len > PCAP_SNAP_LEN) ? PCAP_SNAP_LEN : len;
    fwrite(&ts_sec,  4, 1, _fp);
    fwrite(&ts_usec, 4, 1, _fp);
    fwrite(&incl_len, 4, 1, _fp);
    fwrite(&incl_len, 4, 1, _fp);
    fwrite(frame,    incl_len, 1, _fp);
    fflush(_fp);
}

// ─── CSV (Wardriving) ────────────────────────────────────────────────

static void csv_start() {
    ensure_dir(CAPTURE_DIR);
    snprintf(_fname, sizeof(_fname), "%s/wardriving_%lu.csv",
             CAPTURE_DIR, utc_now());
    _fp = fopen(_fname, "w");
    if (_fp) {
        fprintf(_fp, "timestamp,latitude,longitude,rssi,bssid,ssid,channel,auth\n");
        snprintf(_status, sizeof(_status), "CSV: WRITING");
    } else {
        snprintf(_status, sizeof(_status), "CSV: ERR %m");
        _active = false;
    }
}

void app_log_csv_write(double lat, double lon, int8_t rssi,
                        const char* bssid, const char* ssid,
                        uint8_t channel, const char* auth) {
    if (!_fp || !_active || _row != 1) return;
    fprintf(_fp, "%lu,%.8f,%.8f,%d,%s,%s,%u,%s\n",
            utc_now(), lat, lon, rssi,
            bssid ? bssid : "00:00:00:00:00:00",
            ssid  ? ssid  : "",
            channel,
            auth  ? auth  : "");
    fflush(_fp);
}

// ─── GPX Track ────────────────────────────────────────────────────────

static void gpx_start() {
    ensure_dir(GPX_DIR);
    snprintf(_fname, sizeof(_fname), "%s/track_%lu.gpx",
             GPX_DIR, utc_now());
    _fp = fopen(_fname, "w");
    if (_fp) {
        fprintf(_fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        fprintf(_fp, "<gpx version=\"1.1\" creator=\"Void-OS\">\n");
        fprintf(_fp, "  <trk><name>track_%lu</name>\n", utc_now());
        fprintf(_fp, "  <trkseg>\n");
        _gpx_start = time(nullptr);
        snprintf(_status, sizeof(_status), "GPX: TRACKING");
    } else {
        snprintf(_status, sizeof(_status), "GPX: ERR %m");
        _active = false;
    }
}

static void gpx_write_point() {
    if (!_fp || _row != 2) return;
    double lat = hal_gps_lat();
    double lon = hal_gps_lon();
    fprintf(_fp, "    <trkpt lat=\"%.8f\" lon=\"%.8f\">\n", lat, lon);
    fprintf(_fp, "      <time>%lu</time>\n", utc_now());
    fprintf(_fp, "    </trkpt>\n");
    fflush(_fp);
}

static void gpx_close() {
    if (!_fp || _row != 2) return;
    fprintf(_fp, "  </trkseg>\n");
    fprintf(_fp, "  </trk>\n");
    fprintf(_fp, "</gpx>\n");
    close_file();
}

// ─── HEAT (RSSI Heatmap) ─────────────────────────────────────────────

static void heat_start() {
    ensure_dir(HEAT_DIR);
    snprintf(_fname, sizeof(_fname), "%s/heatmap_%lu.csv",
             HEAT_DIR, utc_now());
    _fp = fopen(_fname, "w");
    if (_fp) {
        fprintf(_fp, "latitude,longitude,rssi,band,ssid\n");
        snprintf(_status, sizeof(_status), "HEAT: LOGGING");
    } else {
        snprintf(_status, sizeof(_status), "HEAT: ERR %m");
        _active = false;
    }
}

void app_log_heat_write(double lat, double lon, int8_t rssi,
                          const char* band, const char* ssid) {
    if (!_fp || !_active || _row != 3) return;
    fprintf(_fp, "%.8f,%.8f,%d,%s,%s\n",
            lat, lon, rssi,
            band ? band : "?",
            ssid ? ssid : "");
    fflush(_fp);
}

// ─── WORD (Wordlist Stream) ───────────────────────────────────────────

static void wordlist_open() {
    ensure_dir(WORDLIST_DIR);
    DIR* d = opendir(WORDLIST_DIR);
    if (!d) {
        snprintf(_status, sizeof(_status), "WORD: NO DIR");
        _active = false;
        return;
    }
    _wl_count = 0;
    _wl_cur   = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) && _wl_count < 8) {
        if (ent->d_name[0] == '.') continue;
        std::strncpy(_wl_lines[_wl_count], ent->d_name,
                     sizeof(_wl_lines[0]) - 1);
        _wl_lines[_wl_count][sizeof(_wl_lines[0]) - 1] = '\0';
        _wl_count++;
    }
    closedir(d);

    if (_wl_count == 0) {
        snprintf(_status, sizeof(_status), "WORD: EMPTY");
        _active = false;
    } else {
        snprintf(_fname, sizeof(_fname), "%s/%s",
                 WORDLIST_DIR, _wl_lines[0]);
        _wl_fp = fopen(_fname, "r");
        snprintf(_status, sizeof(_status), "WORD: %.40s", _wl_lines[0]);
    }
}

const char* app_log_wordlist_next() {
    if (!_wl_fp || !_active || _row != 4) return nullptr;
    char buf[128];
    while (fgets(buf, sizeof(buf), _wl_fp)) {
        // Strip newline
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0) {
            // Return in a static buffer (caller must use immediately)
            static char result[128];
            snprintf(result, sizeof(result), "%s", buf);
            return result;
        }
    }
    // EOF on current file — try next wordlist
    fclose(_wl_fp);
    _wl_fp = nullptr;
    _wl_cur++;
    if (_wl_cur < _wl_count) {
        snprintf(_fname, sizeof(_fname), "%s/%s",
                 WORDLIST_DIR, _wl_lines[_wl_cur]);
        _wl_fp = fopen(_fname, "r");
        snprintf(_status, sizeof(_status), "WORD: %.40s", _wl_lines[_wl_cur]);
        return app_log_wordlist_next();  // recurse into next file
    }
    snprintf(_status, sizeof(_status), "WORD: EOF");
    _active = false;
    return nullptr;
}

// ─── Common start/stop per mode ───────────────────────────────────────

static void mode_start() {
    close_file();
    if (_wl_fp) { fclose(_wl_fp); _wl_fp = nullptr; }
    _active = true;
    switch (_row) {
        case 0: pcap_start();  break;
        case 1: csv_start();   break;
        case 2: gpx_start();   break;
        case 3: heat_start();  break;
        case 4: wordlist_open(); break;
    }
}

static void mode_stop() {
    if (_row == 2 && _fp) gpx_close();
    if (_wl_fp) { fclose(_wl_fp); _wl_fp = nullptr; }
    close_file();
    _active = false;
    snprintf(_status, sizeof(_status), "STOPPED");
}

// ─── SD card info ─────────────────────────────────────────────────────

static int sd_percent_used() {
    struct statfs st;
    if (statfs("/mnt/void-os", &st) != 0) return -1;
    uint64_t total = (uint64_t)st.f_blocks * st.f_bsize;
    uint64_t free_  = (uint64_t)st.f_bfree  * st.f_bsize;
    if (total == 0) return -1;
    return (int)((total - free_) * 100 / total);
}

static bool sd_available() {
    struct statfs st;
    return (statfs("/mnt/void-os", &st) == 0);
}

// ─── Public API ───────────────────────────────────────────────────────

void app_log_init() {
    _row = 0;
    _active = false;
    snprintf(_status, sizeof(_status), "READY");
    ensure_dir("/mnt/void-os");
}

void app_log_tick() {
    // GPX: write a trackpoint every second
    if (_active && _row == 2 && _fp) {
        static time_t last_gpx = 0;
        time_t now = time(nullptr);
        if (now != last_gpx) {
            last_gpx = now;
            gpx_write_point();
        }
    }
}

void app_log_suspend() {
    mode_stop();
}

void app_log_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        mode_stop();
        return;
    }
    if (e.type == EVT_POT_CHANGED) {
        uint8_t new_row = (e.data * N_ROWS) / 256;
        if (new_row >= N_ROWS) new_row = N_ROWS - 1;
        if (new_row != _row) {
            mode_stop();
            _row = new_row;
        }
        return;
    }
    if (e.type == EVT_BTN_A_DOWN) {
        if (_active) {
            mode_stop();
        } else {
            mode_start();
        }
    }
}

void app_log_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "FIELD LOG", T_FG, T_PANEL, FONT_SM);

    int pct = sd_percent_used();
    if (pct >= 0)
        draw_textf(SCR_W - 56, 8, T_DIM, T_PANEL, FONT_SM, "%d%%", pct);
    else
        draw_text(SCR_W - 56, 8, "NO SD", T_WARN, T_PANEL, FONT_SM);

    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + (int)i * 26;
        draw_textf(8, y,
                   _active ? (_row == i ? T_WARN : T_DIM)
                           : (i == _row ? T_FG : T_DIM),
                   T_BG, FONT_SM, "%s %s",
                   (i == _row ? ">" : " "), LABELS[i]);
    }

    draw_textf(8, SCR_H - 36, _active ? T_WARN : T_DIM, T_BG, FONT_SM,
               "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] SELECT  [B] BACK", T_DIM, T_BG, FONT_SM);
}

#else  // ESP32 — logger writes to Linux /mnt/void-os
static char _stub_status[24] = "LOG: LINUX ONLY";

void app_log_init() {
    std::snprintf(_stub_status, sizeof(_stub_status), "LOG: LINUX ONLY");
}
void app_log_tick() {}
void app_log_suspend() {}
void app_log_event(Event e) { (void)e; }
void app_log_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_text(8, 8, "FIELD LOG", T_FG, T_BG, FONT_SM);
    draw_text(8, 44, _stub_status, T_WARN, T_BG, FONT_SM);
    draw_text(8, 62, "Needs /mnt/void-os", T_DIM, T_BG, FONT_SM);
}
#endif  // VOIDOS_RPI5

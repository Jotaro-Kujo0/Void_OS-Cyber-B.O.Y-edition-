// app_body.cpp — passive Wi-Fi body / probe capture (Raspberry Pi 5)
//
// Pipeline: forks `tcpdump -l -e` -> pipe -> line parser -> ring buffer.
// Parses MAC addresses and SSIDs from 802.11 Probe Requests.

#include "app_body.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../hal/hal_metrics.h"
#include "../os/scheduler.h"
#include "../config.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#ifdef VOIDOS_RPI5
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#endif

#ifdef VOIDOS_RPI5

//Time Helper
static uint32_t sys_millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Entry + Ring
struct BodyEntry {
    uint8_t  mac[6];
    uint32_t count;
    uint32_t first_ms;
    uint32_t last_ms;
    char     ssid[33];
};

#define BODY_RING_MAX 64
static BodyEntry _ring[BODY_RING_MAX];
static uint16_t  _ring_n = 0;
static int8_t    _pump_id = -1;
static bool      _sniff = false;
static char      _status[24] = "READY";

//OUI Vendor Table 
//wallahi Im cooked
struct VendorEntry { uint32_t oui; const char *name; };

static const VendorEntry VENDORS[] = {
    { 0x000000, "Unspecified"},
    { 0x001A11, "Apple"},     { 0x002241, "Apple"},     { 0x041E64, "Apple"},
    { 0x1430A1, "Apple"},     { 0x18AF61, "Apple"},     { 0x245E48, "Apple"},
    { 0x3C0754, "Apple"},     { 0xA4B197, "Apple"},     { 0x60FB42, "Apple"},
    { 0x40CBC0, "Apple"},     { 0xB0CA68, "Apple"},     { 0x94E96A, "Apple"},
    { 0x48D682, "Apple"},     { 0xDC56E7, "Apple"},     { 0x70C9C6, "Apple"},
    { 0x8866A5, "Apple"},
    { 0x3C5AB4, "Google"},    { 0xF4F5D8, "Google"},    { 0xF8FF0B, "Google"},
    { 0x70B3D5, "Cisco"},     { 0x00163E, "Cisco"},     { 0xB07FB9, "Cisco"},
    { 0x6C5A34, "Cisco"},
    { 0xD8B377, "Samsung"},   { 0xFCAA14, "Samsung"},   { 0x9800C4, "Samsung"},
    { 0xB8C6BB, "Samsung"},
    { 0x001E58, "D-Link"},
    { 0x002401, "Xiaomi"},    { 0x146180, "Xiaomi"},
    { 0xACE069, "Huawei"},    { 0x1062EB, "Huawei"},    { 0x80B686, "Huawei"},
    { 0x0025B3, "Microsoft"}, { 0x60D248, "Microsoft"}, { 0xF8CFC2, "Microsoft"},
    { 0xE4F0D8, "Microsoft"}, { 0x0050F2, "Microsoft"},
    { 0x001D0E, "Sony"},      { 0xFC0FE6, "Sony"},
    { 0xCC36CC, "TP-Link"},   { 0xE4D332, "TP-Link"},   { 0xDCA632, "TP-Link"},
    { 0xAC84C9, "TP-Link"},   { 0xB0487A, "TP-Link"},
    { 0x001EBF, "Intel"},     { 0x7CB0C2, "Intel"},     { 0x685D43, "Intel"},
    { 0x8086F2, "Intel"},
    { 0xF0F8F5, "Hisense"},
    { 0x244C7B, "Espressif"}, { 0xAC67B2, "Espressif"},
};
static const uint8_t N_VENDORS = sizeof(VENDORS) / sizeof(VENDORS[0]);

static const char *vendor_for(const uint8_t *mac) {
    uint32_t key = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
    for (uint8_t i = 0; i < N_VENDORS; ++i) {
        if (VENDORS[i].oui == key) return VENDORS[i].name;
    }
    return "?";
}

//Ring Buff

static int16_t ring_lookup(const uint8_t *mac) {
    for (uint16_t i = 0; i < _ring_n; ++i) {
        if (memcmp(_ring[i].mac, mac, 6) == 0) return (int16_t)i;
    }
    if (_ring_n >= BODY_RING_MAX) return -1;
    return (int16_t)(_ring_n++);
}

void app_body_add_packet(const uint8_t *mac, const char *ssid) {
    if (!mac) return;
    int16_t idx = ring_lookup(mac);
    if (idx < 0) return;
    BodyEntry *e = &_ring[idx];
    if (e->count == 0) {
        memcpy(e->mac, mac, 6);
        e->first_ms = e->last_ms = sys_millis();
        snprintf(e->ssid, sizeof(e->ssid), "%s", ssid ? ssid : "");
    } else {
        if (ssid && ssid[0] && !e->ssid[0])
            snprintf(e->ssid, sizeof(e->ssid), "%s", ssid);
    }
    ++e->count;
    e->last_ms = sys_millis();
}

//tcpdump Pipe Pi 5

static pid_t  _tcpdump_pid = -1;
static int    _pipe_fd = -1;
static char   _line_buf[512];
static int    _line_pos = 0;

static void sniff_on() {
    if (_tcpdump_pid > 0) return;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        snprintf(_status, sizeof(_status), "PIPE ERR");
        return;
    }

    _tcpdump_pid = fork();
    if (_tcpdump_pid == 0) {
        // Child: redirect stdout to pipe, exec tcpdump
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        const char *args[] = {
            "tcpdump", "-l", "-e", "-i", "wlan0mon",
            "-s", "256", "type", "mgt", "subtype", "probe-req",
            NULL
        };
        execvp("/usr/bin/tcpdump", const_cast<char**>(args));
        _exit(1);
    }

    // Parent: close write end, set read end non-blocking
    close(pipefd[1]);
    _pipe_fd = pipefd[0];
    fcntl(_pipe_fd, F_SETFL, O_NONBLOCK);

    _sniff = true;
    snprintf(_status, sizeof(_status), "SNIFF ON");
}

static void sniff_off() {
    if (_tcpdump_pid > 0) {
        kill(_tcpdump_pid, SIGTERM);
        waitpid(_tcpdump_pid, NULL, 0);
        _tcpdump_pid = -1;
    }
    if (_pipe_fd >= 0) {
        close(_pipe_fd);
        _pipe_fd = -1;
    }
    _sniff = false;
    _line_pos = 0;
    snprintf(_status, sizeof(_status), "SNIFF OFF");
}

// Parse one tcpdump line: extract SA (source MAC) and SSID
static void parse_probe_line(const char *line) {
    // tcpdump -e format example:
    //   ... 0x... ...SA aa:bb:cc:dd:ee:ff ... SSID "MyNetwork" ...
    // We look for "SA " followed by MAC, and "SSID" followed by name in quotes

    const char *sa = strstr(line, "SA ");
    if (!sa) return;
    sa += 3;

    uint8_t mac[6];
    unsigned int m[6];
    if (sscanf(sa, "%02x:%02x:%02x:%02x:%02x:%02x",
               &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) return;
    for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)m[i];

    // Extract SSID (in quotes after "SSID" or "(")
    char ssid[33] = "";
    const char *ssid_ptr = strstr(line, "(\"");
    if (ssid_ptr) {
        ssid_ptr += 2;
        int i = 0;
        while (ssid_ptr[i] && ssid_ptr[i] != '"' && i < 32) {
            ssid[i] = ssid_ptr[i];
            ++i;
        }
        ssid[i] = '\0';
    }

    app_body_add_packet(mac, ssid);
}

static void pipe_pump() {
    if (!_sniff || _pipe_fd < 0) return;

    char buf[256];
    ssize_t n = read(_pipe_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == '\n' || _line_pos >= (int)sizeof(_line_buf) - 1) {
            _line_buf[_line_pos] = '\0';
            if (_line_pos > 10) parse_probe_line(_line_buf);
            _line_pos = 0;
        } else {
            _line_buf[_line_pos++] = buf[i];
        }
    }

    // Check if tcpdump died
    int status;
    pid_t r = waitpid(_tcpdump_pid, &status, WNOHANG);
    if (r == _tcpdump_pid) {
        _tcpdump_pid = -1;
        _sniff = false;
        snprintf(_status, sizeof(_status), "tcpdump died");
    }
}

// Lifecycle 

void app_body_init() {
    memset(_ring, 0, sizeof(_ring));
    _ring_n = 0;
    if (_pump_id < 0) _pump_id = sched_add("body_pump", pipe_pump, 200, 6);
}

void app_body_tick() { /* pump task does work */ }

void app_body_suspend() {
    sniff_off();
}

//Event Handle

void app_body_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        sniff_off();
        return;
    }
    if (e.type == EVT_BTN_A_DOWN) {
        if (_sniff) sniff_off();
        else sniff_on();
        return;
    }
    if (e.type == EVT_BTN_C_DOWN) {
        _ring_n = 0;
        snprintf(_status, sizeof(_status), "ring wiped");
        return;
    }
}

// Public Accessor

uint16_t app_body_visible_count() { return _ring_n; }

//Draw

struct RowCursor { uint16_t i; uint32_t count; };

static int rowcmp_count_desc(const void *a, const void *b) {
    uint32_t ca = ((const RowCursor*)a)->count;
    uint32_t cb = ((const RowCursor*)b)->count;
    if (cb > ca) return 1;
    if (cb < ca) return -1;
    return 0;
}

void app_body_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "BODY / PRESENCE", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 60, 8, _sniff ? T_WARN : T_DIM, T_PANEL, FONT_SM,
               "ON:%d", _sniff);

    // Sort by count descending
    RowCursor rows[BODY_RING_MAX] = {};
    uint16_t n = 0;
    for (uint16_t i = 0; i < _ring_n; ++i)
        rows[n++] = { i, _ring[i].count };
    qsort(rows, n, sizeof(RowCursor), rowcmp_count_desc);

    int y = STATS_H + 6;
    uint16_t shown = n > 12 ? 12 : n;
    for (uint16_t i = 0; i < shown; ++i) {
        BodyEntry *e = &_ring[rows[i].i];
        char macs[20];
        snprintf(macs, sizeof(macs), "%02x:%02x:%02x:%02x:%02x:%02x",
                 e->mac[0], e->mac[1], e->mac[2],
                 e->mac[3], e->mac[4], e->mac[5]);
        draw_textf(8, y + (int)i * 11, i == 0 ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %3u  %s  %s",
                   macs, e->count, vendor_for(e->mac),
                   e->ssid[0] ? e->ssid : "-");
    }

    int info_y = STATS_H + 6 + shown * 11 + 4;
    SystemUsage m = hal_metrics_snapshot();
    draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
               "CPU %u%%  MEM %u%%  RX %ukB",
               m.cpu_pct, m.mem_pct, m.net_rx_total_kb);

    draw_textf(8, SCR_H - 28, T_DIM, T_BG, FONT_SM,
               "%s  ring:%u/%u", _status, _ring_n, BODY_RING_MAX);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] ARM  [B] BACK  C=clear",
              T_DIM, T_BG, FONT_SM);
}

#else  // ESP32 — body tracking needs tcpdump (Linux)
static char _stub_status[24] = "BODY: LINUX ONLY";

void app_body_init() {
    std::snprintf(_stub_status, sizeof(_stub_status), "BODY: LINUX ONLY");
}
void app_body_tick() {}
void app_body_suspend() {}
void app_body_event(Event e) { (void)e; }
void app_body_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_text(8, 8, "BODY / PRESENCE", T_FG, T_BG, FONT_SM);
    draw_text(8, 44, _stub_status, T_WARN, T_BG, FONT_SM);
    draw_text(8, 62, "Needs tcpdump (Pi 5)", T_DIM, T_BG, FONT_SM);
}
#endif  // VOIDOS_RPI5

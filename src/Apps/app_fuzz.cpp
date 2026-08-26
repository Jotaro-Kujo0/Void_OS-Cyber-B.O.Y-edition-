// app_fuzz.cpp — small HTTP request fuzzer (Raspberry Pi 5)
//
// Sub-modes:
//   0. TARGET — pick from comma-separated `fuzz_targets` hal_storage list
//   1. REPS   — pick request count from {16, 64, 256, 1024}
//   2. RUN    — fire requests, log (target, status, latency_ms) to CSV

#include "app_fuzz.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../os/scheduler.h"
#include "../config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#ifdef VOIDOS_RPI5
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fstream>
#endif

//  UI + STATE

static const char *LABELS[] = { "TARGET", "REPS", "RUN" };
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);
static uint8_t _row    = 0;
static char    _status[24] = "READY";

static const uint8_t  N_REPS_TIERS = 4;
static const uint16_t REPS_TIERS[] = { 16, 64, 256, 1024 };
static uint8_t _reps_tier = 0;
static uint8_t _tgt_slot  = 0;
static char    _targets[8][64];

static uint16_t _done    = 0;
static int8_t   _pump_id = -1;
static bool     _running = false;

static const uint32_t REQ_PER_TICK = 2;  // keep UI responsive

//  PERSISTENCE

static const char *DEFAULT_TARGETS =
    "127.0.0.1:80,192.168.1.1:80,10.0.0.1:80,localhost:8080";

static void load_targets() {
    char buf[512] = {};
    hal_storage_get_str("fuzz_targets", buf, sizeof(buf));
    if (!buf[0]) std::snprintf(buf, sizeof(buf), "%s", DEFAULT_TARGETS);

    uint8_t n = 0;
    char *p = buf;
    while (*p && n < 8) {
        while (*p == ' ' || *p == ',') ++p;
        char *e = p;
        while (*e && *e != ',') ++e;
        if (e == p) break;
        uint8_t len = static_cast<uint8_t>(e - p);
        if (len >= 64) len = 63;
        std::memcpy(_targets[n], p, len);
        _targets[n][len] = '\0';
        ++n;
        if (!*e) break;
        p = e + 1;
    }
    _tgt_slot  = hal_storage_get_u8("fuzz_tgt", 0);
    _reps_tier = hal_storage_get_u8("fuzz_reps_tier", 1);
    if (_reps_tier >= N_REPS_TIERS) _reps_tier = 1;
}

//  FUZZER — Pi 5 path

#ifdef VOIDOS_RPI5

static void fuzz_log(const char *target, int status, long ms) {
    mkdir("/var/lib/void-os/loot", 0755);
    std::ofstream f("/var/lib/void-os/loot/fuzz.csv", std::ios::app);
    if (f) f << target << "," << status << "," << ms << "\n";
}

static int fuzz_one(const char *target) {
    char host[48] = {};
    uint16_t port = 80;
    std::snprintf(host, sizeof(host), "%s", target);
    char *colon = std::strchr(host, ':');
    if (colon) { *colon = 0; port = static_cast<uint16_t>(std::atoi(colon + 1)); }

    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);

    if (::inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        struct hostent *he = ::gethostbyname(host);
        if (!he) { ::close(s); return -1; }
        std::memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    }

    struct timeval t0;
    ::gettimeofday(&t0, nullptr);

    if (::connect(s, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) < 0) {
        ::close(s);
        fuzz_log(target, 0, 0);
        return -1;
    }

    const char *req = "GET / HTTP/1.0\r\nUser-Agent: void-os\r\n\r\n";
    ::send(s, req, std::strlen(req), 0);

    char buf[512] = {};
    int n = static_cast<int>(::recv(s, buf, sizeof(buf) - 1, 0));
    buf[n > 0 ? n : 0] = '\0';

    int status = 0;
    std::sscanf(buf, "HTTP/%*s %d", &status);

    struct timeval t1;
    ::gettimeofday(&t1, nullptr);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;

    ::close(s);
    fuzz_log(target, status, ms);
    return status;
}

static void fuzz_pump() {
    if (!_running) return;
    for (uint8_t k = 0; k < REQ_PER_TICK && _done < REPS_TIERS[_reps_tier]; ++k) {
        fuzz_one(_targets[_tgt_slot]);
        ++_done;
    }
    if (_done >= REPS_TIERS[_reps_tier]) {
        _running = false;
        std::snprintf(_status, sizeof(_status), "DONE %u", _done);
        return;
    }
    std::snprintf(_status, sizeof(_status), "%u/%u", _done, REPS_TIERS[_reps_tier]);
}

//  FUZZER — ESP32 stub

#else

static int fuzz_one(const char *target) {
    (void)target;
    return -1;
}

static void fuzz_pump() {
    if (!_running) return;
    _done = REPS_TIERS[_reps_tier];
    _running = false;
    std::snprintf(_status, sizeof(_status), "DONE (stub)");
}

#endif

//  LIFECYCLE

void app_fuzz_init() {
    load_targets();
    if (_pump_id < 0)
        _pump_id = sched_add("fuzz_pump", fuzz_pump, 0, 8);
}

void app_fuzz_tick()    {}
void app_fuzz_suspend() {}

//  EVENTS

void app_fuzz_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        _running = false;
        std::snprintf(_status, sizeof(_status), "READY");
        return;
    }

    if (e.type == EVT_POT_CHANGED) {
        if (_row == 0) {
            _tgt_slot = static_cast<uint8_t>((e.data * 8) / 256);
            if (_tgt_slot >= 8) _tgt_slot = 7;
            hal_storage_set_u8("fuzz_tgt", _tgt_slot);
            return;
        }
        if (_row == 1) {
            _reps_tier = static_cast<uint8_t>((e.data * N_REPS_TIERS) / 256);
            if (_reps_tier >= N_REPS_TIERS) _reps_tier = N_REPS_TIERS - 1;
            hal_storage_set_u8("fuzz_reps_tier", _reps_tier);
            return;
        }
        _row = static_cast<uint8_t>((e.data * N_ROWS) / 256);
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }

    if (e.type == EVT_BTN_A_DOWN) {
        if (_row == 2) {
            _done    = 0;
            _running = true;
            std::snprintf(_status, sizeof(_status), "fuzz..");
        }
        return;
    }
}

//  DRAW

void app_fuzz_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "FUZZ / HTTP", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 60, 8, _running ? T_WARN : T_DIM, T_PANEL, FONT_SM,
               "RUN:%d", _running);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;

    if (_row == 0) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "[%u/8] %s",
                   _tgt_slot, _targets[_tgt_slot]);
        for (uint8_t i = 0; i < 8; ++i)
            draw_textf(8, info_y + 12 + i * 10, T_DIM, T_BG, FONT_SM,
                       "  %c %s", i == _tgt_slot ? '>' : ' ', _targets[i]);

    } else if (_row == 1) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
                   "%u reps (tier %u/%u)",
                   REPS_TIERS[_reps_tier], _reps_tier + 1, N_REPS_TIERS);
        for (uint8_t i = 0; i < N_REPS_TIERS; ++i)
            draw_textf(8, info_y + 12 + i * 10, T_DIM, T_BG, FONT_SM,
                       "  %c %u", i == _reps_tier ? '>' : ' ', REPS_TIERS[i]);

    } else {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
                   "%u / %u  -> loot/fuzz.csv",
                   _done, REPS_TIERS[_reps_tier]);
    }

    draw_textf(8, SCR_H - 28, _running ? T_WARN : T_DIM, T_BG, FONT_SM,
               "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] RUN  [B] STOP  POT=row",
              T_DIM, T_BG, FONT_SM);
}

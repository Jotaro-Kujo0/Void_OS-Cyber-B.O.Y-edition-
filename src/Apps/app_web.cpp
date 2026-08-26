// app_web.cpp — small HTTP web scraper.
//
// Sub-modes:
//   0. URL   — pick / edit the saved URL via hal_storage.
//   1. GET   — fetch and save HTML.
//   2. PARSE — extract <title>, <h1>, count <a>, count <form>.
//   3. LINKS — first 8 anchor links from the last saved HTML.
//
// Background pump drives the GET flow; once a fetch starts, the pump
// keeps reading bytes into a streaming buffer until the response ends,
// then closes.
//
// Pi 5 path: shell-out to `curl` (Pi OS default) — simplest.
// ESP32 path: `WiFiClient` + HTTP/1.0 + manual header parsing.

#include "app_web.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_storage.h"
#include "os/scheduler.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <Arduino.h>

#ifdef VOIDOS_RPI5
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

// ── UI + state ─────────────────────────────────────────────────────────

static const char *LABELS[] = { "URL", "GET", "PARSE", "LINKS" };
static const uint8_t N_ROWS = sizeof(LABELS)/sizeof(LABELS[0]);
static uint8_t  _row = 0;
static char     _url[80] = "https://example.com";
static char     _status[24] = "READY";

static int8_t _pump_id = -1;

// Last-fetch summary — used by PARSE / LINKS views.
#define WEB_PEEK_LEN   4096
static char _peek[WEB_PEEK_LEN];
static uint32_t _peek_n = 0;
static char _last_path[64] = {};
static char _last_title[64] = {};
static char _last_h1[64] = {};
static uint16_t _n_links = 0, _n_forms = 0;
static char _links[8][80];

// PERF: operators care about latency. Captured at GET tail.
static uint32_t _last_ms = 0;

// ── URL storage ─────────────────────────────────────────────────────────

static void load_url() {
    char buf[80];
    if (hal_storage_get_str("web_url", buf, sizeof(buf))) {
        std::snprintf(_url, sizeof(_url), "%s", buf);
    } else {
        hal_storage_set_str("web_url", _url);
    }
}
static void save_url() { hal_storage_set_str("web_url", _url); }

// ── HTML peek: peek the file's first chunk into _peek ──────────────────

#ifdef VOIDOS_RPI5
#include <fstream>
static bool peek_file(const char *path) {
    std::ifstream f(path);
    if (!f) return false;
    _peek_n = 0;
    f.read(_peek, sizeof(_peek) - 1);
    _peek_n = (uint32_t)f.gcount();
    _peek[_peek_n] = 0;
    return _peek_n > 0;
}
#else
#include <SD.h>
#include <vector>
static bool peek_file(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    _peek_n = 0;
    int n = f.read((uint8_t*)_peek, sizeof(_peek) - 1);
    f.close();
    if (n <= 0) return false;
    _peek_n = (uint32_t)n;
    _peek[_peek_n] = 0;
    return true;
}
#endif

// ── Tiny HTML parser (string finds for `<title>`, `<h1>`, `<a href`) ──
//
// ponytail: scrapers rarely need a DOM. Sliding-window substring matches
// in the peek buffer pull out data faster than a proper parser, and
// 95% of the time that's all the operator wants.

static void extract_meta() {
    _last_title[0] = _last_h1[0] = 0;
    _n_links = _n_forms = 0;
    for (uint8_t i = 0; i < 8; ++i) _links[i][0] = 0;

    const char *p = _peek;
    while (p && *p) {
        const char *lt = std::strchr(p, '<');
        if (!lt) break;
        const char *gt = std::strchr(lt, '>');
        if (!gt) break;

        // Pull the tag name.
        char tag[16] = {};
        int i = 1;
        while (i < (int)sizeof(tag) - 1 && lt[i] && lt[i] != ' ' && lt[i] != '/' && lt[i] != '>')
            tag[i-1] = std::tolower((unsigned char)lt[i]), ++i;

        // <title>...</title>
        if (std::strcmp(tag, "title") == 0) {
            const char *body = gt + 1;
            const char *end  = std::strstr(body, "</title");
            if (end) std::snprintf(_last_title, sizeof(_last_title), "%.*s", (int)(end - body), body);
            else     std::snprintf(_last_title, sizeof(_last_title), "%.*s", (int)strlen(body), body);
        }
        // <h1>...</h1>
        else if (std::strcmp(tag, "h1") == 0) {
            const char *body = gt + 1;
            const char *end  = std::strstr(body, "</h1");
            if (end) std::snprintf(_last_h1, sizeof(_last_h1), "%.*s", (int)(end - body), body);
        }
        // <a href="...">
        else if (std::strcmp(tag, "a") == 0) {
            // Look for href="..." in the tag attrs.
            const char *href = std::strstr(lt, "href=\"");
            if (href && href < gt) {
                href += 6;
                const char *e = std::strchr(href, '"');
                if (e && _n_links < 8) {
                    std::snprintf(_links[_n_links], 80, "%.*s", (int)(e - href), href);
                    ++_n_links;
                }
            }
        }
        // <form
        else if (std::strcmp(tag, "form") == 0) ++_n_forms;

        p = gt + 1;
    }
}

// ── GET pump ───────────────────────────────────────────────────────────
//
// The pump is shared between Pi 5 (shell-out, runs once per frame to
// poll completion) and ESP32 (active TCP read in chunks). Both write
// to /var/lib/void-os/loot/web_<UTC>.html; the path is captured in
// `_last_path` so PARSE/LINKS can re-read.

static bool     _fetching = false;
#ifdef VOIDOS_RPI5
static pid_t    _curl_pid = -1;
#else
#include <WiFi.h>
static WiFiClient _web_client;
static char     _web_host[80]; static uint16_t _web_port = 80; static char _web_path[160];
static uint32_t _fetch_started_ms = 0;
#endif

#ifdef VOIDOS_RPI5
static void fetch_start() {
    if (_fetching) return;
    char cmd[256];
    std::snprintf(_last_path, sizeof(_last_path),
                  "/var/lib/void-os/loot/web_%lu.html",
                  (unsigned long)millis());
    std::snprintf(cmd, sizeof(cmd),
                  "curl -sL --max-time 10 '%s' -o '%s' && echo OK || echo FAIL",
                  _url, _last_path);
    _curl_pid = fork();
    if (_curl_pid == 0) { std::system(cmd); _exit(0); }
    _fetching = true;
    _last_ms = millis();
    std::snprintf(_status, sizeof(_status), "fetching...");
}

static void fetch_pump() {
    if (!_fetching) return;
    // simplest: poll `waitpid(WNOHANG)` once per frame.
    int st = 0;
    pid_t r = ::waitpid(_curl_pid, &st, WNOHANG);
    if (r == 0) return;            // still running
    _fetching = false;
    if (r == _curl_pid) {
        _last_ms = millis() - _last_ms;
        bool ok = peek_file(_last_path);
        extract_meta();
        std::snprintf(_status, sizeof(_status), "ok %lums %uB",
                     (unsigned long)_last_ms, _peek_n);
        (void)ok;
    }
}
#else
static void fetch_start() {
    if (_fetching) return;
    // Parse URL.
    char host[80], path[160]; uint16_t port = 80;
    if (std::strncmp(_url, "https://", 8) == 0) { std::snprintf(_url, sizeof(_url), "%s", _url + 8); port = 443; }
    if (std::strncmp(_url, "http://",  7) == 0) { std::snprintf(_url, sizeof(_url), "%s", _url + 7); }
    const char *slash = std::strchr(_url, '/');
    if (slash) { std::snprintf(path, sizeof(path), "%s", slash); std::snprintf(host, sizeof(host), "%.*s", (int)(slash - _url), _url); }
    else       { std::snprintf(host, sizeof(host), "%s", _url); path[0] = '/'; path[1] = 0; }
    std::snprintf(_web_host, sizeof(_web_host), "%s", host);
    std::snprintf(_web_path, sizeof(_web_path), "%s", path);
    _web_port = port;
    _fetch_started_ms = millis();
    _fetching = _web_client.connect(host, port);
    std::snprintf(_status, sizeof(_status), _fetching ? "open ok" : "connect fail");
}

static void fetch_pump() {
    if (!_fetching) return;
    if (!_web_client.connected()) {
        _fetching = false;
        _last_ms = millis() - _fetch_started_ms;
        std::snprintf(_status, sizeof(_status), "closed %lums", (unsigned long)_last_ms);
        return;
    }
    static bool sent_request = false;
    if (!sent_request) {
        char req[300];
        std::snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: void-os\r\n\r\n",
                      _web_path, _web_host);
        _web_client.write((const uint8_t*)req, std::strlen(req));
        sent_request = true;
        return;
    }
    // Drain.
    static uint32_t total = 0;
    while (_web_client.available()) {
        uint8_t buf[256]; int n = _web_client.read(buf, sizeof(buf));
        if (n <= 0) break;
        if (total < sizeof(_peek) - 1) {
            int copy = (int)(sizeof(_peek) - 1 - total);
            if (copy > n) copy = n;
            std::memcpy(_peek + total, buf, copy);
            total += copy;
        }
    }
    if (!_web_client.connected()) {
        _peek[total] = 0; _peek_n = total; total = 0;
        sent_request = false;
        _fetching = false;
        _last_ms = millis() - _fetch_started_ms;
        extract_meta();
        std::snprintf(_status, sizeof(_status), "ok %lums %uB", (unsigned long)_last_ms, _peek_n);
    }
}
#endif

// ── Lifecycle ──────────────────────────────────────────────────────────

void app_web_init() {
    load_url();
    if (_pump_id < 0) _pump_id = sched_add("web_pump", fetch_pump, 50, 5);
}
void app_web_tick() {}
void app_web_suspend() {}

void app_web_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        std::snprintf(_status, sizeof(_status), "READY");
        return;
    }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    if (e.type == EVT_BTN_A_DOWN) {
        switch (_row) {
            case 0: save_url(); std::snprintf(_status, sizeof(_status), "url saved"); break;
            case 1: fetch_start();  break;
            case 2:
                if (_last_path[0]) {
                    peek_file(_last_path);
                    extract_meta();
                    std::snprintf(_status, sizeof(_status), "parsed");
                }
                break;
            case 3:
                if (!_links[0][0]) {
                    if (_last_path[0] && peek_file(_last_path)) extract_meta();
                }
                break;
        }
        return;
    }
}

// ── Draw ───────────────────────────────────────────────────────────────

void app_web_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "WEB / SCRAPE", T_FG, T_PANEL, FONT_SM);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    if (_row == 0) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "URL: %.40s", _url);
    } else if (_row == 1) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "FETCH %s", _url);
        draw_textf(8, info_y + 14, T_DIM, T_BG, FONT_SM, "-> %s", _last_path);
    } else if (_row == 2) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "title: %.40s", _last_title);
        draw_textf(8, info_y + 14, T_DIM, T_BG, FONT_SM, "h1: %.40s", _last_h1);
        draw_textf(8, info_y + 28, T_DIM, T_BG, FONT_SM, "links:%u forms:%u", _n_links, _n_forms);
    } else {
        for (uint8_t i = 0; i < _n_links && i < 8; ++i)
            draw_textf(8, info_y + (int)i * 12, T_DIM, T_BG, FONT_SM, "%.70s", _links[i]);
    }
    draw_textf(8, SCR_H - 28, _fetching ? T_WARN : T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] ACT  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}


// hal_serial_term.cpp — USB-CDC host command protocol.
//
// ponytail: a tiny REPL is enough. Each line carries `cmd args...` and
// is dispatched to one of N callbacks. New commands register at boot
// via the table below.
//
// ─────────────────────────────────────────────────────────────────────
//  Protocol summary
// ─────────────────────────────────────────────────────────────────────
//
//   send  : "<command> [arg0 arg1 ...]\n"
//   recv  : "<command>: <result>\n"   (always one line; multi-line is
//                                       rare and only used by 'help')
//
//   The host's terminal emulator adds LF after every response. LF-only
//   is technically accepted, but two-byte CRLF is the safe default.
//
// ─────────────────────────────────────────────────────────────────────
//  Commands that already exist in the firmware
// ─────────────────────────────────────────────────────────────────────
//
//   help                       this list
//   status                     same as `app_stat`'s first row
//   scan arp                   same as app_scan row 0
//   scan ports <ip>            same as app_scan row 1
//   scan hints                 same as app_scan row 2
//   scan bodies                same as app_body arm + count
//   rogue on|off               toggle evil-AP
//   rogue captive on|off       toggle captive portal
//   rogue dns on|off           toggle DNS spoof
//   sniff urls on|off          toggle URL log
//   sniff cookies              dump cookies.csv
//   mitm on|off                toggle ip_forward
//   fuzz run [tgt_idx]         start fuzzer
//   fuzz stop                  stop fuzzer
//   web get <url>              app_web GET
//   web peek                   app_web PARSE
//   ls                         list hal_storage keys
//   get <key>                  print one hal_storage value
//   set <key> <str>            store one hal_storage value
//   reset                      factory reset (NVS clear)
//
// ─────────────────────────────────────────────────────────────────────
//  Wiring note for app authors
// ─────────────────────────────────────────────────────────────────────
//
//   The REPL routes calls directly to the existing app / HAL/
//   hal_storage surfaces rather than re-implementing the logic. That
//   keeps the REPL idempotent with the device's on-device app UX. If
//   you add a row to app_X, please also bind a `cmd` here so the
//   operator's laptop can drive it without a UI gesture.
//
// =====================================================================

#include "hal_serial_term.h"
#include "hal/hal_storage.h"
#include "hal/hal_metrics.h"
#include "hal/hal_buzzer.h"
#include "os/scheduler.h"
#include "config.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

#ifndef VOIDOS_RPI5
extern HardwareSerial Serial;
#else
#include <unistd.h>
#endif

#define LINE_BUF_MAX 128
static char _line[LINE_BUF_MAX];
static uint8_t _line_n = 0;
static int8_t _pump_id = -1;

static void reply(const char *line) {
#ifdef VOIDOS_RPI5
    std::fprintf(stdout, "%s\n", line);
    std::fflush(stdout);
#else
    Serial.println(line);
#endif
}

void hal_serial_term_respond(const char *line) { reply(line); }

// ── Dispatcher ────────────────────────────────────────────────────────

static void cmd_help() {
    reply("help status scan rog sniff mitm fuzz web ls get set reset");
}

static void cmd_status() {
    SystemUsage s = hal_metrics_snapshot();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "cpu:%u%% mem:%u%% sd:%u%% rx:%ukB", s.cpu_pct, s.mem_pct, s.sdc_pct, s.net_rx_total_kb);
    reply(buf);
}

static void cmd_scan_arp(void) {
    reply("scan arp: STUB; run app_scan");
}
static void cmd_scan_ports(const char *args) {
    (void)args; reply("scan ports: STUB; run app_scan target=...");
}
static void cmd_scan_hints(void)  { reply("scan hints: see app_scan"); }
static void cmd_scan_bodies(void) {
    reply("scan bodies: see app_body");
}

static void cmd_rogue(const char *args) {
    if (!args || !*args) { reply("usage: rogue on|off"); return; }
    if (std::strcmp(args, "on") == 0)  reply("rogue on: enabled");
    if (std::strcmp(args, "off") == 0) reply("rogue off: disabled");
}
static void cmd_sniff(const char *args) {
    (void)args; reply("sniff: see app_sniff");
}
static void cmd_mitm(const char *args) {
    if (!args || !*args) { reply("usage: mitm on|off"); return; }
    reply(args);
}

static void cmd_fuzz(const char *args) {
    (void)args; reply("fuzz: see app_fuzz");
}

static void cmd_web(const char *args) {
    (void)args; reply("web: see app_web");
}

static void cmd_ls(void) {
    // Real impl calls hal_storage_iterate; skeleton returns placeholder.
    reply("ls: NVS keys would enumerate here");
}

static void cmd_get(const char *args) {
    if (!args || !*args) { reply("usage: get <key>"); return; }
    char buf[96];
    if      (hal_storage_get_str(args, buf, sizeof(buf))) { reply(buf); }
    else if (hal_storage_get_u8(args, 0) != 0)             { reply("u8: present"); }
    else                                                    { reply("(missing)"); }
}

static void cmd_set(const char *args) {
    if (!args || !*args) { reply("usage: set <key> <value>"); return; }
    char buf[96];
    if (std::sscanf(args, "%*s %95[^\n]", buf) != 1) { reply("usage: set <key> <value>"); return; }
    hal_storage_set_str(args, buf);
    reply("ok");
}

static void cmd_reset(void) {
    reply("reset: full NVS wipe — same as app_sys C-down");
}

static void dispatch(char *line) {
    char *sp = std::strchr(line, ' ');
    if (sp) { *sp = 0; ++sp; }  // split into cmd + args (args may be empty)
    const char *args = sp ? sp : "";

    if      (!std::strcmp(line, "help"))       cmd_help();
    else if (!std::strcmp(line, "status"))     cmd_status();
    else if (!std::strcmp(line, "scan")) {
        if      (!std::strcmp(args, "arp"))    cmd_scan_arp();
        else if (!std::strcmp(args, "ports"))  cmd_scan_ports(args);
        else if (!std::strcmp(args, "hints"))  cmd_scan_hints();
        else if (!std::strcmp(args, "bodies")) cmd_scan_bodies();
        else reply("usage: scan arp|ports|hints|bodies");
    }
    else if (!std::strcmp(line, "rogue"))      cmd_rogue(args);
    else if (!std::strcmp(line, "sniff"))      cmd_sniff(args);
    else if (!std::strcmp(line, "mitm"))       cmd_mitm(args);
    else if (!std::strcmp(line, "fuzz"))       cmd_fuzz(args);
    else if (!std::strcmp(line, "web"))        cmd_web(args);
    else if (!std::strcmp(line, "ls"))         cmd_ls();
    else if (!std::strcmp(line, "get"))        cmd_get(args);
    else if (!std::strcmp(line, "set"))        cmd_set(args);
    else if (!std::strcmp(line, "reset"))      cmd_reset();
    else reply("unknown command");
}

// ── Line pump ─────────────────────────────────────────────────────────

static void pump(void) {
#ifdef VOIDOS_RPI5
    // Pi 5: read a single byte from stdin if it's a TTY. The boot
    // environment is normally a serial terminal already.
    char c = 0;
    int n = ::read(0, &c, 1);
    if (n <= 0) return;
#else
    if (!Serial.available()) return;
    char c = (char)Serial.read();
#endif
    if (c == '\r' || c == '\n') {
        if (_line_n) {
            _line[_line_n] = 0;
            dispatch(_line);
            _line_n = 0;
        }
    } else if (c == 0x08 || c == 0x7F) {
        if (_line_n) --_line_n;
    } else if (_line_n < LINE_BUF_MAX - 1) {
        _line[_line_n++] = c;
    }
}

void hal_serial_term_init() {
    if (_pump_id < 0) _pump_id = sched_add("serial_term", pump, 0, 4);
}

void hal_serial_term_tick() { /* pump does work */ }

bool hal_serial_term_run_script(const char *path) {
    if (!path) return false;
#ifdef VOIDOS_RPI5
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char buf[128];
    while (fgets(buf, sizeof(buf), f)) {
        char *eol = std::strpbrk(buf, "\r\n");
        if (eol) *eol = 0;
        dispatch(buf);
    }
    fclose(f);
#else
    // ESP32: SD.open + read + dispatch
    // Skeleton until hal_sdcard is wired.
#endif
    return true;
}

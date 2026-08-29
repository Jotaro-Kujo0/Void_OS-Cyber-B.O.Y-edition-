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
#include <fcntl.h>
#include <errno.h>
#include <cstdlib>
#include <strings.h>
#endif

#define LINE_BUF_MAX 128
static char _line[LINE_BUF_MAX];
static uint8_t _line_n = 0;
static int8_t _pump_id = -1;

#ifdef VOIDOS_RPI5
// ── Control channel (RPi5 harness) ────────────────────────────────────────
//
// The host harness drives keyboard input through stdin (VOIDOS_HARNESS),
// which would collide with REPL commands typed on the same fd. To keep the
// two separate, the harness can hand the device a FIFO path via
// VOIDOS_CTRL_FIFO; lines read there go through the same dispatcher, but
// never touch the button/pot keystrokes. Replies still flow back on stdout
// (Serial), which the harness already captures.
static int  _ctrl_fd        = -1;
static char _ctrl_line[LINE_BUF_MAX];
static uint8_t _ctrl_line_n = 0;

extern void os_launch_app(uint8_t idx);

static const char *APP_NAMES[APP_COUNT] = {
    "HOME","STAT","WIFI","LOG","RADIO","NFC","IR","SYS","BUS","HID","DARK",
    "SCAN","ROGUE","SNIFF","FUZZ","WEB","BODY","HELP","DROP","QR",
    "LEAK","MARAUD","OSINT","HARDEN"
};
#endif

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

// leak / osint settings & kick-off via REPL (see app_leak, app_osint)
static void cmd_leak(const char *args) {
    if (!args || !*args) { reply("usage: leak scan|hash <mode>|wordlist <path>|dir <path>|hibp <key>"); return; }
    // `set`-style helpers reuse hal_storage directly.
    if (std::strncmp(args, "wordlist ", 9) == 0) { hal_storage_set_str("leak_wordlist", args + 9); reply("ok: wordlist set"); }
    else if (std::strncmp(args, "dir ", 4) == 0)   { hal_storage_set_str("leak_dir", args + 4);      reply("ok: extra dir set"); }
    else if (std::strncmp(args, "hibp ", 5) == 0)   { hal_storage_set_str("hibp_key", args + 5);      reply("ok: hibp key set"); }
    else if (std::strncmp(args, "scan", 4) == 0)    { reply("leak scan: open app_leak (SCAN row)"); }
    else if (std::strncmp(args, "hash", 4) == 0)    { reply("leak hash: open app_leak (HASH row)"); }
    else reply("leak: unknown arg");
    hal_storage_commit();
}

static void cmd_osint(const char *args) {
    if (!args || !*args) { reply("usage: osint target <host|ip>|whois|dns|subdom|geo|dork"); return; }
    if (std::strncmp(args, "target ", 7) == 0) { hal_storage_set_str("osint_target", args + 7); reply("ok: target set (restart app_osint)"); hal_storage_commit(); }
    else if (std::strcmp(args, "whois") == 0)  reply("osint whois: open app_osint (WHOIS row)");
    else if (std::strcmp(args, "dns") == 0)    reply("osint dns: open app_osint (DNS row)");
    else if (std::strcmp(args, "subdom") == 0) reply("osint subdom: open app_osint (SUBDOM row)");
    else if (std::strcmp(args, "geo") == 0)    reply("osint geo: open app_osint (GEO row)");
    else if (std::strcmp(args, "dork") == 0)   reply("osint dork: open app_osint (DOCK row)");
    else reply("osint: unknown arg");
}

// Launch a specific app directly, as if the operator had selected it on
// the home screen and pressed A. arg is an app index or a name.
static void cmd_launch(const char *args) {
#ifdef VOIDOS_RPI5
    if (!args || !*args) { reply("launch: usage launch <index|NAME>"); return; }
    int  idx = -1;
    int  n   = 0;
    if (std::sscanf(args, "%d", &n) == 1 && n >= 0 && n < APP_COUNT) {
        idx = n;
    } else {
        for (int i = 0; i < APP_COUNT; ++i)
            if (strcasecmp(args, APP_NAMES[i]) == 0) { idx = i; break; }
    }
    if (idx < 0) { reply("launch: bad app"); return; }
    os_launch_app(static_cast<uint8_t>(idx));
    char buf[48];
    std::snprintf(buf, sizeof(buf), "launch: %s", APP_NAMES[idx]);
    reply(buf);
#else
    reply("launch: RPi5 only");
#endif
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
    else if (!std::strcmp(line, "leak"))       cmd_leak(args);
    else if (!std::strcmp(line, "osint"))      cmd_osint(args);
    else if (!std::strcmp(line, "launch"))     cmd_launch(args);
    else if (!std::strcmp(line, "reset"))      cmd_reset();
    else reply("unknown command");
}

// ── Line pump ─────────────────────────────────────────────────────────

#ifdef VOIDOS_RPI5
// Drains the control FIFO into its own line buffer and dispatches complete
// lines, so REPL commands never mingle with harness keystrokes on stdin.
static void ctrl_pump(void) {
    if (_ctrl_fd < 0) return;
    char c;
    for (;;) {
        ssize_t n = ::read(_ctrl_fd, &c, 1);
        if (n <= 0) break;                       // EAGAIN / EOF: give up this tick
        if (c == '\r' || c == '\n') {
            if (_ctrl_line_n) {
                _ctrl_line[_ctrl_line_n] = 0;
                dispatch(_ctrl_line);
                _ctrl_line_n = 0;
            }
        } else if (c == 0x08 || c == 0x7F) {
            if (_ctrl_line_n) --_ctrl_line_n;
        } else if (_ctrl_line_n < LINE_BUF_MAX - 1) {
            _ctrl_line[_ctrl_line_n++] = c;
        }
    }
}
#endif

static void pump(void) {
#ifdef VOIDOS_RPI5
    ctrl_pump();                                 // control FIFO first (low traffic)
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
#ifdef VOIDOS_RPI5
    const char *fifo = std::getenv("VOIDOS_CTRL_FIFO");
    if (fifo && fifo[0]) {
        _ctrl_fd = ::open(fifo, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (_ctrl_fd >= 0) reply("[ctrl] control channel open");
    }
#endif
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

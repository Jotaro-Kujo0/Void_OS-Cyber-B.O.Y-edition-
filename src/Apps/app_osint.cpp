// app_osint.cpp — OSINT gatherer via whatever whois/dig/curl exist.
// Bare box keeps DOCK; others report "tool missing".

#include "app_osint.h"
#include "wordlists.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_storage.h"
#include "hal/hal_loot.h"
#include "os/scheduler.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#ifdef VOIDOS_RPI5
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fstream>
#endif

// ── MENU ──────────────────────────────────────────────────────────────

static const char *LABELS[] = { "WHOIS", "DNS", "SUBDOM", "GEO", "DOCK" };
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);
static uint8_t _row = 0;
static char    _status[72] = "READY";

// last-run output summary (up to 6 lines)
#ifdef VOIDOS_RPI5
static char _out[6][80];
static uint8_t _out_n = 0;
static char _target[64] = "example.com";
static bool _running = false;
static int _bg_pid = -1;   // background tool PID (poll via waitpid)
#endif

static void push_out(const char *s) {
#ifdef VOIDOS_RPI5
    if (_out_n >= 6) {
        for (uint8_t i = 1; i < 6; ++i) std::snprintf(_out[i-1], 60, "%s", _out[i]);
        _out_n = 5;
    }
    std::snprintf(_out[_out_n++], 60, "%.59s", s);
#endif
}

#ifdef VOIDOS_RPI5
static bool tool_present(const char *name) {
    char cmd[80];
    std::snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return std::system(cmd) == 0;
}

// dump command output to loot file, return first N lines on screen
static void run_capture(const char *cmd, const char *fname, int screen_lines) {
    mkdir("/var/lib/void-os/loot", 0755);
    char out[128];
    std::snprintf(out, sizeof(out), "/var/lib/void-os/loot/%s", fname);
    char full[400];
    std::snprintf(full, sizeof(full), "%s > %s 2>&1", cmd, out);
    _bg_pid = fork();
    if (_bg_pid == 0) { std::system(full); _exit(0); }
    _running = true;

    char ncmd[120];
    std::snprintf(ncmd, sizeof(ncmd), "wc -l < %s 2>/dev/null", out);
    FILE *p = ::popen(ncmd, "r");
    int lines = 0; if (p) { std::fscanf(p, "%d", &lines); ::pclose(p); }
    (void)screen_lines;
    std::snprintf(_status, sizeof(_status), "%d line(s) -> loot/%s", lines, fname);
    std::snprintf(_out[0], 60, "cmd: %s", cmd); _out_n = 1;
}

static void do_whois() {
    if (!tool_present("whois")) { std::snprintf(_status,sizeof(_status),"TOOL MISSING: whois"); return; }
    char c[120];
    std::snprintf(c, sizeof(c), "whois '%s' 2>/dev/null | head -120", _target);
    run_capture(c, "whois.txt", 8);
}
static void do_dns() {
    if (!tool_present("dig")) { std::snprintf(_status, sizeof(_status), "TOOL MISSING: dig"); return; }
    char c[180];
    std::snprintf(c, sizeof(c),
        "{ echo '== A =='; dig +short A '%s'; echo; "
        " echo '== AAAA =='; dig +short AAAA '%s'; echo; "
        " echo '== MX =='; dig +short MX '%s'; echo; "
        " echo '== NS =='; dig +short NS '%s'; echo; "
        " echo '== TXT =='; dig +short TXT '%s'; } 2>/dev/null | head -120",
        _target, _target, _target, _target, _target);
    run_capture(c, "dns.txt", 10);
}
static void do_subdom() {
    // bundled common-subdomain list; use dig for each, log hits.
    if (!tool_present("dig")) { std::snprintf(_status,sizeof(_status),"TOOL MISSING: dig"); return; }
    // Write the bundled list to loot so the operator can inspect it and,
    // if needed, drop their own longer list and it keeps working.
    mkdir("/var/lib/void-os/loot", 0755);
    {
        std::ofstream wf("/var/lib/void-os/loot/subdom.list", std::ios::trunc);
        if (wf) for (uint32_t i = 0; i < OSINT_SUBDOM_COUNT; ++i) wf << OSINT_SUBDOMAINS[i] << "\n";
    }
    char cstr[400];
    std::snprintf(cstr, sizeof(cstr),
        "while read s; do "
        "[ -z \"$s\" ] && continue; "
        "if dig +short A \"$s.%s\" 2>/dev/null | grep -qv '^$'; then "
        "echo \"$s.%s\"; fi; done < /var/lib/void-os/loot/subdom.list 2>/dev/null "
        "| head -60", _target, _target);
    run_capture(cstr, "subdom.txt", 10);
}
static void do_geo() {
    if (_target[0] && tool_present("curl")) {
        char c[160];
        std::snprintf(c, sizeof(c),
            "curl -s --max-time 8 'https://ipinfo.io/%s' 2>/dev/null "
            "| grep -E '\\\"(ip|city|region|country|org)\\\"'", _target);
        run_capture(c, "geo_ipinfo.txt", 6);
    } else if (tool_present("whois")) {
        char c[120];
        std::snprintf(c, sizeof(c),
            "whois '%s' 2>/dev/null | grep -iE '^(netname|country|org-name)' "
            "| head -8", _target);
        run_capture(c, "geo_whois.txt", 6);
    } else {
        std::snprintf(_status,sizeof(_status),"TOOL MISSING: curl/whois");
    }
}
static void do_dork() {
    // pure URL building — always works; write to loot/dork.txt + screen
    mkdir("/var/lib/void-os/loot", 0755);
    std::ofstream f("/var/lib/void-os/loot/dork.txt", std::ios::trunc);
    if (f) {
        char esc[64]; // url-escape basic spaces
        std::snprintf(esc, sizeof(esc), "%s", _target);
        f << "https://www.google.com/search?q=\"" << esc << "\"\n"
          << "https://www.bing.com/search?q=\"" << esc << "\"\n"
          << "https://www.shodan.io/search?query=\"" << esc << "\"\n";
    }
    std::snprintf(_status, sizeof(_status), "3 dork URLs -> loot/dork.txt");
    std::snprintf(_out[0], 60, "google: %s", _target);
    std::snprintf(_out[1], 60, "bing / shodan built too");
    _out_n = 2;
}

// poll a background tool; when it exits, update status
static void osint_pump() {
    if (!_running) return;
    if (_bg_pid <= 0) return;
    int st; pid_t r = ::waitpid(_bg_pid, &st, WNOHANG);
    if (r == _bg_pid) { _running = false; _bg_pid = -1; }
}
#endif

// ── PERSISTENCE ───────────────────────────────────────────────────────

static void load_target() {
#ifdef VOIDOS_RPI5
    if (!hal_storage_get_str("osint_target", _target, sizeof(_target)))
        std::snprintf(_target, sizeof(_target), "example.com");
    hal_storage_set_str("osint_target", _target);
#endif
}

// ── LIFECYCLE ─────────────────────────────────────────────────────────

void app_osint_init() {
    load_target();
    _row = 0;
#ifdef VOIDOS_RPI5
    _out_n = 0; _running = false; _bg_pid = -1;
#endif
    std::snprintf(_status, sizeof(_status), "READY");
}
void app_osint_tick() {
#ifdef VOIDOS_RPI5
    osint_pump();
#endif
}
void app_osint_suspend() {
#ifdef VOIDOS_RPI5
    _running = false; _bg_pid = -1;
    std::snprintf(_status, sizeof(_status), "READY");
#endif
}

// ── EVENTS ────────────────────────────────────────────────────────────

void app_osint_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) { std::snprintf(_status, sizeof(_status), "READY"); return; }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        if (_row == 4) { if (_out_n) return; }
        return;
    }
    if (e.type != EVT_BTN_A_DOWN) return;
#ifdef VOIDOS_RPI5
    switch (_row) {
        case 0: do_whois(); break;
        case 1: do_dns();   break;
        case 2: do_subdom(); break;
        case 3: do_geo();   break;
        case 4: do_dork();  break;
    }
#else
    std::snprintf(_status, sizeof(_status), "OSINT: Linux only");
#endif
}

// ── DRAW ──────────────────────────────────────────────────────────────

void app_osint_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "OSINT", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W-110, 8, T_DIM, T_PANEL, FONT_SM, "t:%s", _target);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
#ifdef VOIDOS_RPI5
    for (uint8_t i = 0; i < _out_n; ++i)
        draw_textf(8, info_y + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", _out[i]);
#else
    draw_textf(8, info_y, T_WARN, T_BG, FONT_SM, "LINUX ONLY");
#endif

    draw_textf(8, SCR_H - 40, T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] RUN  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
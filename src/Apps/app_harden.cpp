// app_harden.cpp — self-hardening dashboard; shells out, degrades cleanly.

#include "app_harden.h"
#include "UI/draw.h"
#include "UI/theme.h"
#include "hal/hal_storage.h"
#include "os/scheduler.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#ifdef VOIDOS_RPI5
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

// ── MENU ──────────────────────────────────────────────────────────────

static const char *LABELS[] = { "UFW", "FAIL2BAN", "ROOTKIT", "LYNIS", "UPDATE" };
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);
static uint8_t _row = 0;
static char    _status[72] = "READY";
static char    _out[6][80];
static uint8_t _out_n = 0;

#ifdef VOIDOS_RPI5
static int  _bg_pid = -1;
static bool _running = false;
#endif

#ifdef VOIDOS_RPI5
static bool tool_present(const char *n) {
    char c[80]; std::snprintf(c, sizeof(c), "command -v %s >/dev/null 2>&1", n);
    return std::system(c) == 0;
}

// run a (possibly background) command; dump output to loot/<file>
static void run_capture(const char *cmd, const char *file, bool background) {
    mkdir("/var/lib/void-os/loot", 0755);
    char out[128]; std::snprintf(out, sizeof(out), "/var/lib/void-os/loot/%s", file);
    char full[400]; std::snprintf(full, sizeof(full), "%s > %s 2>&1", cmd, out);
    if (background) {
        _bg_pid = fork();
        if (_bg_pid == 0) { std::system(full); _exit(0); }
        _running = true;
        std::snprintf(_status, sizeof(_status), "running... -> loot/%s", file);
    } else {
        std::system(full);
        char ncmd[120]; std::snprintf(ncmd, sizeof(ncmd), "wc -l < %s 2>/dev/null", out);
        FILE *p = ::popen(ncmd, "r");
        int lines = 0; if (p) { std::fscanf(p, "%d", &lines); ::pclose(p); }
        std::snprintf(_status, sizeof(_status), "%d line(s) -> loot/%s", lines, file);
    }
    std::snprintf(_out[0], 52, "cmd: %s", file); _out_n = 1;
}

static void status_of(const char *svc) {
    char c[200];
    std::snprintf(c, sizeof(c),
        "if systemctl is-active --quiet %s; then echo '%s: ACTIVE'; "
        "elif systemctl is-enabled --quiet %s 2>/dev/null; then "
        "echo '%s: enabled-stopped'; else echo '%s: not-installed'; fi",
        svc, svc, svc, svc, svc);
    char res[80] = "";
    FILE *p = ::popen(c, "r");
    if (p) { std::fgets(res, sizeof(res), p); ::pclose(p); }
    if (res[0]) { std::snprintf(_status, sizeof(_status), "%.42s", res); }
    else        { std::snprintf(_status, sizeof(_status), "%s: unknown", svc); }
}
static void do_ufw() {
    if (!tool_present("ufw")) { std::snprintf(_status, sizeof(_status), "UFW not installed"); return; }
    char c[200];
    std::snprintf(c, sizeof(c),
        "ufw status 2>/dev/null | head -1");          // "Status: active|inactive"
    char res[40] = "";
    FILE *p = ::popen(c, "r");
    if (p) { std::fgets(res, sizeof(res), p); ::pclose(p); }
    if (std::strstr(res, "active")) {
        std::system("ufw disable >/dev/null 2>&1; echo 'UFW: disabled'");
        std::snprintf(_status, sizeof(_status), "UFW: DISABLED");
    } else {
        std::system("ufw --force enable >/dev/null 2>&1; echo 'UFW: enabled'");
        std::snprintf(_status, sizeof(_status), "UFW: ENABLED");
    }
}
static void do_fail2ban() { status_of("fail2ban"); }
static void do_rootkit() {
    const char *t = tool_present("rkhunter") ? "rkhunter" :
                    tool_present("chkrootkit") ? "chkrootkit" : "";
    if (!t[0]) { std::snprintf(_status, sizeof(_status), "TOOL MISSING: rkhunter/chkrootkit"); return; }
    if (std::strcmp(t, "rkhunter") == 0)
        run_capture("rkhunter --check --skips-banner 2>/dev/null | tail -40",
                    "rkhunter.txt", true);
    else
        run_capture("chkrootkit 2>/dev/null | tail -40", "chkrootkit.txt", true);
    std::snprintf(_status, sizeof(_status), "rootkit scan started -> loot/*.txt");
}
static void do_lynis() {
    if (!tool_present("lynis")) { std::snprintf(_status, sizeof(_status), "TOOL MISSING: lynis"); return; }
    run_capture("lynis audit system --no-colors --quiet 2>/dev/null | tail -50",
                "lynis.txt", true);
    std::snprintf(_status, sizeof(_status), "lynis audit started -> loot/lynis.txt");
}
static void do_update() {
    run_capture("(apt-get update && apt-get -y upgrade) 2>&1 | tail -30",
                "apt_upgrade.txt", true);
    std::snprintf(_status, sizeof(_status), "apt update/upgrade started -> loot/apt_upgrade.txt");
}

// poll background scans; update status when done
static void harden_pump() {
    if (!_running || _bg_pid <= 0) return;
    int st; pid_t r = ::waitpid(_bg_pid, &st, WNOHANG);
    if (r == _bg_pid) { _running = false; _bg_pid = -1; std::snprintf(_status, sizeof(_status), "done -> loot/"); }
}
#endif

// ── LIFECYCLE ─────────────────────────────────────────────────────────

void app_harden_init() {
    _row = 0;
#ifdef VOIDOS_RPI5
    _out_n = 0; _running = false; _bg_pid = -1;
#endif
    std::snprintf(_status, sizeof(_status), "READY");
}
void app_harden_tick() {
#ifdef VOIDOS_RPI5
    harden_pump();
#endif
}
void app_harden_suspend() {
#ifdef VOIDOS_RPI5
    _running = false; _bg_pid = -1;
#endif
    std::snprintf(_status, sizeof(_status), "READY");
}

// ── EVENTS ────────────────────────────────────────────────────────────

void app_harden_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) { std::snprintf(_status, sizeof(_status), "READY"); return; }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    if (e.type != EVT_BTN_A_DOWN) return;
#ifdef VOIDOS_RPI5
    switch (_row) {
        case 0: do_ufw();        break;
        case 1: do_fail2ban();   break;
        case 2: do_rootkit();    break;
        case 3: do_lynis();      break;
        case 4: do_update();     break;
    }
#else
    std::snprintf(_status, sizeof(_status), "HARDEN: Linux only");
#endif
}

// ── DRAW ──────────────────────────────────────────────────────────────

void app_harden_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "HARDEN / DEFEND", T_FG, T_PANEL, FONT_SM);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    for (uint8_t i = 0; i < _out_n; ++i)
        draw_textf(8, info_y + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", _out[i]);

    draw_textf(8, SCR_H - 40, T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] ACT  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
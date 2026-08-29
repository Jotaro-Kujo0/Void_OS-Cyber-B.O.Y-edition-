
//app_help.cpp - small cheatsheet, just text yk


#include "app_help.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../config.h"
#include <cstdio>
#include <cstring>

//menu

static const char *LABELS[] = { "THIS APP", "ALL APPS", "HAL KEYS", "KEY MAP" };
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);
static uint8_t _row      = 0;
static uint8_t _last_app = 1;
struct HelpLine { uint8_t aid; const char *title; const char *lines; };

//per-app help 
static const HelpLine HELP[] = {
    { APP_STAT,  "STAT",  "vitals; cpu% mem% sd% up" },
    { APP_WIFI,  "WIFI",  "scan/inject/EAPOL; KARMA blocks under stealth" },
    { APP_LOG,   "LOG",   "captures go here as PCAPs/CSV" },
    { APP_RADIO, "RADIO", "sub-GHz: capture/sweep/POCSAG/dirty tx" },
    { APP_NFC,   "NFC",   "MIFARE read/clone" },
    { APP_IR,    "IR",    "TV-B-Gone-style blasts" },
    { APP_SYS,   "SYS",   "brightness + factory reset" },
    { APP_BUS,   "BUS",   "UART+TCP console; scanner target table" },
    { APP_HID,   "HID",   "BadUSB over USB" },
    { APP_DARK,  "DARK",  "MAC random, hostname, mDNS drop, OPS scan" },
    { APP_SCAN,  "SCAN",  "ARP / TCP portscan / service hints" },
    { APP_ROGUE, "ROGUE", "softAP + captive + DNS spoof -> loot/logins.csv" },
    { APP_SNIFF, "SNIFF", "URL cookie extractor + ip_forward MITM" },
    { APP_FUZZ,  "FUZZ",  "N HTTP GETs, status+latency -> loot/fuzz.csv" },
    { APP_WEB,   "WEB",   "GET URL, peek HTML for title/h1/links" },
    { APP_BODY,  "BODY",  "passive WiFi probe ring + OUI vendor" },
    { APP_LEAK,   "LEAK",   "secret/hash/breach scanner -> loot/leaks.csv" },
    { APP_MARAUD, "MARAUD", "Wi-Fi beacon spam/deauth/probe sheet (Pi5)" },
    { APP_OSINT,  "OSINT",  "whois/dns/subdom/geo/dork OSINT gatherer" },
    { APP_HARDEN, "HARDEN", "device self-hardening (ufw/fail2ban/lynis)" },
    { APP_HELP,  "HELP",  "you are here" },
};

static const uint8_t N_HELP = sizeof(HELP) / sizeof(HELP[0]);

static const HelpLine *find_help(uint8_t aid) {
    for (uint8_t i = 0; i < N_HELP; ++i)
        if (HELP[i].aid == aid) return &HELP[i];
    return nullptr;
}

//lifecycle 
void app_help_init() {
    _last_app = hal_storage_get_u8(NVS_LAST_APP, APP_HOME);
    if (_last_app >= APP_COUNT) _last_app = APP_HOME;
}

void app_help_tick() {}
void app_help_suspend() {}

//event handler
void app_help_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        return;
    }
    if (e.type == EVT_POT_CHANGED) {
        _row = static_cast<uint8_t>((e.data * N_ROWS) / 256);
        if (_row >= N_ROWS) _row = N_ROWS - 1;
    }
}

//draw
void app_help_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "HELP / CHEAT", T_FG, T_PANEL, FONT_SM);

    // Menu rows
    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + static_cast<int>(i) * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    // Body content
    int body_y = STATS_H + 8 + N_ROWS * row_h + 4;

    if (_row == 0) {
        // THIS APP — last active app's blurb
        const HelpLine *h = find_help(_last_app);
        if (!h)
            draw_textf(8, body_y, T_WARN, T_BG, FONT_SM, "no desc");
        else
            draw_textf(8, body_y, T_FG, T_BG, FONT_SM,
                       "%s  %s", h->title, h->lines);

    } else if (_row == 1) {
        // ALL APPS — compact list
        int yy = body_y;
        for (uint8_t i = 0; i < N_HELP; ++i) {
            if (yy > SCR_H - 40) break;
            draw_textf(8, yy, T_DIM, T_BG, FONT_SM,
                       "%-6s %s", HELP[i].title, HELP[i].lines);
            yy += 12;
        }

    } else if (_row == 2) {
        // HAL KEYS — editable storage keys
        draw_text(8, body_y, "editable hal_storage keys:", T_FG, T_BG, FONT_SM);
        int yy = body_y + 14;

        static const char *keys[] = {
            "rogue_ssid         evil-AP name",
            "rogue_chan         1..13",
            "scan_target        IPv4 : port",
            "fuzz_targets       comma-separated host:p",
            "fuzz_reps_tier     0..3 -> {16,64,256,1024}",
            "web_url            default https://example.com",
            "rmt_0..7           saved-target list (REMOTE)",
            "rmt_ps_0..11       preset commands per slot",
            "dark_0..3          app_dark toggles",
            "dark_ops           per-op MAC re-roll",
            "ms                 monotonic uptime placeholder",
        };
        static const uint8_t N_KEYS = sizeof(keys) / sizeof(keys[0]);

        for (uint8_t i = 0; i < N_KEYS && yy < SCR_H - 30; ++i) {
            draw_text(8, yy, keys[i], T_DIM, T_BG, FONT_SM);
            yy += 12;
        }

    } else {
        // KEY MAP — button legend
        static const char *map[] = {
            "[A]        ARM / commit / select sub-mode",
            "[B]        BACK to home / disarm",
            "[C]        SEND preset / clear",
            "[A]+[B]    factory reset (hold in SYS)",
            "[B] held   armed op flag (DARK)",
            "POT        row focus / target / baud",
        };
        static const uint8_t N_MAP = sizeof(map) / sizeof(map[0]);

        int yy = body_y;
        for (uint8_t i = 0; i < N_MAP; ++i) {
            draw_text(8, yy, map[i], T_DIM, T_BG, FONT_SM);
            yy += 14;
        }
    }

    // Footer
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
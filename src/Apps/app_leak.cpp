// app_leak.cpp — leak scanner. SCAN/HASH/BREACH glue; shells out with
// "TOOL MISSING" fallback (safe on bare box).
//               over the wordlist. Result shows on screen + loot/hashes.csv.
//   2. BREACH — correlate a username/email/hash against existing loot csv
//               rows; if an api key + curl are present, also hit the HIBP
//               range API with the SHA1 prefix.
//   3. SETTINGS — extra scan dir (`leak_dir`) + HIBP api key (`hibp_key`)
//               stored in hal_storage.

#include "app_leak.h"
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
#include <fstream>
#include <sys/wait.h>
#endif

// ── UI + state ─────────────────────────────────────────────────────────

static const char *LABELS[] = { "SCAN", "HASH", "BREACH", "SETTINGS" };
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);
static uint8_t _row = 0;
static char    _status[96] = "READY";
static char    _out_log[6][96];
static uint8_t _out_n = 0;

#ifdef VOIDOS_RPI5
static char    _extra_dir[64] = "";
static char    _hibp_key[40]  = "";
static char    _hash[48]      = "";
static uint8_t _mode          = 0;   // 0=MD5 1=SHA1 2=SHA256
#endif

// secret pattern list for SCAN (grep -E alternation)
static const char *SCAN_PATTERNS =
    "api[_-]?key|secret|password|passwd|"     // generic
    "AKIA[0-9A-Z]{16}|ASIA[0-9A-Z]{16}|"      // AWS
    "-----BEGIN (RSA|EC|OPENSSH|PGP) PRIVATE KEY"    // keys
    "|ghp_[0-9A-Za-z]{36}|gho_[0-9A-Za-z]{36}|"      // GitHub
    "|sk-[A-Za-z0-9]{20,}|xox[baprs]-[A-Za-z0-9-]+"  // openai/slack
    "|Bearer [A-Za-z0-9._~+/-]+=?";            // bearer tokens

static void push_out(const char *s) {
    if (_out_n >= 6) {
        for (uint8_t i = 1; i < 6; ++i)
            std::snprintf(_out_log[i - 1], 48, "%s", _out_log[i]);
        _out_n = 5;
    }
    std::snprintf(_out_log[_out_n++], 48, "%.47s", s);
}

#ifdef VOIDOS_RPI5
static bool tool_present(const char *name) {
    char cmd[80];
    std::snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return std::system(cmd) == 0;
}

// ── SCAN ──────────────────────────────────────────────────────────────
static void do_scan() {
    mkdir("/var/lib/void-os/loot", 0755);
    // Build grep target list: loot root + optional extra dir (skip "drop"
    // payload dir which is operator-generated binaries we don't scan for
    // secrets of our own).
    char targets[160];
    std::snprintf(targets, sizeof(targets), "%s", VOIDOS_LOOT_ROOT);
    if (_extra_dir[0])
        std::snprintf(targets, sizeof(targets), "%s %s", targets, _extra_dir);

    char cmd[400];
    std::snprintf(cmd, sizeof(cmd),
        "grep -rInIE -- '%s' %s 2>/dev/null | head -40 > "
        "/var/lib/void-os/loot/leaks.csv",
        SCAN_PATTERNS, targets);
    if (std::system(cmd) != 0) {
        // exit 1 = no matches; still valid
    }
    // count hits
    char ncmd[120];
    std::snprintf(ncmd, sizeof(ncmd),
        "wc -l < /var/lib/void-os/loot/leaks.csv 2>/dev/null");
    FILE *p = ::popen(ncmd, "r");
    int n = 0;
    if (p) { std::fscanf(p, "%d", &n); ::pclose(p); }
    std::snprintf(_status, sizeof(_status), "SCAN: %d hit(s) -> loot/leaks.csv", n);
    std::snprintf(_out_log[0], 48, "grep -rIE over loot%s",
                  _extra_dir[0] ? " + extra dir" : "");
    _out_n = 1;
}

// ── HASH ──────────────────────────────────────────────────────────────
static void crack_mode(char *alg, uint8_t *mode) {
    switch (_mode) {
        case 0: *mode = 0;   std::snprintf(alg, 6, "md5");    break;
        case 1: *mode = 100; std::snprintf(alg, 6, "sha1");   break;
        default:*mode = 100; std::snprintf(alg, 6, "sha256"); break;
    }
}

// Resolve the wordlist path; if none is configured, seed loot/wordlist.txt
// from the bundled credentials so the app works on a fresh Pi with no
// setup. Returns true if a usable wordlist is present.
static bool resolve_wordlist(char *word, uint32_t cap) {
    if (!hal_storage_get_str("leak_wordlist", word, cap) || !word[0]) {
        mkdir("/var/lib/void-os/loot", 0755);
        std::snprintf(word, cap, "/var/lib/void-os/loot/wordlist.txt");
        struct stat st;
        if (::stat(word, &st) != 0) {   // seed from bundle on first run
            std::ofstream f(word, std::ios::trunc);
            if (f) for (uint32_t i = 0; i < LEAK_CRED_COUNT; ++i) f << LEAK_CREDENTIALS[i] << "\n";
        }
    }
    struct stat st;
    return ::stat(word, &st) == 0;
}

static void do_hash() {
    if (!_hash[0]) { std::snprintf(_status,sizeof(_status),"no hash set"); return; }
    char word[80];
    if (!resolve_wordlist(word, sizeof(word))) {
        std::snprintf(_status, sizeof(_status), "wordlist unavailable");
        return;
    }

    if (tool_present("hashcat")) {
        char alg[8]; uint8_t mode;
        crack_mode(alg, &mode);
        char cmd[300];
        std::snprintf(cmd, sizeof(cmd),
            "hashcat -m %u '%s' '%s' --show 2>/dev/null | head -1",
            mode, _hash, word);
        FILE *p = ::popen(cmd, "r");
        char res[80] = "";
        if (p) { std::fgets(res, sizeof(res), p); ::pclose(p); }
        if (res[0]) {
            // format "hash:plaintext"
            char *colon = std::strchr(res, ':');
            std::snprintf(_status, sizeof(_status), "CRACKED: %s",
                          colon ? colon + 1 : res);
        } else {
            std::snprintf(_status, sizeof(_status), "not in wordlist (%s)", alg);
        }
    } else {
        // Fallback: sha-N sweep line-by-line.
        char cmd[300];
        std::snprintf(cmd, sizeof(cmd),
            "while read w; do "
            "h=$(printf '%%s' \"$w\" | %ssum | cut -d' ' -f1); "
            "[ \"%s\" = \"$h\" ] && echo \"$w\" && break; "
            "done < '%s' 2>/dev/null | head -1", "sha1", _hash, word);
        FILE *p = ::popen(cmd, "r");
        char res[80] = "";
        if (p) { std::fgets(res, sizeof(res), p); ::pclose(p); }
        if (res[0])
            std::snprintf(_status, sizeof(_status), "CRACKED: %s", res);
        else
            std::snprintf(_status, sizeof(_status), "not in wordlist (sha1 qy)");
    }
    std::snprintf(_out_log[0], 48, "hash %s (mode %u)", _hash, _mode);
    _out_n = 1;
}

// ── BREACH correl + (optional) HIBP ───────────────────────────────────
static void do_breach() {
    // Correlate against loot csv rows for the current target string.
    if (!_hash[0]) { std::snprintf(_status,sizeof(_status),"no email/hash set"); return; }
    char cmd[300];
    std::snprintf(cmd, sizeof(cmd),
        "grep -rihn '%s' %s 2>/dev/null | head -8 > /var/lib/void-os/loot/breach.csv",
        _hash, VOIDOS_LOOT_ROOT);
    std::system(cmd);
    char ncmd[120];
    std::snprintf(ncmd, sizeof(ncmd),
        "wc -l < /var/lib/void-os/loot/breach.csv 2>/dev/null");
    FILE *p = ::popen(ncmd, "r");
    int n = 0;
    if (p) { std::fscanf(p, "%d", &n); ::pclose(p); }

    // Optional HIBP range API if key + curl present. Fold the whole
    // query into one pipeline: sha1(target) -> take prefix5 -> range
    // query -> grep the remaining suffix for a match count.
    bool hibp = false;
    if (_hibp_key[0] && tool_present("curl")) {
        char c[360];
        std::snprintf(c, sizeof(c),
            "sha1=$(printf '%%s' '%s' | sha1sum | cut -d' ' -f1); "
            "cur=$(curl -s -A void-os -H 'hibp-api-key: %s' "
            "'https://api.pwnedpasswords.com/range/$${sha1%%%%:5}' 2>/dev/null); "
            "echo \"$cur\" | grep -i \"$${sha1:5}\" | head -1",
            _hash, _hibp_key);
        FILE *r = ::popen(c, "r");
        if (r) {
            char line[80] = "";
            if (std::fgets(line, sizeof(line), r)) {
                char *colon = std::strchr(line, ':');
                std::snprintf(_status, sizeof(_status), "HIBP: %s exposures",
                              colon ? colon + 1 : line);
                hibp = true;
            }
            ::pclose(r);
        }
    }
    if (!hibp) {
        std::snprintf(_status, sizeof(_status), "loot breach: %d row(s)", n);
    }
    std::snprintf(_out_log[0], 48, "target: %s", _hash);
    std::snprintf(_out_log[1], 48, "loot rows: %d  hibp:%s", n,
                  _hibp_key[0] ? _hibp_key : "no key");
    _out_n = 2;
}
#endif

// ── PERSISTENCE ───────────────────────────────────────────────────────

static void load_cfg() {
#ifdef VOIDOS_RPI5
    if (!hal_storage_get_str("leak_dir", _extra_dir, sizeof(_extra_dir)))
        _extra_dir[0] = 0;
    if (!hal_storage_get_str("hibp_key", _hibp_key, sizeof(_hibp_key)))
        _hibp_key[0] = 0;
    if (!hal_storage_get_str("leak_target", _hash, sizeof(_hash)))
        _hash[0] = 0;
    _mode = hal_storage_get_u8("leak_mode", 0);
#else
    (void)0;
#endif
}

// ── LIFECYCLE ─────────────────────────────────────────────────────────

void app_leak_init() {
    load_cfg();
    _row = 0;
    _out_n = 0;
    std::snprintf(_status, sizeof(_status), "READY");
}

void app_leak_tick() {}
void app_leak_suspend() {}

// ── EVENTS ────────────────────────────────────────────────────────────

void app_leak_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) { std::snprintf(_status,sizeof(_status),"READY"); return; }
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    if (e.type != EVT_BTN_A_DOWN) return;
#ifdef VOIDOS_RPI5
    switch (_row) {
        case 0: do_scan();   break;
        case 1: do_hash();   break;
        case 2: do_breach(); break;
        case 3: {
            // SETTINGS — cycle the hash mode (MD5/SHA1/SHA256). The dir and
            // HIBP key are set via the serial-term `set` command (see
            // hal_serial_term dispatch); here we just persist + report.
            _mode = (_mode + 1) % 3;
            hal_storage_set_u8("leak_mode", _mode);
            char wl[80];
            std::snprintf(wl, sizeof(wl), "(default)");
            hal_storage_get_str("leak_wordlist", wl, sizeof(wl));
            std::snprintf(_status, sizeof(_status),
                          "mode:%s wl:%s",
                          _mode==0?"MD5":_mode==1?"SHA1":"SHA256", wl);
            std::snprintf(_out_log[0], 48, "dir:%s  hibp:%s",
                          _extra_dir[0]?_extra_dir:"(loot)",
                          _hibp_key[0]?"set":"none");
            _out_n = 1;
            break;
        }
    }
#else
    std::snprintf(_status, sizeof(_status), "LEAK: Linux only");
#endif
}

// ── DRAW ──────────────────────────────────────────────────────────────

void app_leak_draw() {
    draw_fill(0,0,SCR_W,SCR_H,T_BG);
    draw_fill(0,0,SCR_W,STATS_H,T_PANEL);
    draw_hline(0,STATS_H,SCR_W,T_BORDER);
    draw_text(8, 8, "LEAK / SCANNER", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 80, 8, T_DIM, T_PANEL, FONT_SM, "SCAN HASH BREACH");

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + i * row_h;
        draw_textf(8, y, i == _row ? T_FG : T_DIM, T_BG, FONT_SM,
                   "%s %s", i == _row ? ">" : " ", LABELS[i]);
    }

    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    for (uint8_t i = 0; i < _out_n; ++i)
        draw_textf(8, info_y + (int)i * 11, T_DIM, T_BG, FONT_SM, "%s", _out_log[i]);

    draw_textf(8, SCR_H - 40, T_DIM, T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12, "[A] RUN  [B] BACK  POT=row", T_DIM, T_BG, FONT_SM);
}
// app_bus.cpp — bus/hardware sniffer + cable/remote console.
// LOGIC / WIEGAND / iBUTTON / CONSOLE / REMOTE / GPIO / DTMF

#include <Arduino.h>
#include "app_bus.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_wiegand.h"
#include "../hal/hal_onewire_emul.h"
#include "../hal/hal_storage.h"
#include "../config.h"
#include "events.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef VOIDOS_RPI5
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#endif

// MENU

static const char *LABELS[] = {
    "LOGIC (CH0-3)", //0
    "WIEGAND (D0/D1)",  // 1
    "iBUTTON (1-W)",    // 2
    "CONSOLE (UART)",   // 3
    "REMOTE  (TCP)",    // 4
    "GPIO   (TEST)",    // 5
    "DTMF   (RX)",      // 6
};
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);

// LIMITS

#define BUS_RX_BYTES    1024
#define BUS_RMT_TARGETS_MAX     8
#define BUS_RMT_TARGET_LEN      48
#define BUS_PRESETS_MAX     12
#define BUS_PRESET_LEN      32

// shared stateg

static uint8_t  _row    = 0;
static bool     _active = false;
static char     _status[24] = "READY";

// Default remote presets loaded into hal_storage on first boot.
static const char *REMOTE_PRESETS_DEFAULT[] = {
    "\n",                "id\n",              "uname -a\n",
    "cat /etc/passwd\n", "ip a\n",            "ifconfig\n",
    "sudo -l\n",         "ss -lntp\n",        "ps auxf\n",
    "help\n",            "exit\n",            "history\n",
};

// logic analyzer -stub
//  Future: libgpiod line events at ~50 kHz on 4 GPIOs.
//  Enough to decode UART ≤115200, I2C ≤400 kHz, SPI ≤1 MHz.

//console state

static const uint32_t BAUD_POOL[] = {
    9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
};
static const uint8_t N_BAUD = sizeof(BAUD_POOL) / sizeof(BAUD_POOL[0]);

#ifdef VOIDOS_RPI5
static int      _con_fd   = -1;
#else
static bool     _con_open = false;
#endif
static uint32_t _con_baud = 115200;
static uint16_t _con_head = 0;
static uint16_t _con_tail = 0;
static uint8_t  _con_rx[BUS_RX_BYTES];

//remote state

#ifdef VOIDOS_RPI5
static int      _rmt_fd   = -1;
#else
static bool     _rmt_open = false;
#endif
static uint8_t  _rmt_sel  = 0;
static char     _rmt_targets[BUS_RMT_TARGETS_MAX][BUS_RMT_TARGET_LEN];
static char     _rmt_presets[BUS_PRESETS_MAX][BUS_PRESET_LEN];
static uint16_t _rmt_head = 0;
static uint16_t _rmt_tail = 0;
static uint8_t  _rmt_rx[BUS_RX_BYTES];

//GPIO state
#ifdef VOIDOS_RPI5
static int      _gpio_fd  = -1;   // /dev/gchipN for libgpiod
static bool     _gpio_out = false;
static uint8_t  _gpio_val = 0;
#endif

//persistence
static void rmt_load() {
    _rmt_sel = hal_storage_get_u8("rmt_sel", 0);
    for (uint8_t i = 0; i < BUS_RMT_TARGETS_MAX; ++i) {
        char k[8];
        std::snprintf(k, sizeof(k), "rmt_%u", i);
        if (!hal_storage_get_str(k, _rmt_targets[i], BUS_RMT_TARGET_LEN))
            _rmt_targets[i][0] = 0;
    }
    // Seed first target if empty.
    if (!_rmt_targets[0][0]) {
        std::snprintf(_rmt_targets[0], BUS_RMT_TARGET_LEN, "root@127.0.0.1:22");
        hal_storage_set_str("rmt_0", _rmt_targets[0]);
    }
    for (uint8_t i = 0; i < BUS_PRESETS_MAX; ++i) {
        char k[12];
        std::snprintf(k, sizeof(k), "rmt_ps_%u", i);
        if (!hal_storage_get_str(k, _rmt_presets[i], BUS_PRESET_LEN))
            std::snprintf(_rmt_presets[i], BUS_PRESET_LEN, "%s",
                          REMOTE_PRESETS_DEFAULT[i]);
    }
    hal_storage_commit();
}

//rıng buffer helpers
static inline void rx_push(uint8_t *ring, uint16_t *head, uint8_t b) {
    ring[*head] = b;
    *head = static_cast<uint16_t>((*head + 1) % BUS_RX_BYTES);
}

static uint16_t rx_drain(uint8_t *ring, uint16_t head, uint16_t *tail,
                         uint8_t *dst, uint16_t cap) {
    uint16_t n = 0;
    while (*tail != head && n < cap) {
        dst[n++] = ring[*tail];
        *tail = static_cast<uint16_t>((*tail + 1) % BUS_RX_BYTES);
    }
    return n;
}

//console open/close/pump/send
#ifdef VOIDOS_RPI5

static bool con_open(uint32_t baud) {
    if (_con_fd >= 0) { _con_baud = baud; return true; }

    // Try common Pi serial ports.
    const char *ports[] = {"/dev/ttyUSB0", "/dev/ttyACM0", "/dev/serial0"};
    int fd = -1;
    for (int i = 0; i < 3; ++i) {
        fd = ::open(ports[i], O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) break;
    }
    if (fd < 0) {
        std::snprintf(_status, sizeof(_status), "UART: NO DEV");
        return false;
    }

    struct termios tty{};
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    tty.c_cflag |= (CREAD | CLOCAL);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;  // 500 ms inter-byte timeout

    // Set baud rate.
    speed_t spd;
    switch (baud) {
        case 9600:   spd = B9600;   break;
        case 19200:  spd = B19200;  break;
        case 38400:  spd = B38400;  break;
        case 57600:  spd = B57600;  break;
        case 115200: spd = B115200; break;
        case 230400: spd = B230400; break;
        case 460800: spd = B460800; break;
        case 921600: spd = B921600; break;
        default:     spd = B115200; break;
    }
    cfsetspeed(&tty, spd);
    tcsetattr(fd, TCSANOW, &tty);

    _con_fd   = fd;
    _con_baud = baud;
    std::snprintf(_status, sizeof(_status), "UART %lu", (unsigned long)baud);
    return true;
}

static void con_close() {
    if (_con_fd >= 0) { ::close(_con_fd); _con_fd = -1; }
}

static void con_pump() {
    if (_con_fd < 0) return;
    uint8_t buf[64];
    for (;;) {
        ssize_t n = ::read(_con_fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i) {
            rx_push(_con_rx, &_con_head, buf[i]);
            // Mirror raw bytes to stdout (laptop USB-CDC).
            ::write(STDOUT_FILENO, &buf[i], 1);
        }
    }
}

static void con_resize() {
    // Reopen with new baud rate.
    if (_con_fd >= 0) { ::close(_con_fd); _con_fd = -1; }
    con_open(_con_baud);
}

static void con_send(const char *s, uint8_t n) {
    if (_con_fd < 0) return;
    ::write(_con_fd, s, n);
}

#else  // ESP32

static bool con_open(uint32_t baud) {
    if (_con_open) { Serial1.updateBaudRate(baud); _con_baud = baud; return true; }
    Serial1.begin(baud);
    _con_open = true;
    _con_baud = baud;
    std::snprintf(_status, sizeof(_status), "UART %lu", (unsigned long)baud);
    return true;
}

static void con_close() {
    if (_con_open) { Serial1.end(); _con_open = false; }
}

static void con_pump() {
    while (Serial1.available()) {
        uint8_t b = static_cast<uint8_t>(Serial1.read());
        rx_push(_con_rx, &_con_head, b);
        Serial.write(b);
    }
}

static void con_resize() {
    if (_con_open) Serial1.updateBaudRate(_con_baud);
}

static void con_send(const char *s, uint8_t n) {
    Serial1.write(s, n);
}

#endif  // VOIDOS_RPI5

//remote open/close/pump/send
static bool rmt_split_hostport(const char *tgt, char *host, uint16_t *port) {
    *port = 22;
    if (!tgt || !*tgt) return false;

    const char *at = std::strchr(tgt, '@');
    const char *start = at ? at + 1 : tgt;
    char buf[BUS_RMT_TARGET_LEN];
    std::snprintf(buf, sizeof(buf), "%s", start);

    char *colon = std::strchr(buf, ':');
    if (colon) {
        *colon = 0;
        *port = static_cast<uint16_t>(std::atoi(colon + 1));
    }
    std::snprintf(host, BUS_RMT_TARGET_LEN, "%s", buf);
    return host[0] != 0;
}

#ifdef VOIDOS_RPI5

static bool rmt_open() {
    if (_rmt_fd >= 0) return true;

    char host[BUS_RMT_TARGET_LEN];
    uint16_t port;
    if (!rmt_split_hostport(_rmt_targets[_rmt_sel], host, &port))
        return false;

    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);

    // Resolve hostname (tries numeric first, falls back to DNS).
    if (::inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        struct hostent *he = ::gethostbyname(host);
        if (!he) { ::close(s); return false; }
        std::memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    }

    if (::connect(s, reinterpret_cast<struct sockaddr *>(&sa), sizeof(sa)) < 0) {
        ::close(s);
        return false;
    }

    // Non-blocking for the pump.
    int fl = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, fl | O_NONBLOCK);

    _rmt_fd = s;
    std::snprintf(_status, sizeof(_status), "%.17s:%u", host, port);
    return true;
}

static void rmt_close() {
    if (_rmt_fd >= 0) { ::close(_rmt_fd); _rmt_fd = -1; }
}

static void rmt_pump() {
    if (_rmt_fd < 0) return;
    uint8_t buf[128];
    for (;;) {
        ssize_t n = ::recv(_rmt_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i)
            rx_push(_rmt_rx, &_rmt_head, buf[i]);
    }
}

static void rmt_send(const char *s, uint8_t n) {
    if (_rmt_fd < 0) return;
    ::send(_rmt_fd, s, n, 0);
}

#else  // ESP32

static bool rmt_open() {
    if (_rmt_open) return true;
    // ESP32: WiFiClient deferred until radio is up.
    _rmt_open = true;
    std::snprintf(_status, sizeof(_status), "%s",
                  _rmt_targets[_rmt_sel][0] ? _rmt_targets[_rmt_sel] : "(no target)");
    return true;
}

static void rmt_close() { _rmt_open = false; }
static void rmt_pump()  { /* HAL defers real driver */ }
static void rmt_send(const char *s, uint8_t n) { (void)s; (void)n; }

#endif  // VOIDOS_RPI5

//lifecycle 
void app_bus_init() {
    _row    = 0;
    _active = false;
    hal_wiegand_set_enabled(false);
    hal_onewire_emul_set_mode(DS1990_OFF);
    rmt_load();
    std::snprintf(_status, sizeof(_status), "READY");
    _con_tail = _con_head;
    _rmt_tail = _rmt_head;
}

void app_bus_tick() {
    if (_row == 3 && _active) con_pump();
    if (_row == 4 && _active) rmt_pump();
}

void app_bus_suspend() {
    hal_wiegand_set_enabled(false);
    hal_onewire_emul_set_mode(DS1990_OFF);
    con_close();
    rmt_close();
    _active = false;
    _con_tail = _con_head;
    _rmt_tail = _rmt_head;
}

//event handler

void app_bus_event(Event e) {
    //back - disarm
    if (e.type == EVT_BTN_B_DOWN) {
        _active = false;
        hal_wiegand_set_enabled(false);
        hal_onewire_emul_set_mode(DS1990_OFF);
        con_close();
        rmt_close();
        std::snprintf(_status, sizeof(_status), "READY");
        return;
    }

    //POT: scroll /cycle baud and target
    if (e.type == EVT_POT_CHANGED) {
        if (_row == 3) {
            //cycle baud rate
            uint8_t idx = static_cast<uint8_t>((e.data * N_BAUD) / 256);
            if (_rmt_sel >= BUS_RMT_TARGETS_MAX)
                _rmt_sel =  BUS_RMT_TARGETS_MAX - 1;
            hal_storage_set_u8("rmt_sel", _rmt_sel);
            rmt_close();
            return;
        }
        // default: scroll main menu
        _row = static_cast<uint8_t>((e.data * N_ROWS) / 256);
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }

    // A: arm /activate
    if (e.type == EVT_BTN_A_DOWN) {
        _active = true;
        if (_row == 0) std::snprintf(_status, sizeof(_status), "LOGIC: stub");
         if (_row == 1) hal_wiegand_set_enabled(true);
        if (_row == 2) hal_onewire_emul_set_mode(DS1990_READ);
        if (_row == 3) con_open(_con_baud);
        if (_row == 4) rmt_open();
        if (_row == 5) std::snprintf(_status, sizeof(_status), "GPIO: stub");
        if (_row == 6) std::snprintf(_status, sizeof(_status), "DTMF: stub");
        return;
    }

    //C: send preset
    if (e.type == EVT_BTN_C_DOWN) {
        uint8_t idx = (_row == 4)
            ? (_rmt_sel % BUS_PRESETS_MAX)
            : 0;
        const char *p = _rmt_presets[idx];
        uint8_t n = static_cast<uint8_t>(std::strlen(p));

        if (_row == 3) { if  (!_active ) con_open(_con_baud); con_send(p, n); }
         if (_row == 4) { if (!_active) rmt_open();            rmt_send(p, n); }
        if (_row == 4)
            std::snprintf(_status, sizeof(_status), "> %.21s", p);
        return;

    }
}

//draw helpers
// Render a single-line RX preview, sanitized to printable ASCII.
static void draw_rx_strip(int y, uint8_t *ring, uint16_t head,
                          uint16_t *tail, uint16_t fg, uint16_t bg) {
    uint8_t buf[64];
    uint16_t n = rx_drain(ring, head, tail, buf, sizeof(buf));
    if (!n) return;

    char line[65];
    int w = static_cast<int>(n);
    if (w > 60) w = 60;
    for (int i = 0; i < w; ++i) {
        uint8_t b = buf[i];
        line[i] = (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
    }
    line[w] = '\0';
    draw_textf(8, y, fg, bg, FONT_SM, "RX: %s", line);
}

//draw
void app_bus_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "BUS / CABLE", T_FG, T_PANEL, FONT_SM);

    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + static_cast<int>(i) * row_h;
        if (y > SCR_H - 110) continue;
        draw_textf(8, y,
                   (_active && i == _row) ? T_WARN :
                   (i == _row ? T_FG : T_DIM),
                   T_BG, FONT_SM, "%s %s",
                   (i == _row ? ">" : " "), LABELS[i]);
    }

    //  Info panel bottom
    int info_y = SCR_H - 108;

    if (_row == 3) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
                   "CONSOLE @ %lub  preset[0]: %.20s",
                   (unsigned long)_con_baud, _rmt_presets[0]);
        draw_rx_strip(info_y + 14, _con_rx, _con_head, &_con_tail, T_DIM, T_BG);

    } else if (_row == 4) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM,
                   "REMOTE [%u] %s",
                   _rmt_sel,
                   _rmt_targets[_rmt_sel][0] ? _rmt_targets[_rmt_sel] : "(none)");
        draw_textf(8, info_y + 14, T_DIM, T_BG, FONT_SM,
                   "preset[%u]: %.24s",
                   _rmt_sel % BUS_PRESETS_MAX,
                   _rmt_presets[_rmt_sel % BUS_PRESETS_MAX]);
        draw_rx_strip(info_y + 28, _rmt_rx, _rmt_head, &_rmt_tail, T_DIM, T_BG);

    } else if (_row == 0) {
        draw_textf(8, info_y, T_DIM, T_BG, FONT_SM,
                   "4ch logic analyzer");
        draw_textf(8, info_y + 14, T_DIM, T_BG, FONT_SM,
                   "libgpiod @ ~50 kHz — future");

    } else if (_row == 1) {
        draw_textf(8, info_y, _active ? T_WARN : T_DIM, T_BG, FONT_SM,
                   "Wiegand D0/D1 — %s", _active ? "CAPTURING" : "idle");

    } else if (_row == 2) {
        draw_textf(8, info_y, _active ? T_WARN : T_DIM, T_BG, FONT_SM,
                   "iButton 1-Wire — %s", _active ? "ACTIVE" : "idle");

    } else if (_row == 5) {
        draw_textf(8, info_y, T_DIM, T_BG, FONT_SM,
                   "GPIO read/write — stub");

    } else if (_row == 6) {
        draw_textf(8, info_y, T_DIM, T_BG, FONT_SM,
                   "DTMF/POCSAG/FSK — stub");
    }

    //Status bar 
    draw_textf(8, SCR_H - 28, _active ? T_WARN : T_DIM, T_BG, FONT_SM,
               "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12,
              "[A] ARM  [C] SEND  [B] BACK  POT=sel",
              T_DIM, T_BG, FONT_SM);
}
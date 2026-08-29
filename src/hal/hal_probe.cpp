// hal_probe.cpp — shared passive Wi-Fi probe-request capture (Pi 5)
//
// One tcpdump child feeds a line ring that app_body + app_maraud share.
// On ESP32 / without tcpdump the module is a no-op.

#include "hal_probe.h"
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

#define IFACE "wlan0mon"

static ProbeDevice _ring[PROBE_RING_MAX];
static uint16_t    _ring_n = 0;
static ProbeSink   _sink   = nullptr;
static bool        _running = false;

#ifdef VOIDOS_RPI5
static pid_t  _tcpdump_pid = -1;
static int    _pipe_fd = -1;
static char   _line_buf[512];
static int    _line_pos = 0;

static uint32_t sys_millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

void hal_probe_clear() { _ring_n = 0; }

uint16_t hal_probe_visible_count() { return _ring_n; }
const ProbeDevice *hal_probe_at(uint16_t i) {
    return (i < _ring_n) ? &_ring[i] : nullptr;
}

void hal_probe_register_sink(ProbeSink s) { _sink = s; }

// ── Ring ingest (both targets) ────────────────────────────────────────

static void ingest(const uint8_t *mac, const char *ssid) {
    if (!mac) return;
    int16_t idx = -1;
    for (uint16_t i = 0; i < _ring_n; ++i)
        if (std::memcmp(_ring[i].mac, mac, 6) == 0) { idx = (int16_t)i; break; }
    if (idx < 0) {
        if (_ring_n >= PROBE_RING_MAX) return;   // ring full; drop new MACs
        idx = (int16_t)_ring_n++;
        std::memcpy(_ring[idx].mac, mac, 6);
        _ring[idx].count = 0;
        _ring[idx].ssid[0] = 0;
    }
    ++_ring[idx].count;
    if (ssid && ssid[0])
        std::snprintf(_ring[idx].ssid, sizeof(_ring[idx].ssid), "%s", ssid);
    _ring[idx].last_ms = sys_millis();

    if (_sink) _sink(mac, ssid);
}

#ifdef VOIDOS_RPI5

// ── tcpdump pipe ──────────────────────────────────────────────────────

bool hal_probe_capture_start() {
    if (_running) return true;
    int pipefd[2];
    if (pipe(pipefd) < 0) return false;

    _tcpdump_pid = fork();
    if (_tcpdump_pid == 0) {
        close(pipefd[0]); dup2(pipefd[1], STDOUT_FILENO); close(pipefd[1]);
        execlp("tcpdump", "tcpdump", "-l", "-e", "-i", IFACE,
               "-s", "256", "type", "mgt", "subtype", "probe-req", (char*)0);
        _exit(1);
    }
    close(pipefd[1]);
    _pipe_fd = pipefd[0];
    fcntl(_pipe_fd, F_SETFL, O_NONBLOCK);
    _running = true;
    return true;
}

void hal_probe_capture_stop() {
    if (_tcpdump_pid > 0) {
        kill(_tcpdump_pid, SIGTERM);
        waitpid(_tcpdump_pid, NULL, 0);
        _tcpdump_pid = -1;
    }
    if (_pipe_fd >= 0) { close(_pipe_fd); _pipe_fd = -1; }
    _running = false;
    _line_pos = 0;
}

bool hal_probe_running() { return _running; }

// parse one tcpdump -e probe line, e.g. ...SA aa:bb:... (...) SSID (...)
static void parse_line(const char *line) {
    const char *sa = std::strstr(line, "SA ");
    if (!sa) return;
    sa += 3;
    uint8_t mac[6]; unsigned m[6];
    if (std::sscanf(sa, "%02x:%02x:%02x:%02x:%02x:%02x",
                    &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) return;
    for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)m[i];

    char ssid[33] = "";
    const char *p = std::strstr(line, "(\"");
    if (p) { p += 2; int i = 0; while (p[i] && p[i] != '"' && i < 32) { ssid[i] = p[i]; ++i; } ssid[i] = 0; }
    ingest(mac, ssid);
}

void hal_probe_tick() {
    if (!_running || _pipe_fd < 0) return;
    char buf[256];
    ssize_t n = read(_pipe_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    if (n < (ssize_t)sizeof(buf)) buf[n] = 0; else buf[sizeof(buf)-1] = 0;

    for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == '\n' || _line_pos >= (int)sizeof(_line_buf) - 1) {
            _line_buf[_line_pos] = 0;
            if (_line_pos > 10) parse_line(_line_buf);
            _line_pos = 0;
        } else {
            _line_buf[_line_pos++] = buf[i];
        }
    }
    int st; pid_t r = waitpid(_tcpdump_pid, &st, WNOHANG);
    if (r == _tcpdump_pid) { _tcpdump_pid = -1; _running = false; }
}

#else   // ── ESP32 stub ──────────────────────────────────────────────────

bool hal_probe_capture_start() { return false; }
void hal_probe_capture_stop() {}
bool hal_probe_running() { return false; }
void hal_probe_tick() {}

#endif  // VOIDOS_RPI5

void hal_probe_init() { _ring_n = 0; _sink = nullptr; }
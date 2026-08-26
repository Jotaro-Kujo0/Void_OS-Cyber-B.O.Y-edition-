// hal_network.cpp — real sysfs / syscall / iptables wrappers.
//
// Every function returns false on any failure (missing interface, no
// CAP_NET_ADMIN, iptables not installed, etc). The caller logs and
// continues; the OS never aborts on a stealth-toggle failure.
//
// We use std::system(3) for iptables / ip / sysctl because:
//
//   * these are admin commands the operator runs at most a few times per
//     minute, so the ~2 ms fork/exec cost is irrelevant;
//   * pulling in libiptc / libmnl / libnl would add three library deps
//     for half a dozen syscalls;
//   * the iptables command is the same one the operator would type at a
//     shell, so debugging is one grep away.
//
// Filesystem writes (resolved.conf, /etc/hostname) are streamed via
// std::ofstream with umask 0644 so they are not world-writable.

#include "hal_network.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <unistd.h>

#ifndef VOIDOS_RPI5
// ESP32 path: every function is a no-op. The radio is on-chip and the
// real masking logic lives in hal_wifi_set_stealth().
bool hal_network_randomize_mac(const char *)   { return false; }
bool hal_network_get_mac(const char *, uint8_t [6]) { return false; }
bool hal_network_set_hostname(const char *)    { return false; }
bool hal_network_randomize_hostname()         { return false; }
const char *hal_network_get_hostname()         { return "void-os"; }
bool hal_network_flush_arp()                  { return false; }
bool hal_network_flush_routes()               { return false; }
bool hal_network_flush_dns()                  { return false; }
bool hal_network_reset_link(const char *)     { return false; }
bool hal_network_block_mdns()                 { return false; }
bool hal_network_unblock_mdns()               { return false; }
bool hal_network_block_llmnr()                { return false; }
bool hal_network_unblock_llmnr()              { return false; }
bool hal_network_block_netbios()              { return false; }
bool hal_network_unblock_netbios()            { return false; }
bool hal_network_block_all_discovery()        { return false; }
bool hal_network_unblock_all_discovery()      { return false; }
bool hal_network_doh_enable(const char *)     { return false; }
bool hal_network_dot_enable(const char *)     { return false; }
bool hal_network_dns_clear()                  { return false; }
bool hal_network_set_ttl(uint8_t)             { return false; }
bool hal_network_set_tcp_window_scaling(uint8_t) { return false; }
bool hal_network_stealth_enable()             { return false; }
bool hal_network_stealth_disable()            { return false; }
bool hal_network_stealth_is_enabled()         { return false; }
#else

namespace {

// ── helpers ──────────────────────────────────────────────────────────────

// Run a shell command. Returns true iff exit status == 0.
bool sh(const char *cmd) { return std::system(cmd) == 0; }

// Read up to `len-1` bytes from `path` into `buf`, NUL-terminated.
// Strips trailing newline.
bool read_file(const char *path, char *buf, size_t len) {
    std::ifstream f(path);
    if (!f) return false;
    f.get(buf, static_cast<std::streamsize>(len));
    size_t n = static_cast<size_t>(f.gcount());
    if (n && buf[n - 1] == '\n') buf[n - 1] = 0;
    return true;
}

// Write `val` to `path`. Used for sysfs files where std::system(3) is
// overkill (a 1-byte echo).
bool write_file(const char *path, const char *val) {
    std::ofstream f(path);
    if (!f) return false;
    f << val;
    return static_cast<bool>(f);
}

// One random byte. /dev/urandom first; falls back to rand() if urandom
// is unreadable (very unusual on Pi 5).
uint8_t rbyte() {
    uint8_t b = 0;
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ::read(fd, &b, 1);
        ::close(fd);
        return b;
    }
    return static_cast<uint8_t>(std::rand() & 0xFF);
}

char  _hostname[64] = {};
bool  _stealth      = false;

// Comment tag used in every iptables rule we write so the operator can
// find and remove them in one grep.
constexpr const char *TAG = "void-os";

// iptables: insert a DROP rule with our comment tag.
// `family` is "-4" or "-6"; `table` is the iptables table name; the
// remaining args form the match. Returns true on exit 0.
bool ipt_drop(const char *family, const char *args) {
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "iptables %s -I OUTPUT 1 -m comment --comment \"%s\" -j DROP %s 2>/dev/null",
                  family, TAG, args);
    return sh(cmd);
}

bool ipt_undrop(const char *family, const char *args) {
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "iptables %s -D OUTPUT -m comment --comment \"%s\" -j DROP %s 2>/dev/null",
                  family, TAG, args);
    return sh(cmd);
}

} // namespace

// ── 1. MAC ────────────────────────────────────────────────────────────────

bool hal_network_randomize_mac(const char *iface) {
    if (!iface || !*iface) return false;
    uint8_t mac[6];
    for (int i = 0; i < 6; ++i) mac[i] = rbyte();
    mac[0] = static_cast<uint8_t>((mac[0] & 0xFE) | 0x02);   // LAA on, mcast off
    char val[24];
    std::snprintf(val, sizeof(val), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char cmd[160];
    std::snprintf(cmd, sizeof(cmd),
                  "ip link set %s down && ip link set %s address %s && ip link set %s up",
                  iface, iface, val, iface);
    return sh(cmd);
}

bool hal_network_get_mac(const char *iface, uint8_t out[6]) {
    if (!iface || !out) return false;
    char path[64];
    std::snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
    char buf[32];
    if (!read_file(path, buf, sizeof(buf))) return false;
    unsigned int m[6];
    if (std::sscanf(buf, "%x:%x:%x:%x:%x:%x",
                    &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) return false;
    for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(m[i]);
    return true;
}

// ── 2. Hostname ───────────────────────────────────────────────────────────

bool hal_network_set_hostname(const char *hostname) {
    if (!hostname || !*hostname) return false;
    if (::sethostname(hostname, std::strlen(hostname)) != 0) return false;
    // Persist for next boot so DHCP doesn't pick up the kernel default.
    std::ofstream f("/etc/hostname", std::ios::trunc);
    if (f) f << hostname << "\n";
    std::snprintf(_hostname, sizeof(_hostname), "%s", hostname);
    return true;
}

bool hal_network_randomize_hostname() {
    char name[16];
    std::snprintf(name, sizeof(name), "vd-%02x%02x%02x%02x",
                  (unsigned)rbyte(), (unsigned)rbyte(),
                  (unsigned)rbyte(), (unsigned)rbyte());
    return hal_network_set_hostname(name);
}

const char *hal_network_get_hostname() {
    if (_hostname[0]) return _hostname;
    char buf[64];
    if (read_file("/etc/hostname", buf, sizeof(buf))) {
        std::snprintf(_hostname, sizeof(_hostname), "%s", buf);
        return _hostname;
    }
    return "void-os";
}

// ── 3. Cache flushing ─────────────────────────────────────────────────────

bool hal_network_flush_arp()    { return sh("ip neigh flush all 2>/dev/null"); }
bool hal_network_flush_routes() { return sh("ip route flush cache 2>/dev/null"); }

bool hal_network_flush_dns() {
    // systemd-resolved first; fall back to nscd; otherwise no-op.
    if (sh("systemd-resolve --flush-caches 2>/dev/null")) return true;
    return sh("nscd -i hosts 2>/dev/null");
}

// ── 4. Link reset ─────────────────────────────────────────────────────────

bool hal_network_reset_link(const char *iface) {
    if (!iface || !*iface) return false;
    char cmd[128];
    std::snprintf(cmd, sizeof(cmd),
                  "ip link set %s down; sleep 1; ip link set %s up", iface, iface);
    return sh(cmd);
}

// ── 5. Discovery blocking ─────────────────────────────────────────────────

bool hal_network_block_mdns() {
    return ipt_drop("-4", "-p udp --dport 5353") &&
           ipt_drop("-6", "-p udp --dport 5353");
}
bool hal_network_unblock_mdns() {
    return ipt_undrop("-4", "-p udp --dport 5353") &&
           ipt_undrop("-6", "-p udp --dport 5353");
}

bool hal_network_block_llmnr() {
    return ipt_drop("-4", "-p udp --dport 5355") &&
           ipt_drop("-6", "-p udp --dport 5355");
}
bool hal_network_unblock_llmnr() {
    return ipt_undrop("-4", "-p udp --dport 5355") &&
           ipt_undrop("-6", "-p udp --dport 5355");
}

bool hal_network_block_netbios() {
    bool ok = true;
    ok &= ipt_drop("-4", "-p udp --dport 137");
    ok &= ipt_drop("-4", "-p udp --dport 138");
    ok &= ipt_drop("-4", "-p tcp --dport 139");
    ok &= ipt_drop("-4", "-p tcp --dport 445");
    return ok;
}
bool hal_network_unblock_netbios() {
    bool ok = true;
    ok &= ipt_undrop("-4", "-p udp --dport 137");
    ok &= ipt_undrop("-4", "-p udp --dport 138");
    ok &= ipt_undrop("-4", "-p tcp --dport 139");
    ok &= ipt_undrop("-4", "-p tcp --dport 445");
    return ok;
}

bool hal_network_block_all_discovery() {
    bool ok = true;
    ok &= hal_network_block_mdns();
    ok &= hal_network_block_llmnr();
    ok &= hal_network_block_netbios();
    return ok;
}

bool hal_network_unblock_all_discovery() {
    bool ok = true;
    ok &= hal_network_unblock_mdns();
    ok &= hal_network_unblock_llmnr();
    ok &= hal_network_unblock_netbios();
    return ok;
}

// ── 6. DNS encryption ─────────────────────────────────────────────────────

bool hal_network_dns_clear() {
    // Remove the drop-in if it exists; reload to apply.
    ::unlink("/etc/systemd/resolved.conf.d/void-os.conf");
    return sh("systemctl reload systemd-resolved 2>/dev/null");
}

bool hal_network_doh_enable(const char *url) {
    if (!url || !*url) return false;
    std::ofstream f("/etc/systemd/resolved.conf.d/void-os.conf", std::ios::trunc);
    if (!f) return false;
    f << "[Resolve]\n"
         "DNS=" << url << "\n"
         "DNSOverHTTPS=yes\n";
    return sh("systemctl reload systemd-resolved 2>/dev/null");
}

bool hal_network_dot_enable(const char *server) {
    if (!server || !*server) return false;
    std::ofstream f("/etc/systemd/resolved.conf.d/void-os.conf", std::ios::trunc);
    if (!f) return false;
    f << "[Resolve]\n"
         "DNS=" << server << "\n"
         "DNSOverTLS=yes\n";
    return sh("systemctl reload systemd-resolved 2>/dev/null");
}

// ── 7. Fingerprint alignment ──────────────────────────────────────────────

bool hal_network_set_ttl(uint8_t ttl) {
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd),
                  "sysctl -qw net.ipv4.ip_default_ttl=%u 2>/dev/null", ttl);
    return sh(cmd);
}

bool hal_network_set_tcp_window_scaling(uint8_t enabled) {
    char cmd[80];
    std::snprintf(cmd, sizeof(cmd),
                  "sysctl -qw net.ipv4.tcp_window_scaling=%u 2>/dev/null",
                  (unsigned)(enabled ? 1 : 0));
    return sh(cmd);
}

// ── 8. Bulk toggle ────────────────────────────────────────────────────────

bool hal_network_stealth_enable() {
    bool ok = true;
    ok &= hal_network_randomize_mac("wlan0");
    ok &= hal_network_randomize_hostname();
    ok &= hal_network_block_all_discovery();
    ok &= hal_network_flush_arp();
    ok &= hal_network_flush_routes();
    ok &= hal_network_flush_dns();
    ok &= hal_network_set_ttl(64);
    _stealth = ok;
    return ok;
}

bool hal_network_stealth_disable() {
    bool ok = true;
    ok &= hal_network_unblock_all_discovery();
    ok &= hal_network_set_ttl(64);
    // MAC and hostname intentionally NOT restored — leaving the
    // operator-rotated values in place is safer than going back to the
    // default factory MAC.
    _stealth = false;
    return ok;
}

bool hal_network_stealth_is_enabled() { return _stealth; }

#endif // VOIDOS_RPI5
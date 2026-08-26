// hal_network.h — network-level device masking
//
// ───────────────────────────────────────────────────────────────────────────
//  PURPOSE
// ───────────────────────────────────────────────────────────────────────────
//
//  hal_network owns every "stop the rest of the LAN / the internet from
//  seeing us" knob. It is the only HAL that touches the kernel network
//  stack, iptables, /etc/systemd/resolved.conf, and the hostname.
//
//  The two adjacent HALs do NOT cover this surface:
//
//      * hal_wifi    — radio mode (monitor / station / AP) and the BLE
//                      stack. Per-channel MAC write through nl80211 is
//                      there; the generic interface-level MAC write is
//                      here.
//      * hal_mosfet  — RF / IR / haptic power switching through GPIOs.
//                      No network stack involvement.
//
//  Every function returns bool. A return of false means "the kernel or
//  iptables rejected the operation" — usually because the process lacks
//  CAP_NET_ADMIN. The caller is expected to log and continue; we never
//  abort the OS on a stealth-toggle failure.
//
// ───────────────────────────────────────────────────────────────────────────
//  PERMISSION POSTURE
// ───────────────────────────────────────────────────────────────────────────
//
//  All operations here require CAP_NET_ADMIN (or root). On a Pi 5 with
//  Raspberry Pi OS, the user account used to launch void-os must either
//  be root or have CAP_NET_ADMIN granted via systemd's
//  `AmbientCapabilities=` directive in the service unit. The skeleton
//  returns false instead of crashing when the permission is missing so
//  the rest of the OS keeps working.
//
//  iptables rules are stored in the `raw` table (PREROUTING / OUTPUT)
//  with comment "void-os" so a single iptables-save | grep void-os can
//  list every rule the OS added. To uninstall:
//
//      iptables-save | grep -v 'void-os' | iptables-restore
//
// ───────────────────────────────────────────────────────────────────────────
//  FUNCTION GROUPS
// ───────────────────────────────────────────────────────────────────────────
//
//  1. MAC randomization        — hal_network_randomize_mac / get_mac
//  2. Hostname randomization    — hal_network_randomize_hostname / set / get
//  3. Cache flushing           — hal_network_flush_arp / routes / dns
//  4. Link reset               — hal_network_reset_link
//  5. Discovery blocking        — block/unblock mDNS, LLMNR, NetBIOS
//  6. DNS encryption           — hal_network_doh_enable / dot_enable / clear
//  7. Fingerprint alignment    — hal_network_set_ttl / tcp_window
//  8. Bulk toggle              — hal_network_stealth_enable / disable / is_enabled
//
// ───────────────────────────────────────────────────────────────────────────
//  RESOURCE NOTES (Pi 5, 4 GB)
// ───────────────────────────────────────────────────────────────────────────
//
//   * iptables: each block_* writes one IPv4 + one IPv6 rule = 6 rules
//     per toggle. iptables-restore is ~5 ms; system(3) overhead is
//     ~2 ms; total wall-clock per toggle ≈ 60 ms.
//   * /dev/urandom reads: 16 bytes per call. Negligible.
//   * sysctl writes: ~1 ms each.
//   * Memory: 64-byte hostname buffer, no other state.
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include <stdint.h>
#include <stdbool.h>

// ── 1. MAC randomization ─────────────────────────────────────────────────
//
//  Writes a locally-administered MAC (bit 1 of byte 0 set, bit 0 clear)
//  with the remaining 5 bytes from /dev/urandom. The interface is brought
//  down, the address is written, and the interface is brought back up.
//
//  On Pi 5 the kernel accepts the write through
//      /sys/class/net/<iface>/address
//  only when the interface is down. The `ip link set address` path is
//  the fallback for kernels where sysfs refuses.
bool hal_network_randomize_mac(const char *iface);
bool hal_network_get_mac(const char *iface, uint8_t out[6]);

// ── 2. Hostname ───────────────────────────────────────────────────────────
//
//  sethostname(2) updates the kernel hostname AND (when systemd-hostnamed
//  is running) the pretty name. The DHCP client reads
//  /etc/hostname at every lease renewal, so a randomized hostname hides
//  the device from passive observers on the LAN.
//
//  The random hostname is 8 lowercase hex chars prefixed with "vd-" so
//  it is recognisably void-os-generated on packet captures.
bool hal_network_set_hostname(const char *hostname);
bool hal_network_randomize_hostname();   // returns the generated name
const char *hal_network_get_hostname();  // current kernel hostname

// ── 3. Cache flushing ─────────────────────────────────────────────────────
//
//  Flushes any record that links the device to past activity on the LAN.
//
//      ARP   — `ip neigh flush all`  removes every learned L2 entry
//      routes— `ip route flush cache` removes the routing cache (NOT the
//               routing table — only the cached lookups)
//      DNS   — `systemd-resolve --flush-caches` or `nscd -i hosts`; on
//               a plain Pi 5 OS without a local cache, this is a no-op.
//
//  Each function returns true if the relevant tool was found and exited 0.
bool hal_network_flush_arp();
bool hal_network_flush_routes();
bool hal_network_flush_dns();

// ── 4. Link reset ─────────────────────────────────────────────────────────
//
//  `ip link set <iface> down && ip link set <iface> up`. Brings the
//  radio back into the associated state with a fresh DHCP lease, fresh
//  ARP, fresh routing cache. Use sparingly — the link takes 5–30 s to
//  reassociate.
bool hal_network_reset_link(const char *iface);

// ── 5. Discovery blocking (iptables raw table) ───────────────────────────
//
//  Each block_ writes two rules (IPv4 + IPv6) and stores the comment
//  "void-os-<feature>" so the operator can list them with iptables-save.
//  Each unblock_ removes the matching rules.
//
//  mDNS     — UDP/5353
//  LLMNR    — UDP/5355
//  NetBIOS  — UDP/137, UDP/138, TCP/139, TCP/445
//
//  block_all_discovery() is a convenience that calls all three.
bool hal_network_block_mdns();
bool hal_network_unblock_mdns();
bool hal_network_block_llmnr();
bool hal_network_unblock_llmnr();
bool hal_network_block_netbios();
bool hal_network_unblock_netbios();
bool hal_network_block_all_discovery();
bool hal_network_unblock_all_discovery();

// ── 6. DNS encryption ─────────────────────────────────────────────────────
//
//  systemd-resolved reads /etc/systemd/resolved.conf (and drop-ins in
//  /etc/systemd/resolved.conf.d/). The skeleton writes a drop-in
//  /etc/systemd/resolved.conf.d/void-os.conf and calls
//  `systemctl reload systemd-resolved`. The operator must have the
//  file system mounted read-write.
//
//  DoH server format: "https://1.1.1.1/dns-query"
//  DoT server format: "1.1.1.1"  (with DNSOverTLS=yes)
//  Plain: clears the drop-in and reloads.
//
//  The skeleton does not validate the URL — systemd-resolved does.
bool hal_network_doh_enable(const char *url);
bool hal_network_dot_enable(const char *server);
bool hal_network_dns_clear();

// ── 7. Fingerprint alignment ──────────────────────────────────────────────
//
//  sysctl writes. The defaults on a Pi 5 are:
//
//      net.ipv4.ip_default_ttl      = 64
//      net.ipv4.tcp_window_scaling  = 1
//
//  Many desktop OSes use TTL=128 (Windows) or TTL=255 (some routers);
//  void-os aligns to 64 by default. Operators can override via app_dark.
//
//  TCP window scaling cannot be set per-connection from userspace; the
//  kernel default is the closest knob. Setting it to 0 forces the legacy
//  64-KB window which is unusual and stands out, so the default keeps
//  it at 1.
bool hal_network_set_ttl(uint8_t ttl);
bool hal_network_set_tcp_window_scaling(uint8_t enabled);  // 0 or 1

// ── 8. Bulk toggle ────────────────────────────────────────────────────────
//
//  stealth_enable  does, in order:
//      1. randomize MAC on wlan0 (if present)
//      2. randomize hostname
//      3. block mDNS + LLMNR + NetBIOS
//      4. flush ARP + DNS + route cache
//      5. set TTL = 64
//
//  stealth_disable undoes everything in reverse.
//
//  is_enabled returns the cached state, not the live network state.
bool hal_network_stealth_enable();
bool hal_network_stealth_disable();
bool hal_network_stealth_is_enabled();
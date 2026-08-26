// hal_pcapng.cpp — PCAP-NG block writer (SKELETON).
//
// =====================================================================
//  PCAP-NG block layout (real impl needed)
// =====================================================================
//
//  Each block is:
//
//      block_type : uint32_t   (little-endian)
//      block_total_length : uint32_t
//      block_body : <type-specific bytes>
//      block_total_length : uint32_t   (repeated at end)
//
//  ── Section Header Block (0x0A0D0D0A) ──────────────────────────────
//
//      magic    : uint32_t  = 0x1A2B3C4D
//      version  : uint16_t  = 1
//      thiszone : int16_t   = 0 (UTC)
//      sigfigs  : uint32_t  = 0
//      snaplen  : uint32_t  = 0   (use interface defaults)
//      options  :<pad to 4-byte alignment>
//
//  ── Interface Description Block (0x00000001) ───────────────────────
//
//      link_type : uint16_t (e.g. 105 = IEEE 802.11)
//      reserved  : uint16_t
//      snaplen   : uint32_t (0 = default)
//      options   : <interface name string>, TS resolution, etc.
//
//  ── Enhanced Packet Block (0x00000006) ─────────────────────────────
//
//      interface_id : uint32_t = 0 (only one)
//      ts_high      : uint32_t (μs part)
//      ts_low       : uint32_t
//      captured_len : uint32_t
//      original_len : uint32_t
//      packet_data  : <captured bytes> (padded to 4-byte multiples)
//      options      : <flags, hash, comment>
//
//  ── Implementation steps ────────────────────────────────────────────
//
//  1. `pcapng_open`: write the Section Header Block. Total length
//     depends on options; ~28 bytes for the minimum useful form.
//
//  2. Append an Interface Description Block to declare the link
//     type (LL_TYPE_IEEE802_11 = 105 for monitor-mode Wi-Fi, or
//     LINKTYPE_RAW = 101 for the raw 802.11+RFC1042 path).
//
//  3. On each captured frame, call `pcapng_emit_packet` which:
//     - Builds the Enhanced Packet Block inline.
//     - Computes block_total_length = 32 (header) + packet_bytes
//       (rounded up to 4-byte boundary).
//     - Repeats block_total_length at the end.
//
//  4. Close on EOF. Wireshark/tshark/libpcap readers handle the
//     format natively.
//
//  ── LIBRARY ALTERNATIVES ─────────────────────────────────────────────
//
//  * libpcap's `pcap_dump_open` writes classic PCAP, not PCAP-NG.
//    For PCAP-NG use `pcap_set_fileno` + `pcap_dump` only after
//    manually prepending the SHB.
//  * Wireshark's `wiretap` library, but it's a runtime dep not a
//    writing library — not usable from the device.
//  * `pcapng-tools/python-pcapng` for offline post-processing.
//  * The pure-C `pcapnglib` (LGPL v2.1) is a possible single-file
//    in-tree vendoring target at ~1.2 KB per block.
//
//  =====================================================================

#include "hal_pcapng.h"
#ifdef VOIDOS_RPI5
#include <cstdio>
#include <cstdlib>
FILE *_pcapng_f = nullptr;
#else
#endif
static uint8_t _pad[4] = {0};

bool pcapng_open(const char *path) {
#ifdef VOIDOS_RPI5
    _pcapng_f = std::fopen(path, "wb");
    return _pcapng_f != nullptr;
#else
    return false;
#endif
}
void pcapng_emit_packet(const uint8_t *data, size_t len, uint32_t ts_us) {
    (void)data; (void)len; (void)ts_us; (void)_pad;
}
void pcapng_close() {
#ifdef VOIDOS_RPI5
    if (_pcapng_f) { std::fclose(_pcapng_f); _pcapng_f = nullptr; }
#else
#endif
}

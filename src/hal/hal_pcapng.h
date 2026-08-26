// hal_pcapng.h — PCAP-NG export format.
//
// PCAP-NG is the modern wire-capture container (libpcap >= 1.5). Each
// file is a sequence of typed blocks (Section Header, Interface
// Description, Enhanced Packet, …) with a 32-bit little-endian
// block-type tag.
//
// The skeleton below explains the block layout, writes a Section
// Header Block, and stops. Real impl: append block writers and a
// packet capture loop.

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Open a `.pcapng` file and write its Section Header Block.
// Returns an opaque handle (NULL on failure). State path: /loot/...
bool pcapng_open(const char *path);

// Append an Enhanced Packet Block. `data` is the link-layer payload;
// `len` is its byte-length.
void    pcapng_emit_packet(const uint8_t *data, size_t len, uint32_t ts_us);
void    pcapng_close();

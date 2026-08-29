// hal_sdcard.h — single point of truth for on-card persistence.
// POSIX-style surface over /mnt/void-os (Pi5) / SD lib (ESP32), fallback NVS.
// Keeps <8 concurrent opens; use 4-byte-aligned streaming writes.

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Lifecycle -----------------------------------------------------------------
void   hal_sdcard_init();
bool   hal_sdcard_available();          // true if /mnt/void-os is mounted
void   hal_sdcard_tick();               // background sync / cache flush

// File I/O ------------------------------------------------------------------
// All paths are interpreted as relative to the SD mount point and must not
// contain ".." segments. Returns NULL on failure.
void  *hal_sdcard_open(const char *path, bool write);  // returns FILE*-like
bool   hal_sdcard_close(void *handle);
size_t hal_sdcard_write(void *handle, const void *buf, size_t len);
size_t hal_sdcard_read (void *handle, void *buf, size_t len);
bool   hal_sdcard_seek (void *handle, uint32_t offset);
bool   hal_sdcard_truncate(void *handle, uint32_t length);
bool   hal_sdcard_exists(const char *path);
bool   hal_sdcard_remove(const char *path);
bool   hal_sdcard_mkdir (const char *path);

// Directory walk ------------------------------------------------------------
// Returns the next entry name into `out` (null-terminated, max `out_len`).
// Call hal_sdcard_dir_close() when done. Returns false at end-of-directory
// or on error.
void  *hal_sdcard_dir_open(const char *path);
bool   hal_sdcard_dir_next(void *handle, char *out, size_t out_len);
void   hal_sdcard_dir_close(void *handle);

// Wordlist streaming --------------------------------------------------------
// Wordlists may not fit in RAM (the bundled common-credentials file is
// 14 MB and the WPA PSK dictionary is ~600 MB). Stream one line at a time
// into a caller-provided buffer. Returns false at EOF or error.
void  *hal_sdcard_wordlist_open(const char *path);
bool   hal_sdcard_wordlist_next(void *handle, char *line, size_t line_len);
void   hal_sdcard_wordlist_close(void *handle);

// Filesystem accounting -----------------------------------------------------
uint32_t hal_sdcard_total_bytes();
uint32_t hal_sdcard_free_bytes();
uint8_t  hal_sdcard_percent_used();
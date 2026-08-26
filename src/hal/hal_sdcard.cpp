// hal_sdcard.cpp — skeleton. See hal_sdcard.h for the full API + roadmap.
//
// Base behaviour: every call returns the "no card" failure code. The real
// driver (mount(2), mkdir_p, FAT file I/O) is documented inline at the top
// of hal_sdcard.h and must be added before any field deployment.

#include "hal_sdcard.h"

static bool _mounted = false;

void     hal_sdcard_init()          { _mounted = false; }
bool     hal_sdcard_available()     { return _mounted; }
void     hal_sdcard_tick()          {}

void    *hal_sdcard_open(const char *, bool) { return nullptr; }
bool     hal_sdcard_close(void *)            { return false; }
size_t   hal_sdcard_write(void *, const void *, size_t) { return 0; }
size_t   hal_sdcard_read (void *, void *, size_t)       { return 0; }
bool     hal_sdcard_seek    (void *, uint32_t) { return false; }
bool     hal_sdcard_truncate(void *, uint32_t) { return false; }
bool     hal_sdcard_exists  (const char *)     { return false; }
bool     hal_sdcard_remove  (const char *)     { return false; }
bool     hal_sdcard_mkdir   (const char *)     { return false; }

void    *hal_sdcard_dir_open(const char *)    { return nullptr; }
bool     hal_sdcard_dir_next(void *, char *, size_t) { return false; }
void     hal_sdcard_dir_close(void *)         {}

void    *hal_sdcard_wordlist_open (const char *) { return nullptr; }
bool     hal_sdcard_wordlist_next (void *, char *, size_t) { return false; }
void     hal_sdcard_wordlist_close(void *)       {}

uint32_t hal_sdcard_total_bytes()  { return 0; }
uint32_t hal_sdcard_free_bytes()   { return 0; }
uint8_t  hal_sdcard_percent_used() { return 0; }
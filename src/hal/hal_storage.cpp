// hal_storage.cpp
#include "hal_storage.h"
#include "config.h"
#include <Preferences.h>

static Preferences _prefs;

void hal_storage_init() {
    _prefs.begin(NVS_NS, false);   // false = read-write
}

bool hal_storage_set_u8(const char *k, uint8_t v) {
    return _prefs.putUChar(k, v) == sizeof(v);
}
bool hal_storage_set_u32(const char *k, uint32_t v) {
    return _prefs.putUInt(k, v) == sizeof(v);
}
bool hal_storage_set_str(const char *k, const char *v) {
    return _prefs.putString(k, v) > 0;
}
bool hal_storage_set_blob(const char *k, const void *d, size_t l) {
    return _prefs.putBytes(k, d, l) == l;
}
uint8_t  hal_storage_get_u8 (const char *k, uint8_t  d) { return _prefs.getUChar(k, d); }
uint32_t hal_storage_get_u32(const char *k, uint32_t d) { return _prefs.getUInt(k, d);  }
bool hal_storage_get_str(const char *k, char *buf, size_t l) {
    String s = _prefs.getString(k, "");
    if (s.length() == 0) return false;
    strncpy(buf, s.c_str(), l); buf[l-1]=0; return true;
}
bool hal_storage_get_blob(const char *k, void *buf, size_t *l) {
    size_t n = _prefs.getBytes(k, buf, *l);
    *l = n; return n > 0;
}
void hal_storage_commit() { _prefs.end(); _prefs.begin(NVS_NS, false); }
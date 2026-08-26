// hal_storage.cpp
#include "hal_storage.h"
#include "config.h"

#ifdef VOIDOS_RPI5
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::filesystem::path storage_dir;
std::filesystem::path path_for(const char *key) {
    std::string safe;
    for (const char *p = key ? key : ""; *p; ++p)
        safe += (*p == '/' || *p == '\\') ? '_' : *p;
    return storage_dir / safe;
}
}

void hal_storage_init() {
    const char *state = std::getenv("XDG_STATE_HOME");
    if (state && *state) storage_dir = std::filesystem::path(state) / "void-os";
    else {
        const char *home = std::getenv("HOME");
        storage_dir = std::filesystem::path(home && *home ? home : ".") /
                      ".local" / "state" / "void-os";
    }
    std::error_code ec;
    std::filesystem::create_directories(storage_dir, ec);
}

bool hal_storage_set_u8(const char *k, uint8_t v) { return hal_storage_set_blob(k, &v, sizeof(v)); }
bool hal_storage_set_u32(const char *k, uint32_t v) { return hal_storage_set_blob(k, &v, sizeof(v)); }
bool hal_storage_set_str(const char *k, const char *v) {
    std::ofstream out(path_for(k), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (v) out.write(v, static_cast<std::streamsize>(std::strlen(v)));
    return static_cast<bool>(out);
}
bool hal_storage_set_blob(const char *k, const void *data, size_t len) {
    if (!data && len) return false;
    std::ofstream out(path_for(k), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
    return static_cast<bool>(out);
}

uint8_t hal_storage_get_u8(const char *k, uint8_t def) {
    uint8_t value = def; size_t len = sizeof(value);
    return hal_storage_get_blob(k, &value, &len) && len == sizeof(value) ? value : def;
}
uint32_t hal_storage_get_u32(const char *k, uint32_t def) {
    uint32_t value = def; size_t len = sizeof(value);
    return hal_storage_get_blob(k, &value, &len) && len == sizeof(value) ? value : def;
}
bool hal_storage_get_str(const char *k, char *buf, size_t len) {
    if (!buf || len == 0) return false;
    std::ifstream in(path_for(k), std::ios::binary);
    if (!in) { buf[0] = 0; return false; }
    in.read(buf, static_cast<std::streamsize>(len - 1));
    const std::streamsize count = in.gcount();
    buf[count > 0 ? count : 0] = 0;
    return count > 0;
}
bool hal_storage_get_blob(const char *k, void *buf, size_t *len) {
    if (!buf || !len) return false;
    std::ifstream in(path_for(k), std::ios::binary);
    if (!in) { *len = 0; return false; }
    const std::size_t capacity = *len;
    in.read(static_cast<char *>(buf), static_cast<std::streamsize>(capacity));
    *len = static_cast<std::size_t>(in.gcount());
    return *len > 0;
}
void hal_storage_commit() {}

#else

#include <Preferences.h>
#include <cstring>

static Preferences _prefs;

void hal_storage_init() { _prefs.begin(NVS_NS, false); }
bool hal_storage_set_u8(const char *k, uint8_t v) { return _prefs.putUChar(k, v) == sizeof(v); }
bool hal_storage_set_u32(const char *k, uint32_t v) { return _prefs.putUInt(k, v) == sizeof(v); }
bool hal_storage_set_str(const char *k, const char *v) { return _prefs.putString(k, v) > 0; }
bool hal_storage_set_blob(const char *k, const void *d, size_t l) { return _prefs.putBytes(k, d, l) == l; }
uint8_t hal_storage_get_u8(const char *k, uint8_t d) { return _prefs.getUChar(k, d); }
uint32_t hal_storage_get_u32(const char *k, uint32_t d) { return _prefs.getUInt(k, d); }
bool hal_storage_get_str(const char *k, char *buf, size_t l) {
    String s = _prefs.getString(k, "");
    if (s.length() == 0 || l == 0) return false;
    strncpy(buf, s.c_str(), l); buf[l - 1] = 0; return true;
}
bool hal_storage_get_blob(const char *k, void *buf, size_t *l) {
    size_t n = _prefs.getBytes(k, buf, *l); *l = n; return n > 0;
}
void hal_storage_commit() { _prefs.end(); _prefs.begin(NVS_NS, false); }

#endif

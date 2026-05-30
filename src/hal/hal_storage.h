// hal_storage.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <cstddef>

void     hal_storage_init();
bool     hal_storage_set_u8 (const char *key, uint8_t val);
bool     hal_storage_set_u32(const char *key, uint32_t val);
bool     hal_storage_set_str(const char *key, const char *val);
bool     hal_storage_set_blob(const char *key,
                               const void *data, size_t len);
uint8_t  hal_storage_get_u8 (const char *key, uint8_t  def);
uint32_t hal_storage_get_u32(const char *key, uint32_t def);
bool     hal_storage_get_str (const char *key, char *buf, size_t len);
bool     hal_storage_get_blob(const char *key,
                               void *buf, size_t *len);
void     hal_storage_commit();
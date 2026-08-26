// hal_crypto.h — at-rest encryption for loot.
//
// The captured loot directory contains login credentials, cookies,
// and URLs that smoke-gun the device's owner if the SD card pops.
// This module provides real at-rest encryption (AES-256-CBC) with a
// passphrase-derived key. On the Raspberry Pi / Linux target it is
// backed by OpenSSL libcrypto; the ESP32 target has no vetted
// primitive wired in yet and reports unavailable (returns false)
// instead of claiming success.
//
// FILE FORMAT (matches the documented layout):
//
//      SIG 4 bytes "VOS1"
//      SALT 16 bytes
//      IV   16 bytes
//      CIPHERTEXT (multiple of 16 bytes)
//      HMAC 32 bytes (SHA256 over SIG+SALT+IV+CIPHERTEXT)
//
// Encrypting a file produces a sibling "<name>.vos1" and removes the
// plaintext once the ciphertext is durably written, so no plaintext
// lingers on disk.

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Human-readable magic written at offset 0 of every ciphertext file.
#define VOS1_SIG "VOS1"
#define VOS1_SIG_LEN 4
#define VOS1_SALT_LEN 16
#define VOS1_IV_LEN 16
#define VOS1_HMAC_LEN 32
#define VOS1_PBKDF2_ITERS 100000

// Configure the operator passphrase used for key derivation.
// Returns false (and leaves crypto disabled) if the passphrase is
// empty, so we never silently "encrypt" with a blank key.
bool hal_crypto_init(const char *kdf_password);

// Enable/disable the at-rest encryption pass. Disabling is safe: it
// only stops new encryptions; already-encrypted files stay encrypted.
void hal_crypto_enable(bool on);

// True iff `init` or `enable` turned the encryption pass on and the
// underlying primitive is actually available on this target.
bool hal_crypto_enabled();

// Encrypt the file at `path` in place: reads it, derives a per-file
// key, AES-256-CBC encrypts with a random IV, writes "<path>.vos1",
// fsyncs, then deletes the plaintext. Returns true only if the
// ciphertext was fully written and the plaintext removed. A false
// return leaves the plaintext untouched (caller decides whether to
// delete it).
bool hal_crypto_process(const char *path);

// Decrypt `<input>` (a .vos1 file) back to plaintext at `output`.
// Returns false on any failure or on an HMAC mismatch (tampered file
// or wrong passphrase).
bool hal_crypto_decrypt_file(const char *input, const char *output);

// Return true iff `path` already carries the VOS1 magic signature.
bool hal_crypto_is_encrypted(const char *path);

// Encryption pump: walk the loot/capture directory and encrypt any
// plaintext (`.csv` / `.md`) files that aren't already encrypted.
// Counts how many files were encrypted and stores it in *out_encrypted
// (may be NULL). Returns false if crypto is unavailable/disabled.
//
// By design this is the recommended way to integrate with streaming
// writers: writers keep writing plaintext during capture, and a
// periodic pump encrypts finished files so no plaintext is left
// behind on disk.
bool hal_crypto_pass(const char *dir_path, int *out_encrypted);
// hal_crypto.cpp — at-rest encryption for loot.
//
// Real implementation (Raspberry Pi / Linux target): backed by OpenSSL
// libcrypto. Key derivation is PBKDF2-HMAC-SHA256 (100,000 iters) from
// the operator passphrase and a random 16-byte per-file salt. Payload
// is encrypted with AES-256-CBC using a random 16-byte IV. An HMAC-SHA256
// over the header + ciphertext is appended so tampering or a wrong
// passphrase is detected on decrypt.
//
// Format (matches hal_crypto.h):
//      SIG "VOS1" | SALT(16) | IV(16) | CIPHERTEXT | HMAC(32)
//
// ESP32 target: no vetted primitive is wired in yet, so every operation
// returns false (honest "unavailable") rather than claiming success the
// way the old skeleton did.

#include "hal_crypto.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef VOIDOS_RPI5
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#endif

static bool _enabled = false;
static char _passphrase[64] = {0};

bool hal_crypto_init(const char *kdf_password) {
    if (!kdf_password || !kdf_password[0]) {
        _enabled = false;
        return false;
    }
    std::snprintf(_passphrase, sizeof(_passphrase), "%s", kdf_password);
    _enabled = true;
    return true;
}

void hal_crypto_enable(bool on) { _enabled = on; }

bool hal_crypto_enabled() {
#ifdef VOIDOS_RPI5
    return _enabled;
#else
    // No vetted primitive wired in on this target yet.
    return false;
#endif
}

#ifdef VOIDOS_RPI5

namespace {

// ── helpers ──────────────────────────────────────────────────────────

// Read a whole file into `out`. Returns true on success.
bool read_whole(const std::string &path, std::string &out) {
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::rewind(fp);
    if (n < 0) { std::fclose(fp); return false; }
    out.resize(static_cast<size_t>(n));
    bool ok = n == 0 || std::fread(&out[0], 1, static_cast<size_t>(n), fp) == static_cast<size_t>(n);
    std::fclose(fp);
    return ok;
}

// Write `data` to `path` durably (fsync before close). Returns true iff OK.
bool write_durable(const std::string &path, const std::string &data) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    size_t off = 0, len = data.size();
    while (off < len) {
        ssize_t w = ::write(fd, data.data() + off, len - off);
        if (w <= 0) { ::close(fd); ::unlink(path.c_str()); return false; }
        off += static_cast<size_t>(w);
    }
    bool ok = (::fsync(fd) == 0);
    if (::close(fd) != 0) ok = false;
    if (!ok) ::unlink(path.c_str());
    return ok;
}

// Derive the per-file 256-bit key from passphrase + salt.
bool derive_key(const uint8_t *salt, size_t salt_len, uint8_t key[32]) {
    return PKCS5_PBKDF2_HMAC(_passphrase, (int)std::strlen(_passphrase),
                             salt, (int)salt_len, VOS1_PBKDF2_ITERS,
                             EVP_sha256(), 32, key) == 1;
}

bool hmac_of(const uint8_t *begin, size_t len, uint8_t out[32]) {
    unsigned int out_len = 0;
    HMAC(EVP_sha256(), _passphrase, (int)std::strlen(_passphrase),
         begin, len, out, &out_len);
    return out_len == VOS1_HMAC_LEN;
}

bool constant_time_eq(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc |= (uint8_t)(a[i] ^ b[i]);
    return acc == 0;
}

// PKCS#7 pad to a multiple of 16 (always adds a full pad block if the
// input is already aligned, so the ciphertext length is unambiguous).
void pkcs7_pad(const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len) {
    size_t pad = 16 - (in_len % 16);
    std::memcpy(out, in, in_len);
    for (size_t i = 0; i < pad; ++i) out[in_len + i] = (uint8_t)pad;
    *out_len = in_len + pad;
}

bool aes256cbc_encrypt(const uint8_t key[32], const uint8_t iv[16],
                       const uint8_t *pt, size_t pt_len,
                       uint8_t *ct, size_t *ct_len) {
    size_t padded_len = pt_len + (16 - pt_len % 16);
    uint8_t *padded = new uint8_t[padded_len ? padded_len : 16];
    size_t padded_sz = 0;
    pkcs7_pad(pt, pt_len, padded, &padded_sz);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { delete[] padded; return false; }
    int out0 = 0, out1 = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv) == 1 &&
              EVP_EncryptUpdate(ctx, ct, &out0, padded, (int)padded_sz) == 1 &&
              EVP_EncryptFinal_ex(ctx, ct + out0, &out1) == 1;
    *ct_len = (size_t)(out0 + out1);
    EVP_CIPHER_CTX_free(ctx);
    delete[] padded;
    return ok;
}

bool aes256cbc_decrypt(const uint8_t key[32], const uint8_t iv[16],
                       const uint8_t *ct, size_t ct_len,
                       uint8_t *pt, size_t *pt_len) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    int out0 = 0, out1 = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv) == 1 &&
              EVP_DecryptUpdate(ctx, pt, &out0, ct, (int)ct_len) == 1;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, pt + out0, &out1) == 1;
    *pt_len = ok ? (size_t)(out0 + out1) : 0;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

} // namespace

bool hal_crypto_process(const char *path) {
    if (!path || !_enabled) return false;

    std::string plain;
    if (!read_whole(path, plain)) return false;

    // Non-empty input required.
    if (plain.empty()) return false;

    // Per-file random salt + IV.
    uint8_t salt[VOS1_SALT_LEN], iv[VOS1_IV_LEN];
    if (RAND_bytes(salt, sizeof(salt)) != 1) return false;
    if (RAND_bytes(iv, sizeof(iv)) != 1) return false;

    uint8_t key[32];
    if (!derive_key(salt, sizeof(salt), key)) return false;

    size_t ct_cap = plain.size() + 32;
    std::string ct(ct_cap, '\0');
    size_t ct_len = 0;
    if (!aes256cbc_encrypt(key, iv,
                           reinterpret_cast<const uint8_t *>(plain.data()), plain.size(),
                           reinterpret_cast<uint8_t *>(&ct[0]), &ct_len))
        return false;

    // Assemble: SIG | SALT | IV | CIPHERTEXT | HMAC
    std::string out(VOS1_SIG_LEN + VOS1_SALT_LEN + VOS1_IV_LEN + ct_len + VOS1_HMAC_LEN, '\0');
    std::memcpy(&out[0], VOS1_SIG, VOS1_SIG_LEN);
    std::memcpy(&out[VOS1_SIG_LEN], salt, VOS1_SALT_LEN);
    std::memcpy(&out[VOS1_SIG_LEN + VOS1_SALT_LEN], iv, VOS1_IV_LEN);
    std::memcpy(&out[VOS1_SIG_LEN + VOS1_SALT_LEN + VOS1_IV_LEN], ct.data(), ct_len);
    hmac_of(reinterpret_cast<const uint8_t *>(out.data()),
            VOS1_SIG_LEN + VOS1_SALT_LEN + VOS1_IV_LEN + ct_len,
            reinterpret_cast<uint8_t *>(&out[out.size() - VOS1_HMAC_LEN]));

    std::string out_path = std::string(path) + ".vos1";
    if (!write_durable(out_path, out)) return false;

    // Plaintext is deleted only after the ciphertext is durably on disk.
    if (::unlink(path) != 0) {
        // Best-effort cleanup of the ciphertext so we never have an
        // orphan that looks encrypted but has no plaintext source.
        ::unlink(out_path.c_str());
        return false;
    }
    return true;
}

bool hal_crypto_decrypt_file(const char *input, const char *output) {
    if (!input || !output) return false;

    std::string blob;
    if (!read_whole(input, blob)) return false;

    const size_t hdr = VOS1_SIG_LEN + VOS1_SALT_LEN + VOS1_IV_LEN;
    if (blob.size() < hdr + VOS1_HMAC_LEN + 16) return false;   // too small
    if (std::memcmp(blob.data(), VOS1_SIG, VOS1_SIG_LEN) != 0) return false;

    // Verify HMAC over everything before the trailing 32 bytes.
    uint8_t expected[VOS1_HMAC_LEN];
    std::memcpy(expected, blob.data() + blob.size() - VOS1_HMAC_LEN, VOS1_HMAC_LEN);
    uint8_t calc[VOS1_HMAC_LEN];
    if (!hmac_of(reinterpret_cast<const uint8_t *>(blob.data()),
                 blob.size() - VOS1_HMAC_LEN, calc))
        return false;
    if (!constant_time_eq(calc, expected, VOS1_HMAC_LEN)) return false;  // tampered / wrong pass

    const uint8_t *salt = reinterpret_cast<const uint8_t *>(blob.data()) + VOS1_SIG_LEN;
    const uint8_t *iv   = salt + VOS1_SALT_LEN;
    const uint8_t *ct   = iv + VOS1_IV_LEN;
    size_t ct_len = blob.size() - hdr - VOS1_HMAC_LEN;

    uint8_t key[32];
    if (!derive_key(salt, VOS1_SALT_LEN, key)) return false;

    std::string pt(ct_len, '\0');
    size_t pt_len = 0;
    if (!aes256cbc_decrypt(key, iv, ct, ct_len,
                           reinterpret_cast<uint8_t *>(&pt[0]), &pt_len))
        return false;

    // PKCS#7 strip. The last byte is the pad length (1..16); validate that
    // all trailing bytes equal it before trusting, so a malformed ciphertext
    // is rejected by the length check rather than silently truncated.
    if (pt_len == 0) return false;
    size_t pad = static_cast<uint8_t>(pt[pt_len - 1]);
    if (pad == 0 || pad > 16 || pad > pt_len) return false;
    for (size_t i = pt_len - pad; i < pt_len; ++i)
        if (static_cast<uint8_t>(pt[i]) != pad) return false;
    pt.resize(pt_len - pad);

    return write_durable(output, pt);
}

bool hal_crypto_is_encrypted(const char *path) {
    if (!path) return false;
    std::string head(VOS1_SIG_LEN, '\0');
    FILE *fp = std::fopen(path, "rb");
    if (!fp) return false;
    size_t got = std::fread(&head[0], 1, VOS1_SIG_LEN, fp);
    std::fclose(fp);
    return got == VOS1_SIG_LEN && std::memcmp(head.data(), VOS1_SIG, VOS1_SIG_LEN) == 0;
}

bool hal_crypto_pass(const char *dir_path, int *out_encrypted) {
    if (!dir_path || !_enabled) {
        if (out_encrypted) *out_encrypted = 0;
        return false;
    }

    DIR *d = ::opendir(dir_path);
    if (!d) {
        if (out_encrypted) *out_encrypted = 0;
        return false;
    }

    int encrypted = 0;
    struct dirent *ent;
    while ((ent = ::readdir(d))) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;
        // Only sweep plaintext capture files.
        size_t n = std::strlen(name);
        bool is_csv = n > 4 && std::strcmp(name + n - 4, ".csv") == 0;
        bool is_md  = n > 3 && std::strcmp(name + n - 3, ".md") == 0;
        if (!is_csv && !is_md) continue;
        if (n > 5 && std::strcmp(name + n - 5, ".vos1") == 0) continue;

        std::string full = std::string(dir_path) + "/" + name;
        if (hal_crypto_is_encrypted(full.c_str())) continue;
        if (hal_crypto_process(full.c_str())) ++encrypted;
    }
    ::closedir(d);

    if (out_encrypted) *out_encrypted = encrypted;
    return _enabled;
}

#else  // !VOIDOS_RPI5 — ESP32 target, no primitive wired in yet.

bool hal_crypto_process(const char *path) {
    (void)path;
    // Honest "not available" — never claim encryption happened.
    return false;
}

bool hal_crypto_decrypt_file(const char *input, const char *output) {
    (void)input; (void)output;
    return false;
}

bool hal_crypto_is_encrypted(const char *path) {
    (void)path;
    return false;
}

bool hal_crypto_pass(const char *dir_path, int *out_encrypted) {
    (void)dir_path;
    if (out_encrypted) *out_encrypted = 0;
    return false;
}

#endif // VOIDOS_RPI5
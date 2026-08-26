// hal_pocsag.cpp — POCSAG pager decoder.
//
// Protocol notes (CCIR Rec 584 / ITU-R M.584):
//   * NRZ 2-FSK at 512/1200/2400 bps, high freq = 0, low = 1.
//   * Burst = preamble (>=576 alternating bits) + batches.
//   * Batch  = sync codeword 0x7CD215D8 + 16 codewords (8 frames of 2).
//   * Codeword = 21 data bits (bits 31-11), BCH(31,21) parity (bits 10-1),
//     even parity (bit 0). Generator x^10+x^9+x^8+x^6+x^5+x^3+1 (0x769).
//   * Bit 31: 0 = address codeword, 1 = message codeword.
//   * Address codeword: bits 30-13 = 18-bit address field, bits 12-11 =
//     function (0 numeric BCD, 3 alphanumeric 7-bit ASCII).
//   * Full 21-bit address = (field << 3) | frame number.
//   * Message ends at an idle codeword (0x7A89C197) or another address
//     in its frame. Messages span batches within the same frame.
//   * Note: POCSAG is NRZ — it does NOT use Manchester encoding.

#include "hal_pocsag.h"
#include <cstring>

#define POCSAG_SYNC  0x7CD215D8u
#define POCSAG_IDLE  0x7A89C197u
#define POCSAG_GEN   0x769u          // BCH generator polynomial

#define MSG_MAX_BITS 512             // 64 bytes of message text
#define MSG_RING     4               // completed-message queue

// CCIR numeric character set (4-bit BCD values 0x0..0xF).
static const char NUM_TABLE[16] = {
    '0','1','2','3','4','5','6','7','8','9','?','U',' ','-',')','('
};

struct FrameMsg {
    bool     active;
    uint32_t addr;
    uint8_t  func;
    uint32_t start_ms;
    uint16_t bits;
    uint8_t  data[MSG_MAX_BITS / 8];
};

enum DecState { ST_PREAMBLE, ST_SYNC, ST_BATCH };

static FrameMsg       _frames[8];            // one message per frame
static RadioPocsagMsg _ring[MSG_RING];
static uint8_t        _ring_head = 0, _ring_tail = 0, _ring_n = 0;

static DecState _state = ST_PREAMBLE;
static uint8_t  _alt_run = 0;                // consecutive alternating bits
static uint8_t  _prev_bit = 0;
static uint32_t _sr = 0;                     // sync search window
static uint32_t _cw = 0;                     // codeword being assembled
static uint8_t  _cw_bits = 0;
static uint8_t  _batch_idx = 0;
static uint8_t  _bad_cw = 0;                 // consecutive uncorrectable
static bool     _invert = false;             // auto-detected polarity
static uint32_t _last_activity_ms = 0;

// ── BCH(31,21) error correction ────────────────────────────────────────

static uint32_t bch_rem(uint32_t value) {
    for (int i = 30; i >= 10; --i)
        if (value & (1u << i)) value ^= POCSAG_GEN << (i - 10);
    return value & 0x3FFu;
}

static bool bch_valid(uint32_t cw) {
    if (bch_rem(cw >> 1) != 0) return false;
    uint32_t p = cw;                          // XOR of all 32 bits
    p ^= p >> 16; p ^= p >> 8; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
    return (p & 1u) == 0;
}

// Correct up to two bit errors. Returns false if uncorrectable.
static bool bch_correct(uint32_t *cw) {
    if (bch_valid(*cw)) return true;
    for (int i = 0; i < 32; ++i) {            // single-bit repair
        uint32_t t = *cw ^ (1u << i);
        if (bch_valid(t)) { *cw = t; return true; }
    }
    for (int i = 0; i < 31; ++i) {            // double-bit repair
        for (int j = i + 1; j < 32; ++j) {
            uint32_t t = *cw ^ (1u << i) ^ (1u << j);
            if (bch_valid(t)) { *cw = t; return true; }
        }
    }
    return false;
}

// Sync within one bit error (Hamming distance 1).
static bool near_sync(uint32_t v) {
    if (v == POCSAG_SYNC) return true;
    const uint32_t d = v ^ POCSAG_SYNC;
    return d && (d & (d - 1)) == 0;
}

// ── Message buffer helpers ─────────────────────────────────────────────

static uint8_t msg_get_bit(const FrameMsg *m, uint16_t i) {
    return (m->data[i >> 3] >> (7 - (i & 7))) & 1u;
}

static void msg_append_bits(FrameMsg *m, uint32_t data, uint8_t nbits) {
    for (int b = nbits - 1; b >= 0 && m->bits < MSG_MAX_BITS; --b) {
        if ((data >> b) & 1u) m->data[m->bits >> 3] |= (0x80u >> (m->bits & 7));
        ++m->bits;
    }
}

// ── Message lifecycle ───────────────────────────────────────────────────

static void msg_emit(FrameMsg *m) {
    if (_ring_n >= MSG_RING) {                // drop oldest, keep newest
        _ring_tail = (_ring_tail + 1) % MSG_RING;
        --_ring_n;
    }
    RadioPocsagMsg *out = &_ring[_ring_head];
    out->timestamp_ms = m->start_ms;
    out->address = m->addr;
    out->function = m->func;
    char *t = out->text;
    uint16_t i = 0;
    if (m->func == 0) {                       // numeric BCD
        uint16_t nibbles = m->bits / 4;
        for (; i < nibbles && i < 79; ++i) {
            uint8_t nib = 0;
            for (int b = 0; b < 4; ++b) nib = static_cast<uint8_t>((nib << 1) | msg_get_bit(m, i * 4 + b));
            t[i] = NUM_TABLE[nib & 0xFu];
        }
        t[i] = '\0';
        while (i > 0 && t[i - 1] == ' ') t[--i] = '\0';   // strip padding
    } else if (m->func == 3) {                // alphanumeric 7-bit ASCII
        uint16_t chars = m->bits / 7;
        for (; i < chars && i < 79; ++i) {
            uint8_t c = 0;
            for (int b = 0; b < 7; ++b) c = static_cast<uint8_t>(c | (msg_get_bit(m, i * 7 + b) << b));
            if (c == 0 || c == 0x03 || c == 0x04) break;   // NUL/ETX/EOT
            t[i] = static_cast<char>(c);
        }
        t[i] = '\0';
    } else {                                  // binary: hex dump
        static const char HEX[] = "0123456789ABCDEF";
        uint16_t bytes = m->bits / 8;
        for (uint16_t k = 0; k < bytes && i < 78; ++k) {
            t[i++] = HEX[m->data[k] >> 4];
            t[i++] = HEX[m->data[k] & 0xFu];
        }
        t[i] = '\0';
    }
    _ring_head = (_ring_head + 1) % MSG_RING;
    ++_ring_n;
}

static void end_msg(uint8_t frame) {
    FrameMsg *m = &_frames[frame];
    if (m->active) {
        m->active = false;
        msg_emit(m);
    }
}

static void start_msg(uint8_t frame, uint32_t addr, uint8_t func, uint32_t now_ms) {
    FrameMsg *m = &_frames[frame];
    end_msg(frame);                           // replace any existing message
    m->active = true;
    m->addr = addr;
    m->func = func;
    m->start_ms = now_ms;
    m->bits = 0;
    std::memset(m->data, 0, sizeof(m->data));
}

// ── Codeword processing ────────────────────────────────────────────────

static void process_cw(uint32_t cw, uint32_t now_ms) {
    if (!bch_correct(&cw)) {
        if (++_bad_cw >= 4) {                 // too many errors, resync
            _bad_cw = 0;
            _batch_idx = 0;
            _cw_bits = 0;
            _state = ST_PREAMBLE;
            _alt_run = 0;
        }
        return;
    }
    _bad_cw = 0;
    const uint8_t frame = static_cast<uint8_t>(_batch_idx >> 1);
    if (cw == POCSAG_IDLE) {
        end_msg(frame);
    } else if (cw & 0x80000000u) {            // message codeword
        FrameMsg *m = &_frames[frame];
        if (m->active) msg_append_bits(m, cw >> 11, 20);
    } else {                                  // address codeword
        const uint32_t addr = ((cw >> 10) & 0x1FFFF8u) | frame;
        const uint8_t func = static_cast<uint8_t>((cw >> 11) & 3u);
        start_msg(frame, addr, func, now_ms);
    }
}

// ── Public API ─────────────────────────────────────────────────────────

void hal_pocsag_reset() {
    _state = ST_PREAMBLE;
    _alt_run = 0;
    _prev_bit = 0;
    _sr = 0;
    _cw = 0;
    _cw_bits = 0;
    _batch_idx = 0;
    _bad_cw = 0;
    _invert = false;
    _last_activity_ms = 0;
    std::memset(_frames, 0, sizeof(_frames));
    _ring_head = _ring_tail = _ring_n = 0;
}

void hal_pocsag_feed_bit(uint8_t bit, uint32_t now_ms) {
    // Flush messages left dangling when the carrier dropped.
    if (_last_activity_ms && now_ms > _last_activity_ms + 2000) {
        for (int f = 0; f < 8; ++f) end_msg(static_cast<uint8_t>(f));
        _state = ST_PREAMBLE;
        _alt_run = 0;
        _batch_idx = 0;
        _cw_bits = 0;
    }
    _last_activity_ms = now_ms;

    switch (_state) {
    case ST_PREAMBLE:
        if (bit != _prev_bit) {
            if (++_alt_run >= 16) { _state = ST_SYNC; _sr = 0; }
        } else {
            _alt_run = 0;
        }
        _prev_bit = bit;
        break;

    case ST_SYNC:
        _sr = (_sr << 1) | bit;
        if (near_sync(_sr)) {
            _invert = false;
            _state = ST_BATCH;
            _batch_idx = 0;
            _cw_bits = 0;
            _bad_cw = 0;
            _cw = 0;
        } else if (near_sync(~_sr)) {         // inverted polarity
            _invert = true;
            _state = ST_BATCH;
            _batch_idx = 0;
            _cw_bits = 0;
            _bad_cw = 0;
            _cw = 0;
        }
        break;

    case ST_BATCH: {
        const uint8_t b = _invert ? static_cast<uint8_t>(bit ^ 1) : bit;
        _cw = (_cw << 1) | b;
        if (++_cw_bits == 32) {
            _cw_bits = 0;
            process_cw(_cw, now_ms);
            _cw = 0;
            if (++_batch_idx == 16) {         // batch done, expect sync
                _batch_idx = 0;
                _state = ST_SYNC;
                _sr = 0;
            }
        }
        break;
    }
    }
}

bool hal_pocsag_pop(RadioPocsagMsg *out) {
    if (!out || _ring_n == 0) return false;
    *out = _ring[_ring_tail];
    _ring_tail = (_ring_tail + 1) % MSG_RING;
    --_ring_n;
    return true;
}

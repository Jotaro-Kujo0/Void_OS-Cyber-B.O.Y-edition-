// hal_haptic.cpp — Coin vibration motor driver (Raspberry Pi 5 via sysfs PWM)
//
// PWM path: /sys/class/pwm/pwmchip0/pwm0/
// Requires: dtoverlay=pwm in /boot/firmware/config.txt

#include "hal_haptic.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

// ─── Sysfs PWM paths ──────────────────────────────────────────────────
static const char* PWM_EXPORT   = "/sys/class/pwm/pwmchip0/export";
static const char* PWM_PERIOD   = "/sys/class/pwm/pwmchip0/pwm0/period";
static const char* PWM_DUTY     = "/sys/class/pwm/pwmchip0/pwm0/duty_cycle";
static const char* PWM_ENABLE   = "/sys/class/pwm/pwmchip0/pwm0/enable";

// ─── Timing constants ─────────────────────────────────────────────────
static const uint32_t PWM_PERIOD_NS    = 2000000;  // 500 Hz
static const uint32_t PWM_DUTY_FULL    = 2000000;  // 100%
static const uint32_t PWM_DUTY_OFF     = 0;

static const uint32_t PULSE_SHORT_MS   = 15;
static const uint32_t PULSE_LONG_MS    = 250;
static const uint32_t PULSE_DOUBLE_MS  = 60;
static const uint32_t PULSE_TRIPLE_MS  = 750;
static const uint32_t INTER_GAP_MS     = 50;
static const uint32_t PATTERN_GAP_MS   = 200;

// ─── Queue slot ───────────────────────────────────────────────────────
struct HapticSlot {                    // FIX 1: was "sturct" typo
    HapticPattern pattern;
    uint16_t      custom_on_ms;
    uint16_t      custom_off_ms;
    uint8_t       custom_count;
};

// ─── State ────────────────────────────────────────────────────────────
static HapticSlot s_queue[HAL_HAPTIC_MAX_PATTERN];
static uint8_t    s_q_head  = 0;
static uint8_t    s_q_tail  = 0;
static uint8_t    s_q_count = 0;

static bool       s_playing       = false;
static bool       s_motor_on      = false;
static uint8_t    s_pulses_left   = 0;
static uint16_t   s_on_ms         = 0;
static uint16_t   s_off_ms        = 0;
static bool       s_in_gap        = false;
static uint64_t   s_next_ns       = 0;
static bool       s_silenced      = false;

// ─── Helpers ──────────────────────────────────────────────────────────
static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void sysfs_write(const char* path, int value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d", value);
    write(fd, buf, len);
    close(fd);
}

static void motor_on() {
    if (s_silenced) return;
    sysfs_write(PWM_DUTY, PWM_DUTY_FULL);
    s_motor_on = true;
}

static void motor_off() {
    sysfs_write(PWM_DUTY, PWM_DUTY_OFF);
    s_motor_on = false;
}

static uint32_t clamp(uint32_t ms) {
    return (ms > HAL_HAPTIC_MAX_ON_MS) ? HAL_HAPTIC_MAX_ON_MS : ms;
}

// FIX 3+4: renamed params to avoid shadowing issues
static void begin_pattern(HapticPattern p, uint16_t con_ms, uint16_t coff_ms, uint8_t cnt) {
    switch (p) {
        case HAPTIC_PULSE_SHORT:   s_on_ms = PULSE_SHORT_MS;  s_off_ms = 0;            s_pulses_left = 1; break;
        case HAPTIC_PULSE_LONG:    s_on_ms = PULSE_LONG_MS;   s_off_ms = 0;            s_pulses_left = 1; break;
        case HAPTIC_DOUBLE_SHORT:  s_on_ms = PULSE_DOUBLE_MS; s_off_ms = INTER_GAP_MS; s_pulses_left = 2; break;
        case HAPTIC_TRIPLE_LONG:   s_on_ms = PULSE_TRIPLE_MS; s_off_ms = INTER_GAP_MS; s_pulses_left = 3; break;
        case HAPTIC_CUSTOM:        s_on_ms = con_ms;          s_off_ms = coff_ms;      s_pulses_left = cnt; break;
    }
    s_in_gap   = false;
    s_playing  = true;
    s_next_ns  = now_ns();
    motor_on();
}

// ─── Public API ───────────────────────────────────────────────────────

// FIX 5: header declares void, so .cpp must also return void
void hal_haptic_init() {
    memset(s_queue, 0, sizeof(s_queue));
    s_q_head = s_q_tail = s_q_count = 0;
    s_playing = s_motor_on = s_silenced = false;

    sysfs_write(PWM_EXPORT, 0);
    usleep(10000);

    sysfs_write(PWM_PERIOD, PWM_PERIOD_NS);
    sysfs_write(PWM_DUTY,   PWM_DUTY_OFF);
    sysfs_write(PWM_ENABLE, 1);
}

void hal_haptic_tick() {
    if (s_silenced) return;

    uint64_t now = now_ns();

    if (!s_playing) {
        if (s_q_count == 0) return;
        HapticSlot& s = s_queue[s_q_head];
        begin_pattern(s.pattern, s.custom_on_ms, s.custom_off_ms, s.custom_count);
        s_q_head = (s_q_head + 1) % HAL_HAPTIC_MAX_PATTERN;
        s_q_count--;
        return;
    }

    if (now < s_next_ns) return;

    if (s_in_gap) {
        s_in_gap = false;
        if (s_pulses_left > 0) {
            motor_on();
            s_next_ns = now + (uint64_t)clamp(s_on_ms) * 1000000ULL;
        } else {
            motor_off();
            s_playing = false;          // FIX 6: was s_q_playing
        }
    } else {
        motor_off();
        if (s_pulses_left > 0 && s_off_ms > 0) {
            s_in_gap  = true;
            s_next_ns = now + (uint64_t)s_off_ms * 1000000ULL;
        } else {
            s_playing = false;
        }
    }
}

void hal_haptic_pulse_ms(uint16_t duration_ms) {
    if (s_silenced) return;
    motor_off();
    s_playing = false;
    s_pulses_left = 0;

    motor_on();
    usleep((useconds_t)clamp(duration_ms) * 1000);
    motor_off();
}

bool hal_haptic_queue(HapticPattern p) {
    if (s_q_count >= HAL_HAPTIC_MAX_PATTERN) return false;
    s_queue[s_q_tail] = { p, 0, 0, 0 };
    s_q_tail = (s_q_tail + 1) % HAL_HAPTIC_MAX_PATTERN;
    s_q_count++;
    return true;
}

bool hal_haptic_queue_custom(uint16_t on_ms, uint16_t off_ms, uint8_t count) {
    if (s_q_count >= HAL_HAPTIC_MAX_PATTERN) return false;
    s_queue[s_q_tail] = { HAPTIC_CUSTOM, on_ms, off_ms, count };
    s_q_tail = (s_q_tail + 1) % HAL_HAPTIC_MAX_PATTERN;
    s_q_count++;
    return true;
}

void hal_haptic_clear_queue() {
    motor_off();
    s_playing = false;
    s_pulses_left = 0;
    s_q_head = s_q_tail = s_q_count = 0;
}

void hal_haptic_silence() {
    motor_off();
    s_silenced = true;
    s_playing  = false;
    s_pulses_left = 0;
}

void hal_haptic_unsilence() { s_silenced = false; }

uint8_t hal_haptic_queue_depth() { return s_q_count; }
bool    hal_haptic_is_playing()  { return s_playing; }
bool    hal_haptic_is_silenced() { return s_silenced; }
// hal_haptic.h — coin vibration motor driver ras pi 5
// PWM via sysfs: /sys/class/pwm/pwmchip0/pwm0/
// Requires: dtoverlay=pwm in /boot/firmware/config.txt
// Hardware: IRLZ44N N-channel MOSFET gate on PIN_HAPTIC
//          + 1N4007 flyback diode (mandatory)
//          + 10k-ohm gate pull-down (boot safety)

#pragma once
#include <cstdint>
#include <stdint.h>
#include <stdbool.h>

//Config
#define HAL_HAPTIC_MAX_ON_MS    1500 // thermal shutdown
#define HAL_HAPTIC_MAX_PATTERN  8    // max patterns
#define HAL_HAPTIC_PWM_FREQ_HZ  500  // PWM freq (Hz)
#define HAL_HAPTIC_PWM_PERIOD_NS 2000000 // 1e9 500 nanoseconds

//pattern def
typedef enum {
    HAPTIC_PULSE_SHORT= 0, //15ms -button confirm
    HAPTIC_PULSE_LONG= 1, // 250 ms- warning
    HAPTIC_DOUBLE_SHORT= 2, //2x60 ms - success
    HAPTIC_TRIPLE_LONG= 3, // 3x750 - error
    HAPTIC_CUSTOM= 4, // could be defined later 
} HapticPattern;

//API

//must be called once at start, exports sysfs PWM, sets period to 0
//returns false if sysfs export fails
void hal_haptic_init();

//non-blocking tick from main every 5-10ms
//drains pattern queue and advances the current pattern
void hal_haptic_tick();

//blocking single pulse for button press config
void hal_haptic_pulse_ms(uint16_t duration_ms);

//enque a patetrn FIFO returns false is full
bool hal_haptic_queue(HapticPattern p);

//enque a custom oattern (explicit timing)
bool hal_haptic_queue_custom(uint16_t on_ms, uint16_t off_ms, uint8_t count);

//cnacel everything
void hal_haptic_clear_queue();

//supress all output
void hal_haptic_silence();

//Re-enable silence
void hal_haptic_unsilence();

//diagnostic
uint8_t hal_haptic_queue_depth();
bool hal_haptic_is_playing();
bool hal_haptic_is_silenced();
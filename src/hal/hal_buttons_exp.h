// hal_buttons_exp.h
#pragma once
#include <stdint.h>

void hal_buttons_exp_init();
void hal_buttons_exp_read();

uint8_t hal_buttons_exp_get_select();
uint8_t hal_buttons_exp_get_back();
uint8_t hal_buttons_exp_get_aux();
uint8_t hal_buttons_exp_get_encoder_a();
uint8_t hal_buttons_exp_get_encoder_b();
uint8_t hal_buttons_exp_get_encoder_push();
uint8_t hal_buttons_exp_get_kill_switch();

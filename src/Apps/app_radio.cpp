#include <Arduino.h>
#include "../config.h"
#include "../UI/draw.h"
#include "app_radio.h"
#include <theme.h>

void app_radio_init() {
    Serial.println("Radio App Initialized");
}

void app_radio_tick() {
    
}

void app_radio_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, C_BLACK);
    
    draw_text(10, 10, "RADIO APP", C_WHITE, C_BLACK, FONT_SM);
    draw_text(10, 50, "Status: Ready", C_PGREEN, C_BLACK, FONT_SM);
    
    draw_hline(0, SCR_H - 16, SCR_W, C_BORDER);
    draw_text(8, SCR_H - 12, "[B] BACK", C_MGRAY, C_BLACK, FONT_SM);
}

void app_radio_event(Event e) {
    
}

void app_radio_suspend() {
    Serial.println("Radio App Suspended");
}
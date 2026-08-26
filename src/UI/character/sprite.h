//sprite.h
#ifndef SPRITE_ENGINE_H
#define SPRITE_ENGINE_H

#include <Arduino.h>

enum MascotState {
    STATE_IDLE,
    STATE_APP_ACTIVE
};

struct EyeTarget {
    int16_t x;
    int16_t y;
};

class MascotEngine {
private:
    uint8_t _currentAppIndex;
    MascotState _state;
    
    // Fixed screen coordinates
    const int16_t HEAD_X = 45;
    const int16_t HEAD_Y = 30;
    const int16_t BODY_X = 45;
    const int16_t BODY_Y = 150;
    
    // Absolute eye centers on screen
    const int16_t L_EYE_HOME_X = 100;
    const int16_t R_EYE_HOME_X = 140;
    const int16_t EYES_HOME_Y  = 80;
    
    void calculateEyeOffset(int8_t appIndex, int16_t &outX, int16_t &outY);
    const char* getHeadAsset(uint8_t appIndex);

public:
    MascotEngine();
    void init();
    void updateState(uint8_t appIndex, bool isSelected);
    void render();
};

extern MascotEngine Mascot;

#endif // SPRITE_ENGINE_H
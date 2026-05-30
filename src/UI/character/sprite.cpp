//sprite.cpp
#include "sprite.h"
#include "../draw.h"     // Contains your verified draw_jpg()
#include "../../config.h" // Centralized configurations
#include <TFT_eSPI.h>

extern TFT_eSPI tft; // Access core global display instance

MascotEngine::MascotEngine() : _currentAppIndex(0), _state(STATE_IDLE) {}

void MascotEngine::init() {
    // Initial draw to clean frame buffer
    render();
}

const char* MascotEngine::getHeadAsset(uint8_t appIndex) {
    if (_state == STATE_IDLE) {
        return "/assets/IDLE.jpg"; // Default face
    }
    
    // Map application ID directly to your asset tree
    switch(appIndex) {
        case 1:  return "/assets/STAT.jpg";     //
        case 2:  return "/assets/MAP.jpg";      //
        case 4:  return "/assets/RAD-O.jpg";    //
        case 5:  return "/assets/DATA.jpg";     //
        case 6:  return "/assets/MISC.jpg";     //
        case 7:  return "/assets/Marauder.jpg"; //
        default: return "/assets/IDLE.jpg";     // Fallback
    }
}

void MascotEngine::calculateEyeOffset(int8_t appIndex, int16_t &outX, int16_t &outY) {
    // 8 distinct directions mapped to the circular rotary encoder positions
    // Max pupil throw distance = 6 pixels inside the socket
    const int8_t MAX_THROW = 6;
    
    // Calculate angle based on menu index slot (8 slots total across 360 degrees)
    float angle = (appIndex * 45.0f) * (PI / 180.0f);
    
    outX = (int16_t)(cos(angle) * MAX_THROW);
    outY = (int16_t)(sin(angle) * MAX_THROW);
}

void MascotEngine::updateState(uint8_t appIndex, bool isSelected) {
    _currentAppIndex = appIndex;
    _state = isSelected ? STATE_APP_ACTIVE : STATE_IDLE;
}

void MascotEngine::render() {
    // Layer 1: Render base body asset
    draw_jpg(BODY_X, BODY_Y, "/assets/BODY.jpg"); //
    
    // Layer 2: Render contextual head asset over body boundary
    const char* headPath = getHeadAsset(_currentAppIndex);
    draw_jpg(HEAD_X, HEAD_Y, headPath);
    
    // Layer 3: Calculate and draw procedural eyes tracking the encoder
    int16_t offsetX = 0;
    int16_t offsetY = 0;
    calculateEyeOffset(_currentAppIndex, offsetX, offsetY);
    
    // Configuration options for procedural eyes
    const uint16_t EYE_SCLERA_COLOR = TFT_WHITE;
    const uint16_t EYE_PUPIL_COLOR  = TFT_GREEN; // Phosphor green match
    const int16_t SCLERA_RADIUS     = 10;
    const int16_t PUPIL_RADIUS      = 4;
    
    // Draw Left Eye
    tft.fillCircle(L_EYE_HOME_X, EYES_HOME_Y, SCLERA_RADIUS, EYE_SCLERA_COLOR);
    tft.fillCircle(L_EYE_HOME_X + offsetX, EYES_HOME_Y + offsetY, PUPIL_RADIUS, EYE_PUPIL_COLOR);
    
    // Draw Right Eye
    tft.fillCircle(R_EYE_HOME_X, EYES_HOME_Y, SCLERA_RADIUS, EYE_SCLERA_COLOR);
    tft.fillCircle(R_EYE_HOME_X + offsetX, EYES_HOME_Y + offsetY, PUPIL_RADIUS, EYE_PUPIL_COLOR);
}

// Instantiate global handle
MascotEngine Mascot;
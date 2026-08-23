#pragma once

#include <Arduino.h>

#define TOUCH_CS 10

struct TouchTap {
    int16_t rawX;
    int16_t rawY;
    int16_t screenX;
    int16_t screenY;
    bool mapped;
};

void initializeTouchInput();
void maintainTouchInput();
void maintainTouchNavigation();
bool consumeTouchTap(TouchTap& tap);
bool touchCalibrationValid();
bool touchDetected();
bool touchCalibrationActive();
bool startTouchCalibration();
void cancelTouchCalibration();
const char* touchCalibrationStatus();
bool touchDiagnosticsActive();

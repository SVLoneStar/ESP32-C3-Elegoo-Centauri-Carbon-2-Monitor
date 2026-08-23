#include "TouchInput.h"

#include "AppState.h"
#include "Config.h"
#include "ConfigStore.h"
#include "Diagnostics.h"

#include <XPT2046_Touchscreen.h>

namespace {
constexpr uint8_t TOUCH_NO_IRQ = 255;
constexpr unsigned long TOUCH_POLL_INTERVAL_MS = 20;
constexpr unsigned long TOUCH_LOG_INTERVAL_MS = 100;
constexpr unsigned long TOUCH_MIN_TAP_INTERVAL_MS = 250;
constexpr unsigned long TOUCH_MAX_TAP_DURATION_MS = 750;
constexpr int16_t TOUCH_MAX_TAP_MOVEMENT_RAW = 300;
constexpr int16_t TOUCH_SCREEN_WIDTH = 320;
constexpr int16_t TOUCH_SCREEN_HEIGHT = 240;
constexpr bool TOUCH_VERBOSE_LOGGING = true;
constexpr int16_t CALIBRATION_INSET = 20;
constexpr int16_t CALIBRATION_TOLERANCE = 30;
constexpr unsigned long CALIBRATION_RESULT_MS = 1800;
constexpr unsigned long TOUCH_DIAGNOSTICS_TIMEOUT_MS = 20000;

struct CalibrationPoint {
    int16_t screenX;
    int16_t screenY;
    int16_t rawX;
    int16_t rawY;
};

const CalibrationPoint CALIBRATION_TARGETS[4] = {
    {CALIBRATION_INSET, CALIBRATION_INSET, 0, 0},
    {TOUCH_SCREEN_WIDTH - 1 - CALIBRATION_INSET, CALIBRATION_INSET, 0, 0},
    {TOUCH_SCREEN_WIDTH - 1 - CALIBRATION_INSET,
     TOUCH_SCREEN_HEIGHT - 1 - CALIBRATION_INSET, 0, 0},
    {CALIBRATION_INSET, TOUCH_SCREEN_HEIGHT - 1 - CALIBRATION_INSET, 0, 0}
};

XPT2046_Touchscreen touchController(TOUCH_CS, TOUCH_NO_IRQ);
bool initialized = false;
bool pressed = false;
bool tapPending = false;
bool calibrationRequested = false;
bool calibrationCancelRequested = false;
bool calibrationRunning = false;
bool calibrationResultVisible = false;
bool calibrationSucceeded = false;
bool diagnosticsActive = false;
uint8_t calibrationTarget = 0;
int16_t pressRawX = 0;
int16_t pressRawY = 0;
int16_t latestRawX = 0;
int16_t latestRawY = 0;
int32_t rawXTotal = 0;
int32_t rawYTotal = 0;
uint16_t rawSampleCount = 0;
unsigned long pressStarted = 0;
unsigned long lastPoll = 0;
unsigned long lastRawLog = 0;
unsigned long lastTap = 0;
unsigned long calibrationResultStarted = 0;
unsigned long diagnosticsOpenedAt = 0;
TouchTap pendingTap = {};
CalibrationPoint capturedPoints[4] = {};

bool mapTouchPointWithConfig(int16_t rawX, int16_t rawY, const AppConfig& config,
                             int16_t& screenX, int16_t& screenY) {
    if (!config.touchCalibrationValid || config.touchRawXMin == config.touchRawXMax ||
        config.touchRawYMin == config.touchRawYMax) {
        screenX = -1;
        screenY = -1;
        return false;
    }

    int16_t sourceX = config.touchSwapAxes ? rawY : rawX;
    int16_t sourceY = config.touchSwapAxes ? rawX : rawY;
    long mappedX = map(sourceX, config.touchRawXMin, config.touchRawXMax, 0,
                       TOUCH_SCREEN_WIDTH - 1);
    long mappedY = map(sourceY, config.touchRawYMin, config.touchRawYMax, 0,
                       TOUCH_SCREEN_HEIGHT - 1);
    mappedX = constrain(mappedX, 0L, (long)TOUCH_SCREEN_WIDTH - 1);
    mappedY = constrain(mappedY, 0L, (long)TOUCH_SCREEN_HEIGHT - 1);

    if (config.touchInvertX)
        mappedX = TOUCH_SCREEN_WIDTH - 1 - mappedX;
    if (config.touchInvertY)
        mappedY = TOUCH_SCREEN_HEIGHT - 1 - mappedY;

    screenX = (int16_t)mappedX;
    screenY = (int16_t)mappedY;
    return true;
}

bool mapTouchPoint(int16_t rawX, int16_t rawY, int16_t& screenX, int16_t& screenY) {
    return mapTouchPointWithConfig(rawX, rawY, appConfig, screenX, screenY);
}

void logTouchPoint(const char* eventName, int16_t rawX, int16_t rawY) {
    if (!TOUCH_VERBOSE_LOGGING)
        return;

    int16_t screenX;
    int16_t screenY;
    bool mapped = mapTouchPoint(rawX, rawY, screenX, screenY);
    if (mapped) {
        serialDiagnostic("TOUCH | event=%s raw_x=%d raw_y=%d screen_x=%d screen_y=%d",
                         eventName, rawX, rawY, screenX, screenY);
    } else {
        serialDiagnostic("TOUCH | event=%s raw_x=%d raw_y=%d screen=UNCALIBRATED", eventName,
                         rawX, rawY);
    }
}

void drawCalibrationTarget() {
    const CalibrationPoint& target = CALIBRATION_TARGETS[calibrationTarget];
    tft.fillScreen(C_BG);
    tft.setTextWrap(false);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(52, 42);
    tft.print("TOUCH CALIBRATION");
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(69, 72);
    tft.print("Tap and release target ");
    tft.print(calibrationTarget + 1);
    tft.print(" / 4");
    tft.drawCircle(target.screenX, target.screenY, 10, C_CYAN);
    tft.drawFastHLine(target.screenX - 14, target.screenY, 29, C_CYAN);
    tft.drawFastVLine(target.screenX, target.screenY - 14, 29, C_CYAN);
}

void drawCalibrationResult(bool success) {
    tft.fillScreen(C_BG);
    tft.setTextWrap(false);
    tft.setFont(&FreeSans12pt7b);
    tft.setTextColor(success ? C_GREEN : C_RED, C_BG);
    tft.setCursor(success ? 50 : 36, 105);
    tft.print(success ? "CALIBRATION SAVED" : "CALIBRATION FAILED");
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setCursor(54, 140);
    tft.print(success ? "Returning to monitor" : "Previous values preserved");
}

int16_t sourceX(const CalibrationPoint& point, bool swapAxes) {
    return swapAxes ? point.rawY : point.rawX;
}

int16_t sourceY(const CalibrationPoint& point, bool swapAxes) {
    return swapAxes ? point.rawX : point.rawY;
}

bool deriveCalibration(AppConfig& candidate) {
    long horizontalX = (capturedPoints[1].rawX + capturedPoints[2].rawX) / 2L -
                       (capturedPoints[0].rawX + capturedPoints[3].rawX) / 2L;
    long horizontalY = (capturedPoints[1].rawY + capturedPoints[2].rawY) / 2L -
                       (capturedPoints[0].rawY + capturedPoints[3].rawY) / 2L;
    long verticalX = (capturedPoints[2].rawX + capturedPoints[3].rawX) / 2L -
                     (capturedPoints[0].rawX + capturedPoints[1].rawX) / 2L;
    long verticalY = (capturedPoints[2].rawY + capturedPoints[3].rawY) / 2L -
                     (capturedPoints[0].rawY + capturedPoints[1].rawY) / 2L;
    bool swapAxes = abs(horizontalY) + abs(verticalX) > abs(horizontalX) + abs(verticalY);

    long left = (sourceX(capturedPoints[0], swapAxes) +
                 sourceX(capturedPoints[3], swapAxes)) / 2L;
    long right = (sourceX(capturedPoints[1], swapAxes) +
                  sourceX(capturedPoints[2], swapAxes)) / 2L;
    long top = (sourceY(capturedPoints[0], swapAxes) +
                sourceY(capturedPoints[1], swapAxes)) / 2L;
    long bottom = (sourceY(capturedPoints[2], swapAxes) +
                   sourceY(capturedPoints[3], swapAxes)) / 2L;

    if (abs(right - left) < 500 || abs(bottom - top) < 500)
        return false;

    float horizontalScale = (float)(right - left) /
                            (CALIBRATION_TARGETS[1].screenX - CALIBRATION_TARGETS[0].screenX);
    float verticalScale = (float)(bottom - top) /
                          (CALIBRATION_TARGETS[3].screenY - CALIBRATION_TARGETS[0].screenY);
    long rawAtX0 = lroundf(left - horizontalScale * CALIBRATION_INSET);
    long rawAtXMax = lroundf(right + horizontalScale * CALIBRATION_INSET);
    long rawAtY0 = lroundf(top - verticalScale * CALIBRATION_INSET);
    long rawAtYMax = lroundf(bottom + verticalScale * CALIBRATION_INSET);

    candidate.touchCalibrationValid = true;
    candidate.touchRawXMin = constrain(min(rawAtX0, rawAtXMax), 0L, 4095L);
    candidate.touchRawXMax = constrain(max(rawAtX0, rawAtXMax), 0L, 4095L);
    candidate.touchRawYMin = constrain(min(rawAtY0, rawAtYMax), 0L, 4095L);
    candidate.touchRawYMax = constrain(max(rawAtY0, rawAtYMax), 0L, 4095L);
    candidate.touchSwapAxes = swapAxes;
    candidate.touchInvertX = rawAtX0 > rawAtXMax;
    candidate.touchInvertY = rawAtY0 > rawAtYMax;

    for (uint8_t i = 0; i < 4; i++) {
        int16_t mappedX;
        int16_t mappedY;
        if (!mapTouchPointWithConfig(capturedPoints[i].rawX, capturedPoints[i].rawY,
                                     candidate, mappedX, mappedY) ||
            abs(mappedX - CALIBRATION_TARGETS[i].screenX) > CALIBRATION_TOLERANCE ||
            abs(mappedY - CALIBRATION_TARGETS[i].screenY) > CALIBRATION_TOLERANCE) {
            return false;
        }
    }
    return true;
}

void finishCalibration(bool success) {
    calibrationRunning = false;
    calibrationResultVisible = true;
    calibrationSucceeded = success;
    calibrationResultStarted = millis();
    drawCalibrationResult(success);
}

void captureCalibrationPoint(int16_t rawX, int16_t rawY) {
    capturedPoints[calibrationTarget] = CALIBRATION_TARGETS[calibrationTarget];
    capturedPoints[calibrationTarget].rawX = rawX;
    capturedPoints[calibrationTarget].rawY = rawY;
    logTouchPoint("CALIBRATION_POINT", rawX, rawY);

    calibrationTarget++;
    if (calibrationTarget < 4) {
        drawCalibrationTarget();
        return;
    }

    AppConfig candidate = appConfig;
    bool valid = deriveCalibration(candidate);
    if (valid && saveConfiguration(candidate)) {
        appConfig = candidate;
        finishCalibration(true);
    } else {
        finishCalibration(false);
    }
}

void leaveCalibrationMode() {
    calibrationRunning = false;
    calibrationResultVisible = false;
    calibrationRequested = false;
    calibrationCancelRequested = false;
    pressed = false;
    tapPending = false;
    currentDisplayMode = MODE_UNKNOWN;
    fullRedrawNeeded = true;
}

void setDiagnosticsActive(bool active, const TouchTap* tap) {
    diagnosticsActive = active;
    diagnosticsOpenedAt = active ? millis() : 0;
    currentDisplayMode = MODE_UNKNOWN;
    fullRedrawNeeded = true;

    if (tap != nullptr) {
        serialDiagnostic("TOUCH | target=%s screen_x=%d screen_y=%d",
                         active ? "DIAGNOSTICS_OPEN" : "DIAGNOSTICS_CLOSE", tap->screenX,
                         tap->screenY);
    } else {
        serialDiagnostic("TOUCH | target=DIAGNOSTICS_CLOSE reason=TIMEOUT");
    }
}
}

void initializeTouchInput() {
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    initialized = touchController.begin(tftSPI);
    touchController.setRotation(1);
    lastPoll = millis();
    lastTap = millis() - TOUCH_MIN_TAP_INTERVAL_MS;
    serialDiagnostic("Touch input      : %s",
                     initialized ? (touchCalibrationValid() ? "READY" : "READY (UNCALIBRATED)")
                                 : "INITIALIZATION FAILED");
}

void maintainTouchInput() {
    if (calibrationRequested && initialized && !calibrationRunning && !calibrationResultVisible) {
        calibrationRequested = false;
        calibrationRunning = true;
        calibrationTarget = 0;
        pressed = false;
        tapPending = false;
        drawCalibrationTarget();
    }

    if (calibrationCancelRequested) {
        leaveCalibrationMode();
        return;
    }

    if (calibrationResultVisible) {
        if (millis() - calibrationResultStarted >= CALIBRATION_RESULT_MS)
            leaveCalibrationMode();
        return;
    }

    if (!initialized || millis() - lastPoll < TOUCH_POLL_INTERVAL_MS)
        return;

    lastPoll = millis();
    digitalWrite(TFT_CS, HIGH);
    bool isTouched = touchController.touched();
    if (isTouched) {
        TS_Point point = touchController.getPoint();
        latestRawX = point.x;
        latestRawY = point.y;

        if (!pressed) {
            pressed = true;
            pressStarted = millis();
            pressRawX = latestRawX;
            pressRawY = latestRawY;
            rawXTotal = latestRawX;
            rawYTotal = latestRawY;
            rawSampleCount = 1;
            lastRawLog = millis();
            logTouchPoint("PRESS", latestRawX, latestRawY);
        } else {
            rawXTotal += latestRawX;
            rawYTotal += latestRawY;
            rawSampleCount++;
            if (TOUCH_VERBOSE_LOGGING && millis() - lastRawLog >= TOUCH_LOG_INTERVAL_MS) {
                lastRawLog = millis();
                logTouchPoint("RAW", latestRawX, latestRawY);
            }
        }
        return;
    }

    if (!pressed)
        return;

    pressed = false;
    int16_t averagedRawX = rawSampleCount ? rawXTotal / rawSampleCount : latestRawX;
    int16_t averagedRawY = rawSampleCount ? rawYTotal / rawSampleCount : latestRawY;
    int movementX = abs(latestRawX - pressRawX);
    int movementY = abs(latestRawY - pressRawY);

    if (calibrationRunning) {
        if (rawSampleCount >= 2 && movementX <= TOUCH_MAX_TAP_MOVEMENT_RAW &&
            movementY <= TOUCH_MAX_TAP_MOVEMENT_RAW) {
            captureCalibrationPoint(averagedRawX, averagedRawY);
        }
        return;
    }

    unsigned long pressDuration = millis() - pressStarted;
    if (pressDuration > TOUCH_MAX_TAP_DURATION_MS || movementX > TOUCH_MAX_TAP_MOVEMENT_RAW ||
        movementY > TOUCH_MAX_TAP_MOVEMENT_RAW || millis() - lastTap < TOUCH_MIN_TAP_INTERVAL_MS) {
        return;
    }

    lastTap = millis();
    pendingTap.rawX = averagedRawX;
    pendingTap.rawY = averagedRawY;
    pendingTap.mapped =
        mapTouchPoint(averagedRawX, averagedRawY, pendingTap.screenX, pendingTap.screenY);
    tapPending = true;
    logTouchPoint("TAP target=NONE", averagedRawX, averagedRawY);
}

void maintainTouchNavigation() {
    if (touchCalibrationActive())
        return;

    if (diagnosticsActive && millis() - diagnosticsOpenedAt >= TOUCH_DIAGNOSTICS_TIMEOUT_MS) {
        setDiagnosticsActive(false, nullptr);
        tapPending = false;
        return;
    }

    TouchTap tap;
    if (!consumeTouchTap(tap) || !touchCalibrationValid() || !tap.mapped)
        return;

    setDiagnosticsActive(!diagnosticsActive, &tap);
}

bool consumeTouchTap(TouchTap& tap) {
    if (!tapPending)
        return false;
    tap = pendingTap;
    tapPending = false;
    return true;
}

bool touchCalibrationValid() {
    return appConfig.touchCalibrationValid;
}

bool touchDetected() {
    return initialized;
}

bool touchCalibrationActive() {
    return calibrationRequested || calibrationRunning || calibrationResultVisible;
}

bool startTouchCalibration() {
    if (!initialized || touchCalibrationActive())
        return false;
    calibrationRequested = true;
    return true;
}

void cancelTouchCalibration() {
    if (touchCalibrationActive())
        calibrationCancelRequested = true;
}

const char* touchCalibrationStatus() {
    if (calibrationRequested)
        return "STARTING";
    if (calibrationRunning)
        return "IN PROGRESS";
    if (calibrationResultVisible)
        return calibrationSucceeded ? "SAVED" : "FAILED";
    return touchCalibrationValid() ? "VALID" : "INVALID";
}

bool touchDiagnosticsActive() {
    return diagnosticsActive;
}

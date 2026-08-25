#include "DisplaySleep.h"

#include "AppState.h"
#include "Config.h"
#include "PrinterData.h"
#include "SerialLog.h"
#include "StateTrace.h"
#include "TouchInput.h"

namespace {
bool sleeping = false;
bool idleTimerRunning = false;
unsigned long idleTimerStartedAt = 0;
PrinterState previousPrinterState = PRINTER_STATE_UNKNOWN;
bool previousTouchViewActive = false;

void logTransition(const char* event, const char* reason) {
    serialLogLinef("DISPLAY_SLEEP | event=%s reason=%s", event, reason);
    stateTraceLog("DISPLAY_SLEEP", String(event) + " | " + reason);
}

void startIdleTimer() {
    idleTimerStartedAt = millis();
    idleTimerRunning = true;
}

void wakeDisplay(const char* reason) {
    if (!sleeping)
        return;

    sleeping = false;
    currentDisplayMode = MODE_UNKNOWN;
    fullRedrawNeeded = true;
    if (getPrinterState() == PRINTER_STATE_IDLE)
        startIdleTimer();
    logTransition("WAKE", reason);
}

bool validMappedTapPending() {
    TouchTap tap;
    return touchCalibrationValid() && peekTouchTap(tap) && tap.mapped;
}
} // namespace

void initializeDisplaySleep() {
    previousPrinterState = getPrinterState();
    previousTouchViewActive = touchDiagnosticsActive() || touchCalibrationActive();
    sleeping = false;
    idleTimerRunning = false;
    if (previousPrinterState == PRINTER_STATE_IDLE)
        startIdleTimer();
}

void maintainDisplaySleep() {
    PrinterState printerState = getPrinterState();
    bool touchViewActive = touchDiagnosticsActive() || touchCalibrationActive();

    if (printerState != previousPrinterState) {
        if (printerState == PRINTER_STATE_IDLE)
            startIdleTimer();
        else
            idleTimerRunning = false;

        if (sleeping && printerState != PRINTER_STATE_IDLE)
            wakeDisplay("PRINTER_ACTIVE");
        previousPrinterState = printerState;
    }

    if (touchViewActive) {
        if (sleeping)
            wakeDisplay(touchCalibrationActive() ? "CALIBRATION" : "DIAGNOSTICS");
        previousTouchViewActive = true;
        return;
    }

    if (previousTouchViewActive && printerState == PRINTER_STATE_IDLE)
        startIdleTimer();
    previousTouchViewActive = false;

    if (sleeping) {
        if (printerState != PRINTER_STATE_IDLE) {
            wakeDisplay("PRINTER_ACTIVE");
        } else if (validMappedTapPending()) {
            TouchTap consumedTap;
            consumeTouchTap(consumedTap);
            wakeDisplay("TOUCH");
        }
        return;
    }

    if (printerState != PRINTER_STATE_IDLE) {
        idleTimerRunning = false;
        return;
    }

    if (validMappedTapPending())
        startIdleTimer();

    if (appConfig.displaySleepTimeoutMinutes == 0)
        return;

    if (!idleTimerRunning)
        startIdleTimer();

    unsigned long timeoutMs = (unsigned long)appConfig.displaySleepTimeoutMinutes * 60000UL;
    if (millis() - idleTimerStartedAt < timeoutMs)
        return;

    tft.fillScreen(ILI9341_BLACK);
    sleeping = true;
    idleTimerRunning = false;
    logTransition("SLEEP", "IDLE_TIMEOUT");
}

bool displaySleepActive() {
    return sleeping;
}

const char* displaySleepStateText() {
    if (sleeping)
        return "SLEEPING";
    if (appConfig.displaySleepTimeoutMinutes == 0)
        return "DISABLED";
    return "AWAKE";
}

String configuredDisplaySleepTimeoutText() {
    return displaySleepTimeoutText(appConfig.displaySleepTimeoutMinutes);
}

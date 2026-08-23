/*
  ==============================================================
  Elegoo Centauri Carbon 2
 * Printer Monitor
  ESP32-C3 SuperMini + ILI9341 320x240

 * ==============================================================

  Behavior-preserving Arduino IDE
 * multi-file version.
*/

#include "AppState.h"
#include "Diagnostics.h"
#include "PrinterData.h"
#include "TimeHelpers.h"
#include "Display.h"
#include "HomeAssistant.h"
#include "Weather.h"
#include "ConfigStore.h"
#include "WebUI.h"
#include "StateTrace.h"
#include "TouchInput.h"
#include "BootStage.h"
#include "SerialLog.h"

// SETUP
// ============================================================

void setup() {
    unsigned long callStarted;

    initializeBootStageTracking();
    markBootStage(BOOT_STAGE_SERIAL_BEGIN);
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
    initializeSerialLog();
    enableBootStageSerialLogging();
    markBootStage(BOOT_STAGE_SERIAL_READY);

    callStarted = millis();
    initBootCounter();
    recordBlockingCall("initBootCounter", millis() - callStarted);

    callStarted = millis();
    printResetReason();
    recordBlockingCall("printResetReason", millis() - callStarted);

    markBootStage(BOOT_STAGE_CONFIG_BEGIN);
    callStarted = millis();
    initializeConfigStore();
    recordBlockingCall("initializeConfigStore", millis() - callStarted);
    markBootStage(BOOT_STAGE_CONFIG_READY);

    callStarted = millis();
    initializePrinterEntityIds();
    recordBlockingCall("initializePrinterEntityIds", millis() - callStarted);

    callStarted = millis();
    initializeStateTrace();
    recordBlockingCall("initializeStateTrace", millis() - callStarted);
    enableBootStageTraceLogging();
    markBootStage(BOOT_STAGE_TRACE_READY);

    callStarted = millis();
    stateTraceLogBoot(bootCount, resetReasonText(lastResetReason));
    recordBlockingCall("stateTraceLogBoot", millis() - callStarted);

    markBootStage(BOOT_STAGE_WEB_UI_TASK_BEGIN);
    callStarted = millis();
    initializeWebUI();
    recordBlockingCall("initializeWebUI", millis() - callStarted);
    markBootStage(BOOT_STAGE_WEB_UI_TASK_READY);

    callStarted = millis();
    initializePrinter();
    recordBlockingCall("initializePrinter", millis() - callStarted);

    callStarted = millis();
    initializeWeather();
    recordBlockingCall("initializeWeather", millis() - callStarted);

    // ----------------------------------------------------------
    // TFT
    // ----------------------------------------------------------

    markBootStage(BOOT_STAGE_TFT_BEGIN);
    callStarted = millis();
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);

    tftSPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);

    tft.begin();

    tft.setRotation(3);

    tft.setTextWrap(false);

    tft.fillScreen(C_BG);
    recordBlockingCall("initializeTFT", millis() - callStarted);
    markBootStage(BOOT_STAGE_TFT_READY);

    callStarted = millis();
    initializeTouchInput();
    recordBlockingCall("initializeTouchInput", millis() - callStarted);

    // ----------------------------------------------------------
    // CALLBACKS
    // ----------------------------------------------------------

    markBootStage(BOOT_STAGE_WEBSOCKET_INIT_BEGIN);
    client.onMessage(onMessageCallback);

    client.onEvent(onEventCallback);
    markBootStage(BOOT_STAGE_WEBSOCKET_INIT_READY);

    // ----------------------------------------------------------
    // WIFI + INITIAL STATES
    // ----------------------------------------------------------

    callStarted = millis();
    bool wifiConnected = connectWiFi();
    recordBlockingCall("connectWiFi", millis() - callStarted);

    if (wifiConnected) {
        markBootStage(BOOT_STAGE_NTP_BEGIN);
        callStarted = millis();
        startNTP();
        recordBlockingCall("startNTP", millis() - callStarted);
        markBootStage(BOOT_STAGE_NTP_READY);

        markBootStage(BOOT_STAGE_INITIAL_REST_BEGIN);
        callStarted = millis();
        loadInitialPrinterData();
        recordBlockingCall("loadInitialPrinterData", millis() - callStarted);
        markBootStage(BOOT_STAGE_INITIAL_REST_COMPLETE);
    }

    // ----------------------------------------------------------
    // WS START
    // ----------------------------------------------------------

    lastReconnectAttempt = millis() - reconnectDelayMs;

    lastTimePageSwitch = millis();

    currentDisplayMode = MODE_UNKNOWN;

    fullRedrawNeeded = true;
    markBootStage(BOOT_STAGE_SETUP_COMPLETE);
}

// ============================================================
// LOOP
// ============================================================

void loop() {
    unsigned long loopStarted = millis();
    unsigned long callStarted = millis();

    maintainWiFi();
    recordBlockingCall("maintainWiFi", millis() - callStarted);

    callStarted = millis();
    maintainWebSocket();
    recordBlockingCall("maintainWebSocket", millis() - callStarted);

    callStarted = millis();
    updatePrinterStateMachine();
    recordBlockingCall("updatePrinterStateMachine", millis() - callStarted);

    callStarted = millis();
    maintainWeather();
    recordBlockingCall("maintainWeather", millis() - callStarted);

    callStarted = millis();
    maintainTouchInput();
    recordBlockingCall("maintainTouchInput", millis() - callStarted);

    callStarted = millis();
    maintainTouchNavigation();
    recordBlockingCall("maintainTouchNavigation", millis() - callStarted);

    if (!touchCalibrationActive() && millis() - lastDisplayUpdate >= 100) {
        lastDisplayUpdate = millis();

        callStarted = millis();
        updateDisplay();
        recordBlockingCall("updateDisplay", millis() - callStarted);
    }

    if (millis() - lastSerialStatus >= 10000) {
        lastSerialStatus = millis();

        callStarted = millis();
        printStatus();
        recordBlockingCall("printStatus", millis() - callStarted);
    }

    delay(1);
    unsigned long loopElapsed = millis() - loopStarted;
    recordLoopDuration(loopElapsed);
    recordBlockingCall("loop", loopElapsed);
}

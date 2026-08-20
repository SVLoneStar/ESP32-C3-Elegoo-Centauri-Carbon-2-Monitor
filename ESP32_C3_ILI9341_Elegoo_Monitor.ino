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

// SETUP
// ============================================================

void setup() {
    unsigned long callStarted;

    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif

    callStarted = millis();
    initBootCounter();
    recordBlockingCall("initBootCounter", millis() - callStarted);

    callStarted = millis();
    printResetReason();
    recordBlockingCall("printResetReason", millis() - callStarted);

    callStarted = millis();
    initializeConfigStore();
    recordBlockingCall("initializeConfigStore", millis() - callStarted);

    callStarted = millis();
    initializePrinterEntityIds();
    recordBlockingCall("initializePrinterEntityIds", millis() - callStarted);

    callStarted = millis();
    initializeStateTrace();
    recordBlockingCall("initializeStateTrace", millis() - callStarted);

    callStarted = millis();
    stateTraceLogBoot(bootCount, resetReasonText(lastResetReason));
    recordBlockingCall("stateTraceLogBoot", millis() - callStarted);

    callStarted = millis();
    initializeWebUI();
    recordBlockingCall("initializeWebUI", millis() - callStarted);

    callStarted = millis();
    initializePrinter();
    recordBlockingCall("initializePrinter", millis() - callStarted);

    callStarted = millis();
    initializeWeather();
    recordBlockingCall("initializeWeather", millis() - callStarted);

    // ----------------------------------------------------------
    // TFT
    // ----------------------------------------------------------

    callStarted = millis();
    tftSPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

    tft.begin();

    tft.setRotation(3);

    tft.setTextWrap(false);

    tft.fillScreen(C_BG);
    recordBlockingCall("initializeTFT", millis() - callStarted);

    // ----------------------------------------------------------
    // CALLBACKS
    // ----------------------------------------------------------

    client.onMessage(onMessageCallback);

    client.onEvent(onEventCallback);

    // ----------------------------------------------------------
    // WIFI + INITIAL STATES
    // ----------------------------------------------------------

    callStarted = millis();
    bool wifiConnected = connectWiFi();
    recordBlockingCall("connectWiFi", millis() - callStarted);

    if (wifiConnected) {
        callStarted = millis();
        startNTP();
        recordBlockingCall("startNTP", millis() - callStarted);

        callStarted = millis();
        loadInitialPrinterData();
        recordBlockingCall("loadInitialPrinterData", millis() - callStarted);
    }

    // ----------------------------------------------------------
    // WS START
    // ----------------------------------------------------------

    lastReconnectAttempt = millis() - reconnectDelayMs;

    lastTimePageSwitch = millis();

    currentDisplayMode = MODE_UNKNOWN;

    fullRedrawNeeded = true;
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

    if (millis() - lastDisplayUpdate >= 100) {
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

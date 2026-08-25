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
#include "DisplaySleep.h"

// SETUP
// ============================================================

void setup() {
    unsigned long callStarted;
    unsigned long callElapsed;
    unsigned long setupEnteredAt = millis();

    initializeBootStageTracking(setupEnteredAt);
    markBootStage(BOOT_STAGE_SERIAL_BEGIN);
    callStarted = millis();
    Serial.begin(115200);
    callElapsed = millis() - callStarted;
    initializeSerialLog();
    enableBootStageSerialLogging();
    markBootStage(BOOT_STAGE_SERIAL_READY);
    recordSetupTiming("Serial.begin", callElapsed);
#if ARDUINO_USB_CDC_ON_BOOT
#if ARDUINO_USB_MODE
    serialDiagnostic("SERIAL_CONFIG | implementation=HWCDC_USB_SERIAL_JTAG usb_mode=%d cdc_on_boot=%d",
                     ARDUINO_USB_MODE, ARDUINO_USB_CDC_ON_BOOT);
#else
    serialDiagnostic("SERIAL_CONFIG | implementation=NATIVE_USB_CDC usb_mode=%d cdc_on_boot=%d",
                     ARDUINO_USB_MODE, ARDUINO_USB_CDC_ON_BOOT);
#endif
#else
    serialDiagnostic("SERIAL_CONFIG | implementation=UART0 usb_mode=%d cdc_on_boot=%d",
                     ARDUINO_USB_MODE, ARDUINO_USB_CDC_ON_BOOT);
#endif

    callStarted = millis();
    initBootCounter();
    recordBlockingCall("initBootCounter", millis() - callStarted);

    callStarted = millis();
    printResetReason();
    recordBlockingCall("printResetReason", millis() - callStarted);

    markBootStage(BOOT_STAGE_CONFIG_BEGIN);
    callStarted = millis();
    initializeConfigStore();
    callElapsed = millis() - callStarted;
    markBootStage(BOOT_STAGE_CONFIG_READY);
    recordBlockingCall("initializeConfigStore", callElapsed);
    recordSetupTiming("LittleFS.config", callElapsed);

    callStarted = millis();
    initializePrinterEntityIds();
    recordBlockingCall("initializePrinterEntityIds", millis() - callStarted);

    markBootStage(BOOT_STAGE_STATE_TRACE_BEGIN);
    callStarted = millis();
    initializeStateTrace();
    callElapsed = millis() - callStarted;
    markBootStage(BOOT_STAGE_TRACE_READY);
    recordBlockingCall("initializeStateTrace", callElapsed);
    recordSetupTiming("StateTrace.init", callElapsed);
    enableBootStageTraceLogging();

    callStarted = millis();
    stateTraceLogBoot(bootCount, resetReasonText(lastResetReason));
    recordBlockingCall("stateTraceLogBoot", millis() - callStarted);

    markBootStage(BOOT_STAGE_WEB_UI_TASK_BEGIN);
    callStarted = millis();
    initializeWebUI();
    callElapsed = millis() - callStarted;
    markBootStage(BOOT_STAGE_WEB_UI_TASK_READY);
    recordBlockingCall("initializeWebUI", callElapsed);
    recordSetupTiming("WebUI.task", callElapsed);

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
    callElapsed = millis() - callStarted;
    markBootStage(BOOT_STAGE_TFT_READY);
    recordBlockingCall("initializeTFT", callElapsed);
    recordSetupTiming("TFT.init", callElapsed);
    drawBootProgressScreen();

    String weatherStatus = getWeatherStatus();
    updateBootWeatherStatus(weatherStatus.startsWith("DISABLED") ? "DISABLED" : "OK");
    updateBootWebUIStatus(isWebUIReady() ? "OK" : "WAITING");

    callStarted = millis();
    initializeTouchInput();
    recordBlockingCall("initializeTouchInput", millis() - callStarted);

    // ----------------------------------------------------------
    // CALLBACKS
    // ----------------------------------------------------------

    markBootStage(BOOT_STAGE_WEBSOCKET_INIT_BEGIN);
    callStarted = millis();
    client.onMessage(onMessageCallback);

    client.onEvent(onEventCallback);
    callElapsed = millis() - callStarted;
    markBootStage(BOOT_STAGE_WEBSOCKET_INIT_READY);
    recordSetupTiming("WebSocket.client.init", callElapsed);

    // ----------------------------------------------------------
    // WIFI + INITIAL STATES
    // ----------------------------------------------------------

    updateBootWiFiStatus("CONNECTING");
    callStarted = millis();
    bool wifiConnected = connectWiFi();
    recordBlockingCall("connectWiFi", millis() - callStarted);
    updateBootWiFiStatus(wifiConnected ? "OK" : "OFFLINE");
    updateBootWebUIStatus(isWebUIReady() ? "OK" : wifiConnected ? "WAITING" : "OFFLINE");

    if (wifiConnected) {
        markBootStage(BOOT_STAGE_NTP_BEGIN);
        callStarted = millis();
        startNTP();
        callElapsed = millis() - callStarted;
        markBootStage(BOOT_STAGE_NTP_READY);
        recordBlockingCall("startNTP", callElapsed);

        loadInitialPrinterData();
        updateBootWebUIStatus(isWebUIReady() ? "OK" : "WAITING");
    } else {
        updateBootHomeAssistantStatus("OFFLINE");
        updateBootPrinterProgress(0, 12, "OFFLINE");
        if (!weatherStatus.startsWith("DISABLED"))
            updateBootWeatherStatus("OFFLINE");
    }

    // ----------------------------------------------------------
    // WS START
    // ----------------------------------------------------------

    lastReconnectAttempt = millis() - reconnectDelayMs;

    lastTimePageSwitch = millis();

    currentDisplayMode = MODE_UNKNOWN;

    fullRedrawNeeded = true;
    markBootStage(BOOT_STAGE_SETUP_COMPLETE);
    finishBootProgressScreen();
    initializeDisplaySleep();
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
    serviceInitialPrinterDataLoad();
    recordBlockingCall("serviceInitialPrinterDataLoad", millis() - callStarted);

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
    maintainDisplaySleep();
    recordBlockingCall("maintainDisplaySleep", millis() - callStarted);

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

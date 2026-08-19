/*
  ==============================================================
  Elegoo Centauri Carbon 2 Printer Monitor
  ESP32-C3 SuperMini + ILI9341 320x240
  ==============================================================

  Behavior-preserving Arduino IDE multi-file version.
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

// SETUP
// ============================================================

void setup()
{
  Serial.begin(
    115200
  );


  delay(
    1200
  );


  initBootCounter();


  printResetReason();


  initializeConfigStore();


  initializeWebUI();


  initializePrinter();


  initializeWeather();


  // ----------------------------------------------------------
  // TFT
  // ----------------------------------------------------------

  tftSPI.begin(
    TFT_SCK,
    -1,
    TFT_MOSI,
    TFT_CS
  );


  tft.begin();


  tft.setRotation(
    3
  );


  tft.setTextWrap(
    false
  );


  tft.fillScreen(
    C_BG
  );


  // ----------------------------------------------------------
  // CALLBACKS
  // ----------------------------------------------------------

  client.onMessage(
    onMessageCallback
  );


  client.onEvent(
    onEventCallback
  );


  // ----------------------------------------------------------
  // WIFI + INITIAL STATES
  // ----------------------------------------------------------

  if (
    connectWiFi()
  )
  {
    startNTP();


    loadInitialPrinterData();
  }


  // ----------------------------------------------------------
  // WS START
  // ----------------------------------------------------------

  lastReconnectAttempt =
    millis() -
    reconnectDelayMs;


  lastTimePageSwitch =
    millis();


  currentDisplayMode =
    MODE_UNKNOWN;


  fullRedrawNeeded =
    true;
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  maintainWiFi();


  maintainWebSocket();


  maintainWeather();


  if (
    millis() -
      lastDisplayUpdate >=
      100
  )
  {
    lastDisplayUpdate =
      millis();


    updateDisplay();
  }


  if (
    millis() -
      lastSerialStatus >=
      10000
  )
  {
    lastSerialStatus =
      millis();


    printStatus();
  }


  delay(
    1
  );
}

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Preferences.h>
#include <time.h>
#include <esp_system.h>

using namespace websockets;

extern char ENTITY_REMAINING[96];
extern char ENTITY_TOTAL_LAYERS[96];
extern char ENTITY_CURRENT_LAYER[96];
extern char ENTITY_PROGRESS[96];
extern char ENTITY_CURRENT_STATUS[96];
extern char ENTITY_PRINT_STATUS[96];
extern char ENTITY_PRINT_ERROR[96];
extern char ENTITY_ERROR_REASON[96];
extern char ENTITY_BOX_TEMP[96];
extern char ENTITY_NOZZLE_TEMP[96];
extern char ENTITY_BED_TEMP[96];
extern char ENTITY_PRINT_SPEED[96];

#define TFT_SCK 4
#define TFT_MOSI 6
#define TFT_CS 7
#define TFT_DC 3
#define TFT_RST 1

#define C_BG ILI9341_BLACK
#define C_TEXT ILI9341_WHITE
#define C_DIM 0x7BEF
#define C_GREY 0xBDF7
#define C_CYAN ILI9341_CYAN
#define C_GREEN ILI9341_GREEN
#define C_ORANGE 0xFD20
#define C_RED ILI9341_RED
#define C_BAR_BG 0x2104

extern SPIClass tftSPI;
extern Adafruit_ILI9341 tft;
extern Preferences preferences;
extern uint32_t bootCount;
extern esp_reset_reason_t lastResetReason;

struct PrinterData {
    String currentStatus;
    String printStatus;
    String remainingTime;
    String printError;
    String errorReason;
    float progress;
    int currentLayer;
    int totalLayers;
    float nozzleTemp;
    float bedTemp;
    float boxTemp;
    float printSpeed;
};

extern PrinterData printer;

enum DisplayMode {
    MODE_UNKNOWN,
    MODE_IDLE,
    MODE_PRINTING,
    MODE_PAUSED,
    MODE_ERROR,
    MODE_PRINT_COMPLETE
};
extern DisplayMode currentDisplayMode;
extern WebsocketsClient client;
extern bool wsConnected;
extern bool authenticated;
extern bool subscriptionPending;
extern bool subscribed;
extern unsigned long connectAttempts;
extern unsigned long successfulConnects;
extern unsigned long disconnectCount;
extern unsigned long messageCount;
extern unsigned long triggerCount;
extern unsigned long lastReconnectAttempt;
extern unsigned long reconnectDelayMs;
extern const unsigned long MAX_RECONNECT_DELAY;
extern unsigned long lastWiFiReconnectAttempt;
extern const unsigned long WIFI_RECONNECT_INTERVAL;
extern bool fullRedrawNeeded;
extern bool headerStatusDirty;
extern bool clockDirty;
extern bool dateDirty;
extern bool progressDirty;
extern bool timeDirty;
extern bool layerSpeedDirty;
extern bool nozzleDirty;
extern bool bedDirty;
extern bool chamberDirty;
extern bool idleConnectionDirty;
extern bool idleDiagnosticsDirty;
extern bool idleWeatherDirty;
extern bool errorDetailsDirty;
extern String lastShownClock;
extern String lastShownDate;
extern bool lastShownWifi;
extern bool lastShownHA;
extern bool headerStateInitialized;
extern unsigned long lastDisplayUpdate;
extern unsigned long lastTimePageSwitch;
extern unsigned long lastSerialStatus;
extern unsigned long lastIdleDiagnosticMinute;
extern bool showETA;

#pragma once
#include "AppState.h"

void drawCenteredText(const String& text, int baselineY, const GFXfont* font, uint16_t color);
void drawNozzleIcon(int x, int y, uint16_t color);
void drawBedIcon(int x, int y, uint16_t color);
void drawChamberIcon(int x, int y, uint16_t color);
void drawStaticHeader();
void updateHeaderStatus();
void updateHeaderClock();
void drawPrintingLayout();
void drawPausedLayout();
void drawErrorLayout();
void drawPrintCompleteLayout();
void drawIdleLayout();
void updateProgress();
void updateTimeField();
void updatePausedTimeField();
void updateErrorDetails();
void updateLayerAndSpeed();
void drawTemperatureField(int x, int iconType, float value, uint16_t color);
void updateIdleDate();
void updateIdleLargeClock();
void updateIdleConnection();
void updateIdleDiagnostics();
void drawOnDeviceDiagnostics();
void drawBootProgressScreen();
void updateBootWiFiStatus(const char* status);
void updateBootHomeAssistantStatus(const char* status);
void updateBootPrinterProgress(uint8_t completed, uint8_t total, const char* status = nullptr);
void updateBootWeatherStatus(const char* status);
void updateBootWebUIStatus(const char* status);
void finishBootProgressScreen();
void checkTimeChanges();
void updateDisplay();

#include "Display.h"
#include "Diagnostics.h"
#include "Config.h"
#include "PrinterData.h"
#include "TimeHelpers.h"
#include "Version.h"
#include "Weather.h"
#include "HomeAssistant.h"
#include "BootStage.h"
#include "TouchInput.h"
#include "DisplaySleep.h"

namespace {
constexpr uint8_t BOOT_LINE_COUNT = 5;
constexpr int16_t BOOT_LINE_BASELINES[BOOT_LINE_COUNT] = {85, 113, 141, 169, 197};
const char* BOOT_LINE_LABELS[BOOT_LINE_COUNT] = {"WiFi", "Home Assistant", "Printer states",
                                                 "Weather", "Web UI"};
char bootLineValues[BOOT_LINE_COUNT][32];
bool bootProgressActive = false;

uint16_t bootStatusColor(const char* status) {
    if (status == nullptr)
        return C_TEXT;
    if (strstr(status, "OK") != nullptr)
        return C_GREEN;
    if (strstr(status, "OFFLINE") != nullptr || strstr(status, "FAILED") != nullptr)
        return C_RED;
    if (strstr(status, "TIMEOUT") != nullptr)
        return C_ORANGE;
    if (strstr(status, "DISABLED") != nullptr)
        return C_DIM;
    return C_CYAN;
}

void updateBootLine(uint8_t lineIndex, const char* value) {
    if (!bootProgressActive || lineIndex >= BOOT_LINE_COUNT || value == nullptr ||
        strcmp(bootLineValues[lineIndex], value) == 0) {
        return;
    }

    strlcpy(bootLineValues[lineIndex], value, sizeof(bootLineValues[lineIndex]));
    int16_t baseline = BOOT_LINE_BASELINES[lineIndex];
    tft.fillRect(6, baseline - 18, 308, 23, C_BG);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(C_TEXT);
    tft.setCursor(10, baseline);
    tft.print(BOOT_LINE_LABELS[lineIndex]);
    tft.print(":");
    tft.setTextColor(bootStatusColor(value));
    tft.setCursor(176, baseline);
    tft.print(value);
}
}

// CENTER TEXT
// ============================================================

void drawCenteredText(const String& text, int baselineY, const GFXfont* font, uint16_t color) {
    tft.setFont(font);

    tft.setTextColor(color);

    int16_t x1;
    int16_t y1;

    uint16_t w;
    uint16_t h;

    tft.getTextBounds(text, 0, baselineY, &x1, &y1, &w, &h);

    int x = (320 - w) / 2;

    if (x < 0)
        x = 0;

    tft.setCursor(x, baselineY);

    tft.print(text);
}

void drawDiagnosticsTextLine(const String& text, int baselineY, uint16_t color = C_TEXT) {
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(color, C_BG);
    tft.setCursor(6, baselineY);
    tft.print(text);
}

void drawDegreeSymbol(int16_t centerX, int16_t baselineY, uint16_t color) {
    tft.drawCircle(centerX, baselineY - 12, 2, color);
}

void drawOnDeviceDiagnostics() {
    tft.fillScreen(C_BG);
    drawCenteredText(FIRMWARE_IDENTIFIER, 23, &FreeSansBold9pt7b, C_CYAN);
    tft.drawFastHLine(0, 31, 320, C_DIM);

    String line = "Uptime: ";
    line += getUptimeString();
    line += "  Boot: ";
    line += String(bootCount);
    drawDiagnosticsTextLine(line, 51);

    line = "Reset: ";
    line += resetReasonWithCode(lastResetReason);
    drawDiagnosticsTextLine(line, 73);

    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    line = "WiFi: ";
    line += wifiConnected ? "CONNECTED" : "OFFLINE";
    line += "  RSSI: ";
    line += wifiConnected ? String(WiFi.RSSI()) + " dBm" : "--";
    drawDiagnosticsTextLine(line, 95, wifiConnected ? C_GREEN : C_RED);

    line = "HA: WS ";
    line += wsConnected ? "UP" : "DOWN";
    line += "  Auth ";
    line += authenticated ? "YES" : "NO";
    line += "  Sub ";
    line += subscribed ? "YES" : "NO";
    drawDiagnosticsTextLine(line, 117, wsConnected && authenticated && subscribed ? C_GREEN : C_RED);

    line = "WS: Try ";
    line += String(connectAttempts);
    line += "  Disc ";
    line += String(disconnectCount);
    line += "  Early ";
    line += String(getConsecutiveEarlyDisconnectCount());
    drawDiagnosticsTextLine(line, 139);

    line = "Heap: ";
    line += String(ESP.getFreeHeap());
    line += "  Min: ";
    line += String(ESP.getMinFreeHeap());
    drawDiagnosticsTextLine(line, 161);

    line = "Printer: ";
    line += printerStateText(getPrinterState());
    drawDiagnosticsTextLine(line, 183);

    line = "Stage: ";
    line += bootStageText(getCurrentBootStage());
    drawDiagnosticsTextLine(line, 205);

    drawCenteredText("Tap to return", 229, &FreeSans9pt7b, C_DIM);
}

void drawBootProgressScreen() {
    memset(bootLineValues, 0, sizeof(bootLineValues));
    bootProgressActive = true;
    tft.fillScreen(C_BG);
    drawCenteredText("STARTING", 26, &FreeSansBold12pt7b, C_CYAN);
    drawCenteredText(FIRMWARE_IDENTIFIER, 51, &FreeSans9pt7b, C_DIM);
    tft.drawFastHLine(0, 59, 320, C_DIM);
    updateBootWiFiStatus("WAITING");
    updateBootHomeAssistantStatus("WAITING");
    updateBootPrinterProgress(0, 12);
    updateBootWeatherStatus("WAITING");
    updateBootWebUIStatus("WAITING");
}

void updateBootWiFiStatus(const char* status) {
    updateBootLine(0, status);
}

void updateBootHomeAssistantStatus(const char* status) {
    updateBootLine(1, status);
}

void updateBootPrinterProgress(uint8_t completed, uint8_t total, const char* status) {
    char value[32];
    if (status != nullptr)
        snprintf(value, sizeof(value), "%u / %u %s", completed, total, status);
    else
        snprintf(value, sizeof(value), "%u / %u", completed, total);
    updateBootLine(2, value);
}

void updateBootWeatherStatus(const char* status) {
    updateBootLine(3, status);
}

void updateBootWebUIStatus(const char* status) {
    updateBootLine(4, status);
}

void finishBootProgressScreen() {
    bootProgressActive = false;
}

// ICONS
// ============================================================

void drawNozzleIcon(int x, int y, uint16_t color) {
    tft.fillRoundRect(x + 4, y, 16, 8, 2, color);

    tft.fillRect(x + 8, y + 8, 8, 4, color);

    tft.fillTriangle(x + 6, y + 12, x + 18, y + 12, x + 12, y + 21, color);

    tft.drawFastVLine(x + 12, y + 21, 2, color);
}

void drawBedIcon(int x, int y, uint16_t color) {
    tft.fillRoundRect(x, y + 11, 26, 5, 2, color);

    tft.drawFastHLine(x + 3, y + 19, 20, color);

    tft.drawFastVLine(x + 5, y + 16, 6, color);

    tft.drawFastVLine(x + 21, y + 16, 6, color);

    tft.drawLine(x + 5, y + 7, x + 8, y + 2, color);

    tft.drawLine(x + 12, y + 7, x + 15, y + 2, color);

    tft.drawLine(x + 19, y + 7, x + 22, y + 2, color);
}

void drawChamberIcon(int x, int y, uint16_t color) {
    tft.drawRoundRect(x + 2, y, 24, 24, 3, color);

    tft.drawCircle(x + 14, y + 15, 3, color);

    tft.fillCircle(x + 14, y + 15, 1, color);

    tft.drawFastVLine(x + 14, y + 6, 8, color);

    tft.drawFastHLine(x + 6, y + 20, 16, color);
}

// ============================================================
// STATIC HEADER
// ============================================================

void drawStaticHeader() {
    tft.setFont(&FreeSansBold9pt7b);

    tft.setTextColor(C_TEXT);

    tft.setCursor(8, 22);

    tft.print("CENT. CARBON 2");

    tft.drawFastHLine(0, 32, 320, C_DIM);

    headerStatusDirty = true;

    clockDirty = true;
}

// ============================================================
// HEADER STATUS ONLY
// ============================================================

void updateHeaderStatus() {
    bool wifi = WiFi.status() == WL_CONNECTED;

    bool ha = wsConnected && authenticated && subscribed;

    if (!headerStateInitialized || wifi != lastShownWifi) {
        tft.fillCircle(224, 16, 5, wifi ? C_GREEN : C_RED);

        lastShownWifi = wifi;
    }

    if (!headerStateInitialized || ha != lastShownHA) {
        tft.fillCircle(242, 16, 5, ha ? C_CYAN : C_RED);

        lastShownHA = ha;

        idleConnectionDirty = true;
    }

    headerStateInitialized = true;

    headerStatusDirty = false;
}

// ============================================================
// HEADER CLOCK ONLY
// ============================================================

void updateHeaderClock() {
    String now = getClock();

    if (now == lastShownClock && !clockDirty) {
        return;
    }

    tft.fillRect(258, 2, 62, 27, C_BG);

    tft.setFont(&FreeSans9pt7b);

    tft.setTextColor(C_TEXT);

    tft.setCursor(263, 21);

    tft.print(now);

    lastShownClock = now;

    clockDirty = false;
}

// ============================================================
// PRINTING STATIC LAYOUT
// ============================================================

void drawPrintingLayout() {
    tft.fillScreen(C_BG);

    drawStaticHeader();

    tft.drawFastHLine(8, 83, 304, C_DIM);

    tft.drawFastHLine(8, 151, 304, C_DIM);

    tft.drawFastHLine(8, 183, 304, C_DIM);

    progressDirty = true;

    timeDirty = true;

    layerSpeedDirty = true;

    nozzleDirty = true;

    bedDirty = true;

    chamberDirty = true;
}

// ============================================================
// IDLE STATIC LAYOUT
// ============================================================

void drawIdleLayout() {
    tft.fillScreen(C_BG);

    drawStaticHeader();

    drawCenteredText("PRINTER IDLE", 54, &FreeSansBold12pt7b, C_CYAN);

    tft.drawFastHLine(20, 158, 280, C_DIM);

    dateDirty = true;

    idleConnectionDirty = true;

    idleDiagnosticsDirty = true;

    markWeatherDirty();

    lastIdleDiagnosticMinute = ULONG_MAX;
}

// ============================================================
// PROGRESS
// ============================================================

void updateProgress() {
    float progress = printer.progress;

    const int barX = 8;

    const int barY = 43;

    const int barW = 244;

    const int barH = 30;

    tft.fillRoundRect(barX, barY, barW, barH, 6, C_BAR_BG);

    if (!isnan(progress)) {
        float p = constrain(progress, 0.0f, 100.0f);

        int fill = (barW - 4) * p / 100.0f;

        if (fill > 0) {
            tft.fillRoundRect(barX + 2, barY + 2, fill, barH - 4, 5, C_CYAN);
        }
    }

    tft.fillRect(258, 39, 62, 40, C_BG);

    tft.setFont(&FreeSansBold18pt7b);

    tft.setTextColor(C_TEXT);

    tft.setCursor(260, 70);

    if (isnan(progress)) {
        tft.print("--");
    } else {
        tft.print((int)round(progress));
    }

    tft.setFont(&FreeSansBold12pt7b);

    tft.print("%");

    progressDirty = false;
}

// ============================================================
// ETA / REMAINING
// ============================================================

void updateTimeField() {
    String value;

    String label;

    if (showETA) {
        value = calculateETA();

        label = "Finish at";
    } else {
        value = formatRemaining();

        label = "Remaining";
    }

    tft.fillRect(0, 84, 320, 66, C_BG);

    tft.setFont(&FreeSansBold18pt7b);

    tft.setTextColor(C_TEXT);

    int16_t x1;
    int16_t y1;

    uint16_t w;
    uint16_t h;

    tft.getTextBounds(value, 0, 0, &x1, &y1, &w, &h);

    tft.setCursor((320 - w) / 2, 119);

    tft.print(value);

    tft.setFont(&FreeSans9pt7b);

    tft.setTextColor(C_DIM);

    tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);

    tft.setCursor((320 - w) / 2, 143);

    tft.print(label);

    timeDirty = false;
}

// ============================================================
// PAUSED TIME FIELD
// ============================================================

void updatePausedTimeField() {
    tft.fillRect(0, 84, 320, 66, C_BG);

    drawCenteredText("PRINT PAUSED", 112, &FreeSansBold18pt7b, C_ORANGE);

    String remaining = "Remaining: ";
    remaining += formatRemaining();

    drawCenteredText(remaining, 143, &FreeSans9pt7b, C_GREY);

    timeDirty = false;
}

// ============================================================
// ERROR DETAILS
// ============================================================

void updateErrorDetails() {
    tft.fillRect(0, 124, 320, 115, C_BG);

    String errorText = printer.printError;
    String reasonText = printer.errorReason;

    if (invalidState(errorText))
        errorText = "No error code available";

    if (invalidState(reasonText))
        reasonText = "No error reason available";

    if (errorText.length() > 38)
        errorText = errorText.substring(0, 35) + "...";

    if (reasonText.length() > 38)
        reasonText = reasonText.substring(0, 35) + "...";

    drawCenteredText("Error", 146, &FreeSans9pt7b, C_DIM);

    drawCenteredText(errorText, 171, &FreeSansBold9pt7b, C_RED);

    drawCenteredText("Reason", 199, &FreeSans9pt7b, C_DIM);

    drawCenteredText(reasonText, 226, &FreeSansBold9pt7b, C_TEXT);

    errorDetailsDirty = false;
}

// ============================================================
// LAYER + SPEED
// ============================================================

void updateLayerAndSpeed() {
    tft.fillRect(0, 152, 320, 30, C_BG);

    tft.setFont(&FreeSans9pt7b);

    tft.setTextColor(C_DIM);

    tft.setCursor(8, 175);

    tft.print("Layer");

    tft.setFont(&FreeSansBold12pt7b);

    tft.setTextColor(C_TEXT);

    tft.setCursor(60, 176);

    if (printer.currentLayer >= 0) {
        tft.print(printer.currentLayer);
    } else {
        tft.print("-");
    }

    tft.print(" / ");

    if (printer.totalLayers >= 0) {
        tft.print(printer.totalLayers);
    } else {
        tft.print("-");
    }

    tft.setFont(&FreeSans9pt7b);

    tft.setTextColor(C_DIM);

    tft.setCursor(225, 175);

    tft.print("Speed");

    tft.setFont(&FreeSansBold12pt7b);

    tft.setTextColor(C_CYAN);

    tft.setCursor(280, 176);

    tft.print(speedMode(printer.printSpeed));

    layerSpeedDirty = false;
}

// ============================================================
// TEMPERATURE FIELD
// ============================================================

void drawTemperatureField(int x, int iconType, float value, uint16_t color) {
    tft.fillRect(x, 187, 101, 51, C_BG);

    if (iconType == 0) {
        drawNozzleIcon(x + 3, 211, color);
    } else if (iconType == 1) {
        drawBedIcon(x + 2, 207, color);
    } else {
        drawChamberIcon(x + 1, 210, color);
    }

    tft.setFont(&FreeSansBold12pt7b);

    tft.setTextColor(color);

    tft.setCursor(x + 34, 231);

    if (isnan(value)) {
        tft.print("--");
    } else {
        tft.print((int)round(value));
    }

    tft.setFont(&FreeSans9pt7b);

    tft.setTextColor(C_GREY);

    int16_t degreeX = tft.getCursorX() + 2;
    tft.setCursor(tft.getCursorX() + 6, 231);
    tft.print("C");
    drawDegreeSymbol(degreeX, 231, C_GREY);
}

// ============================================================
// IDLE DATE
// ============================================================

void updateIdleDate() {
    String date = getLongDate();

    if (date == lastShownDate && !dateDirty) {
        return;
    }

    tft.fillRect(0, 56, 320, 24, C_BG);

    drawCenteredText(date, 76, &FreeSans9pt7b, C_TEXT);

    lastShownDate = date;

    dateDirty = false;
}

// ============================================================
// IDLE LARGE CLOCK
// ============================================================

void updateIdleLargeClock() {
    String now = getClock();

    tft.fillRect(0, 111, 320, 43, C_BG);

    drawCenteredText(now, 143, &FreeSansBold18pt7b, C_TEXT);
}

// ============================================================
// IDLE HA STATUS
// ============================================================

void updateIdleConnection() {
    bool haOK = wsConnected && authenticated && subscribed;

    tft.fillRect(0, 162, 320, 27, C_BG);

    drawCenteredText(haOK ? "HOME ASSISTANT CONNECTED" : "HOME ASSISTANT OFFLINE", 182,
                     &FreeSansBold9pt7b, haOK ? C_GREEN : C_RED);

    idleConnectionDirty = false;
}

// ============================================================
// IDLE DIAGNOSTICS
// ============================================================

void updateIdleDiagnostics() {
    tft.fillRect(0, 191, 320, 48, C_BG);

    String uptime = getUptimeString();

    uptime.replace(" ", "");

    String line1 = "B";

    line1 += String(bootCount);

    line1 += " U";

    line1 += uptime;

    line1 += " ";

    line1 += FIRMWARE_COMPACT_IDENTIFIER;

    drawCenteredText(line1, 210, &FreeSans9pt7b, C_DIM);

    String line2 = "Last reset: ";

    line2 += resetReasonText(lastResetReason);

    drawCenteredText(line2, 233, &FreeSans9pt7b, lastResetReason == ESP_RST_PANIC ? C_RED : C_DIM);

    idleDiagnosticsDirty = false;
}

// ============================================================
// CHECK CLOCK / DATE CHANGES
// ============================================================

void checkTimeChanges() {
    String clockNow = getClock();

    if (clockNow != lastShownClock) {
        clockDirty = true;

        if (currentDisplayMode == MODE_IDLE) {
            /*
              Large idle clock uses same HH:MM,
              therefore redraw once per minute too.
            */

            // handled below by clockDirty
        }

        /*
          ETA changes with current time even if remaining sensor
          has not changed.
        */

        if (currentDisplayMode == MODE_PRINTING && showETA) {
            timeDirty = true;
        }
    }

    if (currentDisplayMode == MODE_IDLE) {
        String dateNow = getLongDate();

        if (dateNow != lastShownDate) {
            dateDirty = true;
        }

        unsigned long currentMinute = millis() / 60000UL;

        if (currentMinute != lastIdleDiagnosticMinute) {
            lastIdleDiagnosticMinute = currentMinute;

            idleDiagnosticsDirty = true;
        }
    }
}

// ============================================================
// DISPLAY UPDATE
// ============================================================

void updateDisplay() {
    if (displaySleepActive())
        return;

    if (touchDiagnosticsActive()) {
        if (fullRedrawNeeded) {
            drawOnDeviceDiagnostics();
            fullRedrawNeeded = false;
        }
        return;
    }

    // ----------------------------------------------------------
    // MODE
    // ----------------------------------------------------------

    DisplayMode wantedMode;

    switch (getPrinterState()) {
    case PRINTER_STATE_PRINTING:
        wantedMode = MODE_PRINTING;
        break;

    case PRINTER_STATE_PAUSED:
        wantedMode = MODE_PAUSED;
        break;

    case PRINTER_STATE_ERROR:
        wantedMode = MODE_ERROR;
        break;

    case PRINTER_STATE_PRINT_COMPLETE:
        wantedMode = MODE_PRINT_COMPLETE;
        break;

    case PRINTER_STATE_IDLE:
    case PRINTER_STATE_UNKNOWN:
    default:
        wantedMode = MODE_IDLE;
        break;
    }

    if (wantedMode != currentDisplayMode) {
        currentDisplayMode = wantedMode;

        fullRedrawNeeded = true;

        if (currentDisplayMode == MODE_PRINTING) {
            showETA = true;

            lastTimePageSwitch = millis();
        }
    }

    // ----------------------------------------------------------
    // FULL REDRAW ONLY ON MODE CHANGE
    // ----------------------------------------------------------

    if (fullRedrawNeeded) {
        headerStateInitialized = false;

        lastShownClock = "";

        switch (currentDisplayMode) {
        case MODE_PRINTING:
            drawPrintingLayout();
            break;

        case MODE_PAUSED:
            drawPausedLayout();
            break;

        case MODE_ERROR:
            drawErrorLayout();
            break;

        case MODE_PRINT_COMPLETE:
            drawPrintCompleteLayout();
            break;

        case MODE_IDLE:
        case MODE_UNKNOWN:
        default:
            drawIdleLayout();
            break;
        }

        fullRedrawNeeded = false;
    }

    // ----------------------------------------------------------
    // DETECT TIME CHANGES
    // ----------------------------------------------------------

    checkTimeChanges();

    // ----------------------------------------------------------
    // HEADER
    // ----------------------------------------------------------

    if (headerStatusDirty) {
        updateHeaderStatus();
    }

    if (clockDirty) {
        updateHeaderClock();
    }

    // ----------------------------------------------------------
    // PRINTING
    // ----------------------------------------------------------

    if (currentDisplayMode == MODE_PRINTING) {
        if (millis() - lastTimePageSwitch >= appConfig.etaRemainingSwitchIntervalMs) {
            lastTimePageSwitch = millis();

            showETA = !showETA;

            timeDirty = true;
        }

        if (progressDirty) {
            updateProgress();
        }

        if (timeDirty) {
            updateTimeField();
        }

        if (layerSpeedDirty) {
            updateLayerAndSpeed();
        }

        if (nozzleDirty) {
            drawTemperatureField(4, 0, printer.nozzleTemp, C_RED);

            nozzleDirty = false;
        }

        if (bedDirty) {
            drawTemperatureField(109, 1, printer.bedTemp, C_ORANGE);

            bedDirty = false;
        }

        if (chamberDirty) {
            drawTemperatureField(214, 2, printer.boxTemp, C_GREEN);

            chamberDirty = false;
        }

        return;
    }

    // ----------------------------------------------------------
    // PAUSED
    // ----------------------------------------------------------

    if (currentDisplayMode == MODE_PAUSED) {
        if (progressDirty)
            updateProgress();

        if (timeDirty)
            updatePausedTimeField();

        if (layerSpeedDirty)
            updateLayerAndSpeed();

        if (nozzleDirty) {
            drawTemperatureField(4, 0, printer.nozzleTemp, C_RED);
            nozzleDirty = false;
        }

        if (bedDirty) {
            drawTemperatureField(109, 1, printer.bedTemp, C_ORANGE);
            bedDirty = false;
        }

        if (chamberDirty) {
            drawTemperatureField(214, 2, printer.boxTemp, C_GREEN);
            chamberDirty = false;
        }

        return;
    }

    // ----------------------------------------------------------
    // ERROR
    // ----------------------------------------------------------

    if (currentDisplayMode == MODE_ERROR) {
        if (errorDetailsDirty)
            updateErrorDetails();

        return;
    }

    // ----------------------------------------------------------
    // PRINT COMPLETE
    // ----------------------------------------------------------

    if (currentDisplayMode == MODE_PRINT_COMPLETE)
        return;

    // ----------------------------------------------------------
    // IDLE
    // ----------------------------------------------------------

    if (dateDirty) {
        updateIdleDate();
    }

    if (weatherNeedsRedraw()) {
        drawWeatherFields();
    }

    if (idleConnectionDirty) {
        updateIdleConnection();
    }

    if (idleDiagnosticsDirty) {
        updateIdleDiagnostics();
    }
}

// ============================================================
// PAUSED STATIC LAYOUT
// ============================================================

void drawPausedLayout() {
    tft.fillScreen(C_BG);
    drawStaticHeader();

    tft.drawFastHLine(8, 83, 304, C_ORANGE);
    tft.drawFastHLine(8, 151, 304, C_DIM);
    tft.drawFastHLine(8, 183, 304, C_DIM);

    progressDirty = true;
    timeDirty = true;
    layerSpeedDirty = true;
    nozzleDirty = true;
    bedDirty = true;
    chamberDirty = true;
}

// ============================================================
// ERROR STATIC LAYOUT
// ============================================================

void drawErrorLayout() {
    tft.fillScreen(C_BG);
    drawStaticHeader();

    drawCenteredText("PRINTER ERROR", 70, &FreeSansBold18pt7b, C_RED);

    tft.fillTriangle(160, 84, 143, 115, 177, 115, C_RED);

    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextColor(C_BG);
    tft.setCursor(157, 109);
    tft.print("!");

    tft.drawFastHLine(20, 122, 280, C_RED);

    errorDetailsDirty = true;
}

// ============================================================
// PRINT COMPLETE STATIC LAYOUT
// ============================================================

void drawPrintCompleteLayout() {
    tft.fillScreen(C_BG);
    drawStaticHeader();

    drawCenteredText("PRINT COMPLETE", 72, &FreeSansBold18pt7b, C_GREEN);

    tft.drawCircle(160, 105, 18, C_GREEN);
    tft.drawLine(150, 105, 157, 112, C_GREEN);
    tft.drawLine(157, 112, 171, 96, C_GREEN);

    String layers = "Final layer: ";

    int finalCurrentLayer = getPrintCompletionCurrentLayer();

    int finalTotalLayers = getPrintCompletionTotalLayers();

    if (finalCurrentLayer >= 0)
        layers += String(finalCurrentLayer);
    else
        layers += "-";

    layers += " / ";

    if (finalTotalLayers >= 0)
        layers += String(finalTotalLayers);
    else
        layers += "-";

    drawCenteredText(layers, 153, &FreeSansBold12pt7b, C_TEXT);

    String completed = "Completed at ";
    completed += getPrintCompletionTime();

    drawCenteredText(completed, 184, &FreeSans9pt7b, C_GREY);

    drawCenteredText("Returning to idle automatically", 222, &FreeSans9pt7b, C_DIM);
}

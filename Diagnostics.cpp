#include "Diagnostics.h"
#include "PrinterData.h"
#include "StateTrace.h"

namespace {
uint32_t maximumLoopDuration = 0;
uint32_t loopOver100MsCount = 0;
uint32_t loopOver500MsCount = 0;
uint32_t loopOver1SecondCount = 0;
uint32_t loopOver5SecondsCount = 0;
}

// BOOT COUNTER
// ============================================================

void initBootCounter() {
    preferences.begin("cc2monitor", false);

    bootCount = preferences.getUInt("bootCount", 0);

    bootCount++;

    preferences.putUInt("bootCount", bootCount);

    preferences.end();
}

// ============================================================
// RESET REASON
// ============================================================

String resetReasonText(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_UNKNOWN:
        return "UNKNOWN";

    case ESP_RST_POWERON:
        return "POWER ON";

    case ESP_RST_EXT:
        return "EXTERNAL";

    case ESP_RST_SW:
        return "SOFTWARE";

    case ESP_RST_PANIC:
        return "PANIC";

    case ESP_RST_INT_WDT:
        return "INT WDT";

    case ESP_RST_TASK_WDT:
        return "TASK WDT";

    case ESP_RST_WDT:
        return "WATCHDOG";

    case ESP_RST_DEEPSLEEP:
        return "DEEP SLEEP";

    case ESP_RST_BROWNOUT:
        return "BROWNOUT";

    case ESP_RST_SDIO:
        return "SDIO";

    case ESP_RST_USB:
        return "USB";

    case ESP_RST_JTAG:
        return "JTAG";

    case ESP_RST_EFUSE:
        return "EFUSE";

    case ESP_RST_PWR_GLITCH:
        return "POWER GLITCH";

    case ESP_RST_CPU_LOCKUP:
        return "CPU LOCKUP";

    default:
        return "OTHER";
    }
}

String resetReasonWithCode(esp_reset_reason_t reason) {
    return resetReasonText(reason) + " (" + String((int)reason) + ")";
}

void printResetReason() {
    lastResetReason = esp_reset_reason();

    Serial.println();
    Serial.println("========== LAST RESET ==========");

    Serial.print("Boot count   : ");

    Serial.println(bootCount);

    Serial.print("Reset reason : ");

    Serial.println(resetReasonWithCode(lastResetReason));

    Serial.println("================================");
}

// ============================================================
// UPTIME
// ============================================================

String getUptimeString() {
    unsigned long totalSeconds = millis() / 1000UL;

    unsigned long days = totalSeconds / 86400UL;

    totalSeconds %= 86400UL;

    unsigned long hours = totalSeconds / 3600UL;

    totalSeconds %= 3600UL;

    unsigned long minutes = totalSeconds / 60UL;

    char buffer[24];

    if (days > 0) {
        snprintf(buffer, sizeof(buffer), "%lud %02luh %02lum", days, hours, minutes);
    } else {
        snprintf(buffer, sizeof(buffer), "%02luh %02lum", hours, minutes);
    }

    return String(buffer);
}

void recordBlockingCall(const char* functionName, unsigned long elapsedMs) {
    if (elapsedMs <= 250)
        return;

    char detail[112];
    snprintf(detail, sizeof(detail), "function=%s elapsed_ms=%lu", functionName, elapsedMs);
    Serial.print("BLOCKING_CALL | ");
    Serial.println(detail);
    stateTraceLog("BLOCKING_CALL", detail);
}

void recordLoopDuration(unsigned long elapsedMs) {
    if (elapsedMs > maximumLoopDuration)
        maximumLoopDuration = elapsedMs;
    if (elapsedMs > 100)
        loopOver100MsCount++;
    if (elapsedMs > 500)
        loopOver500MsCount++;
    if (elapsedMs > 1000)
        loopOver1SecondCount++;
    if (elapsedMs > 5000)
        loopOver5SecondsCount++;
}

uint32_t getMaximumLoopDuration() {
    return maximumLoopDuration;
}

uint32_t getLoopOver100MsCount() {
    return loopOver100MsCount;
}

uint32_t getLoopOver500MsCount() {
    return loopOver500MsCount;
}

uint32_t getLoopOver1SecondCount() {
    return loopOver1SecondCount;
}

uint32_t getLoopOver5SecondsCount() {
    return loopOver5SecondsCount;
}

// SERIAL STATUS
// ============================================================

void printStatus() {
    Serial.println();
    Serial.println("============ STATUS ============");

    Serial.print("Boot count       : ");

    Serial.println(bootCount);

    Serial.print("Last reset       : ");

    Serial.println(resetReasonWithCode(lastResetReason));

    Serial.print("Uptime           : ");

    Serial.println(getUptimeString());

    Serial.print("Printer state    : ");

    Serial.println(printerStateText(getPrinterState()));

    Serial.print("WiFi             : ");

    Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE");

    Serial.print("WebSocket        : ");

    Serial.println(wsConnected ? "CONNECTED" : "OFFLINE");

    Serial.print("Authenticated    : ");

    Serial.println(authenticated ? "YES" : "NO");

    Serial.print("Subscribed       : ");

    Serial.println(subscribed ? "YES" : "NO");

    Serial.print("Connect attempts : ");

    Serial.println(connectAttempts);

    Serial.print("Successful       : ");

    Serial.println(successfulConnects);

    Serial.print("Disconnects      : ");

    Serial.println(disconnectCount);

    Serial.print("WS messages      : ");

    Serial.println(messageCount);

    Serial.print("Printer triggers : ");

    Serial.println(triggerCount);

    Serial.print("Free heap        : ");

    Serial.println(ESP.getFreeHeap());

    Serial.print("Min free heap    : ");

    Serial.println(ESP.getMinFreeHeap());

    Serial.print("Max loop ms      : ");

    Serial.println(maximumLoopDuration);

    Serial.print("Loop overruns    : >100=");
    Serial.print(loopOver100MsCount);
    Serial.print(" >500=");
    Serial.print(loopOver500MsCount);
    Serial.print(" >1000=");
    Serial.print(loopOver1SecondCount);
    Serial.print(" >5000=");
    Serial.println(loopOver5SecondsCount);

    Serial.println("================================");
}

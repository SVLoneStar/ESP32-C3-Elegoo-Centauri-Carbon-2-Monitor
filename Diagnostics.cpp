#include "Diagnostics.h"
#include "PrinterData.h"
#include "StateTrace.h"
#include "SerialLog.h"


namespace {
uint32_t maximumLoopDuration = 0;
uint32_t loopOver100MsCount = 0;
uint32_t loopOver500MsCount = 0;
uint32_t loopOver1SecondCount = 0;
uint32_t loopOver5SecondsCount = 0;
}

void serialDiagnostic(const char* format, ...) {
    char line[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    serialLogLine(line);
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

    if (!beginSerialLogBlock())
        return;

    serialLogBlockLine("========== LAST RESET ==========");
    serialLogBlockLinef("Boot count   : %lu", (unsigned long)bootCount);
    String resetReason = resetReasonWithCode(lastResetReason);
    serialLogBlockLinef("Reset reason : %s", resetReason.c_str());
    serialLogBlockLine("================================");
    endSerialLogBlock();
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
    serialDiagnostic("BLOCKING_CALL | %s", detail);
    stateTraceLog("BLOCKING_CALL", detail);
}

void recordSetupTiming(const char* phaseName, unsigned long elapsedMs) {
    char detail[112];
    snprintf(detail, sizeof(detail), "phase=%s elapsed_ms=%lu", phaseName, elapsedMs);
    serialDiagnostic("SETUP_TIMING | %s", detail);
    stateTraceLog("SETUP_TIMING", detail);
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
    if (!beginSerialLogBlock())
        return;

    serialLogBlockLine("");
    serialLogBlockLine("============ STATUS ============");
    serialLogBlockLinef("Boot count       : %lu", (unsigned long)bootCount);
    String resetReason = resetReasonWithCode(lastResetReason);
    serialLogBlockLinef("Last reset       : %s", resetReason.c_str());
    String uptime = getUptimeString();
    serialLogBlockLinef("Uptime           : %s", uptime.c_str());
    serialLogBlockLinef("Printer state    : %s", printerStateText(getPrinterState()));
    serialLogBlockLinef("WiFi             : %s",
                        WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE");
    serialLogBlockLinef("WebSocket        : %s", wsConnected ? "CONNECTED" : "OFFLINE");
    serialLogBlockLinef("Authenticated    : %s", authenticated ? "YES" : "NO");
    serialLogBlockLinef("Subscribed       : %s", subscribed ? "YES" : "NO");
    serialLogBlockLinef("Connect attempts : %lu", (unsigned long)connectAttempts);
    serialLogBlockLinef("Successful       : %lu", (unsigned long)successfulConnects);
    serialLogBlockLinef("Disconnects      : %lu", (unsigned long)disconnectCount);
    serialLogBlockLinef("WS messages      : %lu", (unsigned long)messageCount);
    serialLogBlockLinef("Printer triggers : %lu", (unsigned long)triggerCount);
    serialLogBlockLinef("Free heap        : %u", (unsigned int)ESP.getFreeHeap());
    serialLogBlockLinef("Min free heap    : %u", (unsigned int)ESP.getMinFreeHeap());
    serialLogBlockLinef(
        "Loop timing      : max=%lums >100ms=%lu >500ms=%lu >1000ms=%lu >5000ms=%lu",
        (unsigned long)maximumLoopDuration, (unsigned long)loopOver100MsCount,
        (unsigned long)loopOver500MsCount, (unsigned long)loopOver1SecondCount,
        (unsigned long)loopOver5SecondsCount);
    serialLogBlockLine("================================");
    endSerialLogBlock();
}

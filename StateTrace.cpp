#include "StateTrace.h"

#include "ConfigStore.h"
#include "Config.h"

#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
const char* STATE_TRACE_PATH = "/state_trace.log";

const size_t STATE_TRACE_MAX_SIZE = 64UL * 1024UL;

SemaphoreHandle_t traceMutex = nullptr;

bool traceAvailable = false;

size_t traceSize = 0;

void makeTimestamp(char* destination, size_t destinationSize) {
    time_t now = time(nullptr);

    if (now >= 1000000000UL) {
        struct tm localTime;

        if (localtime_r(&now, &localTime)) {
            snprintf(destination, destinationSize, "%04d-%02d-%02d %02d:%02d:%02d",
                     localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
                     localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

            return;
        }
    }

    snprintf(destination, destinationSize, "UPTIME_MS=%lu", millis());
}

void sanitizeDetail(const String& detail, char* destination, size_t destinationSize) {
    if (destinationSize == 0)
        return;

    size_t length = min(detail.length(), destinationSize - 1);

    for (size_t i = 0; i < length; i++) {
        char character = detail.charAt(i);

        destination[i] = character == '\r' || character == '\n' ? ' ' : character;
    }

    destination[length] = '\0';
}
} // namespace

void initializeStateTrace() {
    if (traceMutex == nullptr)
        traceMutex = xSemaphoreCreateMutex();

    traceAvailable = traceMutex != nullptr && configStoreAvailable();

    if (traceAvailable) {
        File file = LittleFS.open(STATE_TRACE_PATH, "r");

        if (file) {
            traceSize = file.size();
            file.close();
        }
    }
}

void stateTraceLog(const char* eventType, const String& detail) {
    if (!traceAvailable || !appConfig.stateTraceEnabled || eventType == nullptr ||
        xSemaphoreTake(traceMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    char timestamp[32];
    char safeDetail[200];
    char line[280];

    makeTimestamp(timestamp, sizeof(timestamp));
    sanitizeDetail(detail, safeDetail, sizeof(safeDetail));

    int lineLength =
        snprintf(line, sizeof(line), "%s | %s | %s\n", timestamp, eventType, safeDetail);

    if (lineLength <= 0) {
        xSemaphoreGive(traceMutex);
        return;
    }

    size_t writeLength = min((size_t)lineLength, sizeof(line) - 1);

    File file = LittleFS.open(STATE_TRACE_PATH, "a");

    if (file && file.size() + writeLength > STATE_TRACE_MAX_SIZE) {
        file.close();
        LittleFS.remove(STATE_TRACE_PATH);
        file = LittleFS.open(STATE_TRACE_PATH, "w");
        traceSize = 0;
    }

    if (file) {
        traceSize += file.write(reinterpret_cast<const uint8_t*>(line), writeLength);

        file.close();
    }

    xSemaphoreGive(traceMutex);
}

void stateTraceLogBoot(uint32_t bootNumber, const String& resetReason) {
    String detail = "boot=";

    detail += String(bootNumber);
    detail += " reset=";
    detail += resetReason;

    stateTraceLog("BOOT", detail);
}

size_t getStateTraceSize() {
    return traceAvailable ? traceSize : 0;
}

bool clearStateTrace() {
    if (!traceAvailable || xSemaphoreTake(traceMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool cleared = !LittleFS.exists(STATE_TRACE_PATH) || LittleFS.remove(STATE_TRACE_PATH);

    if (cleared)
        traceSize = 0;

    xSemaphoreGive(traceMutex);

    return cleared;
}

bool streamStateTrace(Print& output) {
    if (!traceAvailable || xSemaphoreTake(traceMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    File file = LittleFS.open(STATE_TRACE_PATH, "r");

    if (!file) {
        xSemaphoreGive(traceMutex);
        return true;
    }

    uint8_t buffer[256];

    while (file.available()) {
        size_t bytesRead = file.read(buffer, sizeof(buffer));

        if (bytesRead == 0)
            break;

        output.write(buffer, bytesRead);
    }

    file.close();
    xSemaphoreGive(traceMutex);

    return true;
}

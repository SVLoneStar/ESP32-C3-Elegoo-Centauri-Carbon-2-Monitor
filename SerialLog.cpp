#include "SerialLog.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>

namespace {
constexpr TickType_t SERIAL_LOG_WAIT_TICKS = pdMS_TO_TICKS(5);
SemaphoreHandle_t serialMutex = nullptr;
TaskHandle_t blockOwner = nullptr;

void formatLine(char* destination, size_t destinationSize, const char* format,
                va_list arguments) {
    vsnprintf(destination, destinationSize, format, arguments);
}
}

void initializeSerialLog() {
    if (serialMutex == nullptr)
        serialMutex = xSemaphoreCreateMutex();
}

void serialLogLine(const char* line) {
    if (serialMutex == nullptr || line == nullptr ||
        xSemaphoreTake(serialMutex, SERIAL_LOG_WAIT_TICKS) != pdTRUE) {
        return;
    }

    Serial.println(line);
    xSemaphoreGive(serialMutex);
}

void serialLogLinef(const char* format, ...) {
    char line[256];
    va_list arguments;
    va_start(arguments, format);
    formatLine(line, sizeof(line), format, arguments);
    va_end(arguments);
    serialLogLine(line);
}

bool beginSerialLogBlock() {
    if (serialMutex == nullptr || xSemaphoreTake(serialMutex, SERIAL_LOG_WAIT_TICKS) != pdTRUE)
        return false;

    blockOwner = xTaskGetCurrentTaskHandle();
    return true;
}

void serialLogBlockLine(const char* line) {
    if (line != nullptr && blockOwner == xTaskGetCurrentTaskHandle())
        Serial.println(line);
}

void serialLogBlockLinef(const char* format, ...) {
    if (blockOwner != xTaskGetCurrentTaskHandle())
        return;

    char line[256];
    va_list arguments;
    va_start(arguments, format);
    formatLine(line, sizeof(line), format, arguments);
    va_end(arguments);
    Serial.println(line);
}

void endSerialLogBlock() {
    if (serialMutex == nullptr || blockOwner != xTaskGetCurrentTaskHandle())
        return;

    blockOwner = nullptr;
    xSemaphoreGive(serialMutex);
}

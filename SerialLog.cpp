#include "SerialLog.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <string.h>

namespace {
constexpr TickType_t SERIAL_LOG_WAIT_TICKS = pdMS_TO_TICKS(5);
constexpr size_t SERIAL_LOG_LINE_SIZE = 256;
constexpr size_t SERIAL_LOG_BLOCK_SIZE = 1024;
SemaphoreHandle_t serialMutex = nullptr;
TaskHandle_t blockOwner = nullptr;
char blockBuffer[SERIAL_LOG_BLOCK_SIZE];
size_t blockLength = 0;
uint32_t shortWriteCount = 0;
uint32_t droppedWriteCount = 0;

void formatLine(char* destination, size_t destinationSize, const char* format,
                va_list arguments) {
    vsnprintf(destination, destinationSize, format, arguments);
}

void writeCompleteBuffer(const char* buffer, size_t length) {
    if (buffer == nullptr || length == 0)
        return;

    size_t written = Serial.write((const uint8_t*)buffer, length);
    if (written == 0)
        droppedWriteCount++;
    else if (written < length)
        shortWriteCount++;
}

void writeCompleteLine(const char* line) {
    if (line == nullptr)
        return;

    char output[SERIAL_LOG_LINE_SIZE + 2];
    size_t lineLength = strnlen(line, SERIAL_LOG_LINE_SIZE - 1);
    memcpy(output, line, lineLength);
    output[lineLength] = '\r';
    output[lineLength + 1] = '\n';
    writeCompleteBuffer(output, lineLength + 2);
}

void appendBlockLine(const char* line) {
    if (line == nullptr)
        return;

    size_t lineLength = strlen(line);
    size_t available = SERIAL_LOG_BLOCK_SIZE - blockLength;
    if (lineLength + 2 > available) {
        droppedWriteCount++;
        return;
    }

    memcpy(blockBuffer + blockLength, line, lineLength);
    blockLength += lineLength;
    blockBuffer[blockLength++] = '\r';
    blockBuffer[blockLength++] = '\n';
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

    writeCompleteLine(line);
    xSemaphoreGive(serialMutex);
}

void serialLogLinef(const char* format, ...) {
    char line[SERIAL_LOG_LINE_SIZE];
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
    blockLength = 0;
    return true;
}

void serialLogBlockLine(const char* line) {
    if (line != nullptr && blockOwner == xTaskGetCurrentTaskHandle())
        appendBlockLine(line);
}

void serialLogBlockLinef(const char* format, ...) {
    if (blockOwner != xTaskGetCurrentTaskHandle())
        return;

    char line[SERIAL_LOG_LINE_SIZE];
    va_list arguments;
    va_start(arguments, format);
    formatLine(line, sizeof(line), format, arguments);
    va_end(arguments);
    appendBlockLine(line);
}

void endSerialLogBlock() {
    if (serialMutex == nullptr || blockOwner != xTaskGetCurrentTaskHandle())
        return;

    writeCompleteBuffer(blockBuffer, blockLength);
    blockLength = 0;
    blockOwner = nullptr;
    xSemaphoreGive(serialMutex);
}

uint32_t getSerialLogShortWriteCount() {
    return shortWriteCount;
}

uint32_t getSerialLogDroppedWriteCount() {
    return droppedWriteCount;
}

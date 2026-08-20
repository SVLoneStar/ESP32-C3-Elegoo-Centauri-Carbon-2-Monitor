#pragma once

#include <Arduino.h>

void initializeStateTrace();
void stateTraceLog(const char* eventType, const String& detail);
void stateTraceLogBoot(uint32_t bootNumber, const String& resetReason);
size_t getStateTraceSize();
bool clearStateTrace();
bool streamStateTrace(Print& output);

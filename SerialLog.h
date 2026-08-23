#pragma once

#include <Arduino.h>

void initializeSerialLog();
void serialLogLine(const char* line);
void serialLogLinef(const char* format, ...);
bool beginSerialLogBlock();
void serialLogBlockLine(const char* line);
void serialLogBlockLinef(const char* format, ...);
void endSerialLogBlock();

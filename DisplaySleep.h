#pragma once

#include <Arduino.h>

void initializeDisplaySleep();
void maintainDisplaySleep();
bool displaySleepActive();
const char* displaySleepStateText();
String configuredDisplaySleepTimeoutText();

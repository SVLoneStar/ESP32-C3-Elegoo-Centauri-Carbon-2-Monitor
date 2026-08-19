#pragma once
#include "AppState.h"

void initBootCounter();
String resetReasonText(esp_reset_reason_t reason);
void printResetReason();
String getUptimeString();
void printStatus();

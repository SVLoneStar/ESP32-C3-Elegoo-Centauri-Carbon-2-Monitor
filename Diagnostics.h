#pragma once
#include "AppState.h"

void initBootCounter();
String resetReasonText(esp_reset_reason_t reason);
String resetReasonWithCode(esp_reset_reason_t reason);
void printResetReason();
String getUptimeString();
void printStatus();
void recordBlockingCall(const char* functionName, unsigned long elapsedMs);
void recordLoopDuration(unsigned long elapsedMs);
uint32_t getMaximumLoopDuration();
uint32_t getLoopOver100MsCount();
uint32_t getLoopOver500MsCount();
uint32_t getLoopOver1SecondCount();
uint32_t getLoopOver5SecondsCount();

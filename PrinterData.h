#pragma once
#include "AppState.h"

bool invalidState(String value);
float stateToFloat(const String& state);
int stateToInt(const String& state);
void initializePrinter();
bool printerIsPrinting();
int getRemainingMinutes();
String formatRemaining();
String calculateETA();
String speedMode(float value);
void updatePrinterEntity(const String& entity, const String& state);

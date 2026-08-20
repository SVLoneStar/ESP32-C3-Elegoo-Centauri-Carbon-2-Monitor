#pragma once
#include "AppState.h"

enum PrinterState {
    PRINTER_STATE_UNKNOWN,
    PRINTER_STATE_IDLE,
    PRINTER_STATE_PRINTING,
    PRINTER_STATE_PAUSED,
    PRINTER_STATE_ERROR,
    PRINTER_STATE_PRINT_COMPLETE
};

bool invalidState(String value);
float stateToFloat(const String& state);
int stateToInt(const String& state);
void initializePrinter();
void initializePrinterEntityIds();
bool printerIsPrinting();
void updatePrinterStateMachine();
PrinterState getPrinterState();
const char* printerStateText(PrinterState state);
const char* getLastPrinterStateTransition();
const char* getPrintCompletionTime();
int getPrintCompletionCurrentLayer();
int getPrintCompletionTotalLayers();
int getRemainingMinutes();
String formatRemaining();
String calculateETA();
String speedMode(float value);
void updatePrinterEntity(const String& entity, const String& state);

#include "PrinterData.h"
#include "Config.h"
#include "StateTrace.h"
#include "Diagnostics.h"

namespace {
const unsigned long PRINT_COMPLETE_DISPLAY_DURATION_MS = 60000UL;

const unsigned long COMPLETION_CONFIRMATION_WINDOW_MS = 10000UL;

enum StatusSignal {
    SIGNAL_UNKNOWN,
    SIGNAL_IDLE,
    SIGNAL_PRINTING,
    SIGNAL_PAUSED,
    SIGNAL_ERROR,
    SIGNAL_COMPLETE,
    SIGNAL_STOPPING,
    SIGNAL_CANCELLED
};

enum TerminalSignal {
    TERMINAL_SIGNAL_NONE,
    TERMINAL_SIGNAL_COMPLETE,
    TERMINAL_SIGNAL_STOPPED
};

PrinterState resolvedPrinterState = PRINTER_STATE_UNKNOWN;

bool stateResolutionDirty = true;

bool completionEligible = false;

unsigned long lastActiveStateTime = 0;

unsigned long completionScreenStarted = 0;

bool activeIdleIgnoredLogged = false;

TerminalSignal pendingTerminalSignal = TERMINAL_SIGNAL_NONE;

char completionTime[6] = "--:--";

int lastValidActiveCurrentLayer = -1;

int lastValidActiveTotalLayers = -1;

int completionCurrentLayer = -1;

int completionTotalLayers = -1;

char lastPrinterStateTransition[40] = "NONE";

const size_t MAX_OBSERVED_UNKNOWN_STATUSES = 16;

char observedUnknownStatuses[MAX_OBSERVED_UNKNOWN_STATUSES][80];

size_t observedUnknownStatusCount = 0;

bool unknownStatusCapacityReported = false;

String normalizedState(const String& value) {
    String normalized = value;

    normalized.trim();
    normalized.toLowerCase();

    return normalized;
}

bool activePrintStatusObserved() {
    return printer.printStatus.equalsIgnoreCase("preheating") ||
           printer.printStatus.equalsIgnoreCase("printing") ||
           printer.printStatus.equalsIgnoreCase("pausing") ||
           printer.printStatus.equalsIgnoreCase("paused");
}

void clearCompletionLayerCache() {
    lastValidActiveCurrentLayer = -1;
    lastValidActiveTotalLayers = -1;
    completionCurrentLayer = -1;
    completionTotalLayers = -1;
}

void observeStatusValue(const char* source, const String& rawValue) {
    String value = normalizedState(rawValue);

    if (value.length() == 0 || value == "printing" ||
        (strcmp(source, "print_status") == 0 &&
         (value == "idle" || value == "preheating" || value == "pausing" || value == "paused" ||
          value == "stopping" || value == "stopped" || value == "complete" ||
          value == "unavailable"))) {
        return;
    }

    char observation[80];

    snprintf(observation, sizeof(observation), "%s=%s", source, value.c_str());

    for (size_t i = 0; i < observedUnknownStatusCount; i++) {
        if (strcmp(observedUnknownStatuses[i], observation) == 0)
            return;
    }

    if (observedUnknownStatusCount >= MAX_OBSERVED_UNKNOWN_STATUSES) {
        if (!unknownStatusCapacityReported) {
            serialDiagnostic("Unknown printer status log capacity reached");

            unknownStatusCapacityReported = true;
        }

        return;
    }

    strlcpy(observedUnknownStatuses[observedUnknownStatusCount], observation,
            sizeof(observedUnknownStatuses[0]));

    observedUnknownStatusCount++;

    serialDiagnostic("Unmapped printer status: %s", observation);
}

StatusSignal resolveStatusSignal() {
    String currentStatus = normalizedState(printer.currentStatus);

    String printStatus = normalizedState(printer.printStatus);

    // The print-status entity is authoritative. These are the exact values
    // observed in Home Assistant history; add mappings only from new evidence.
    if (printStatus == "idle")
        return SIGNAL_IDLE;

    if (printStatus == "preheating" || printStatus == "printing")
        return SIGNAL_PRINTING;

    if (printStatus == "pausing" || printStatus == "paused")
        return SIGNAL_PAUSED;

    if (printStatus == "stopping")
        return SIGNAL_STOPPING;

    if (printStatus == "stopped")
        return SIGNAL_CANCELLED;

    if (printStatus == "complete")
        return SIGNAL_COMPLETE;

    if (printStatus == "unavailable")
        return SIGNAL_UNKNOWN;

    // "printing" is also evidenced on the generic current-status entity.
    // Use it only when print-status is empty; an available print-status value
    // remains authoritative, including values not mapped yet.
    if (printStatus.length() == 0 && currentStatus == "printing")
        return SIGNAL_PRINTING;

    return SIGNAL_UNKNOWN;
}

void captureCompletionTime() {
    strlcpy(completionTime, "--:--", sizeof(completionTime));

    time_t now = time(nullptr);

    if (now < 1000000000UL)
        return;

    struct tm localTime;

    if (!localtime_r(&now, &localTime))
        return;

    snprintf(completionTime, sizeof(completionTime), "%02d:%02d", localTime.tm_hour,
             localTime.tm_min);
}

void transitionTo(PrinterState nextState) {
    if (nextState == resolvedPrinterState)
        return;

    bool newPrintStarting =
        (nextState == PRINTER_STATE_PRINTING || nextState == PRINTER_STATE_PAUSED) &&
        (resolvedPrinterState == PRINTER_STATE_IDLE ||
         resolvedPrinterState == PRINTER_STATE_PRINT_COMPLETE);

    if (newPrintStarting && !activePrintStatusObserved())
        clearCompletionLayerCache();

    const char* previousStateText = printerStateText(resolvedPrinterState);

    const char* nextStateText = printerStateText(nextState);

    serialDiagnostic("Printer state: %s -> %s", previousStateText, nextStateText);

    String transition = previousStateText;
    transition += " -> ";
    transition += nextStateText;

    stateTraceLog("STATE_TRANSITION", transition);
    snprintf(lastPrinterStateTransition, sizeof(lastPrinterStateTransition), "%s -> %s",
             previousStateText, nextStateText);

    resolvedPrinterState = nextState;

    if (nextState == PRINTER_STATE_PRINTING || nextState == PRINTER_STATE_PAUSED) {
        completionEligible = true;
        lastActiveStateTime = millis();
    }

    if (nextState == PRINTER_STATE_PRINT_COMPLETE) {
        completionEligible = false;
        completionScreenStarted = millis();
        completionCurrentLayer = lastValidActiveCurrentLayer;
        completionTotalLayers = lastValidActiveTotalLayers;
        captureCompletionTime();
    }

    if (nextState == PRINTER_STATE_ERROR)
        completionEligible = false;
}

const char* terminalSignalText(TerminalSignal signal) {
    return signal == TERMINAL_SIGNAL_COMPLETE ? "complete" : "stopped";
}

void latchTerminalSignal(TerminalSignal signal) {
    pendingTerminalSignal = signal;
    stateTraceLog("TERMINAL_SIGNAL_LATCHED", terminalSignalText(signal));
}

bool consumePendingTerminalSignal() {
    if (pendingTerminalSignal == TERMINAL_SIGNAL_NONE)
        return false;

    TerminalSignal signal = pendingTerminalSignal;
    pendingTerminalSignal = TERMINAL_SIGNAL_NONE;

    stateTraceLog("TERMINAL_SIGNAL_CONSUMED", terminalSignalText(signal));
    activeIdleIgnoredLogged = false;

    if (signal == TERMINAL_SIGNAL_STOPPED) {
        completionEligible = false;
        transitionTo(PRINTER_STATE_IDLE);
        return true;
    }

    if (resolvedPrinterState == PRINTER_STATE_PRINTING ||
        resolvedPrinterState == PRINTER_STATE_PAUSED ||
        (completionEligible &&
         millis() - lastActiveStateTime <= COMPLETION_CONFIRMATION_WINDOW_MS)) {
        transitionTo(PRINTER_STATE_PRINT_COMPLETE);
    } else if (resolvedPrinterState == PRINTER_STATE_UNKNOWN) {
        transitionTo(PRINTER_STATE_IDLE);
    }

    return true;
}

} // namespace

// GENERIC HELPERS
// ============================================================

bool invalidState(String value) {
    value.trim();
    value.toLowerCase();

    return value.length() == 0 || value == "unknown" || value == "unavailable" || value == "none" ||
           value == "null";
}

float stateToFloat(const String& state) {
    if (invalidState(state))
        return NAN;

    return state.toFloat();
}

int stateToInt(const String& state) {
    if (invalidState(state))
        return -1;

    return state.toInt();
}

// ============================================================
// PRINTER INIT
// ============================================================

void initializePrinterEntityIds() {
    const char* prefix = appConfig.printerEntityPrefix;

    snprintf(ENTITY_REMAINING, sizeof(ENTITY_REMAINING), "sensor.%s_remaining_print_time", prefix);
    snprintf(ENTITY_TOTAL_LAYERS, sizeof(ENTITY_TOTAL_LAYERS), "sensor.%s_total_layers", prefix);
    snprintf(ENTITY_CURRENT_LAYER, sizeof(ENTITY_CURRENT_LAYER), "sensor.%s_current_layer", prefix);
    snprintf(ENTITY_PROGRESS, sizeof(ENTITY_PROGRESS), "sensor.%s_percent_complete", prefix);
    snprintf(ENTITY_CURRENT_STATUS, sizeof(ENTITY_CURRENT_STATUS), "sensor.%s_aktueller_status",
             prefix);
    snprintf(ENTITY_PRINT_STATUS, sizeof(ENTITY_PRINT_STATUS), "sensor.%s_druckstatus", prefix);
    snprintf(ENTITY_PRINT_ERROR, sizeof(ENTITY_PRINT_ERROR), "sensor.%s_druckfehler", prefix);
    snprintf(ENTITY_ERROR_REASON, sizeof(ENTITY_ERROR_REASON), "sensor.%s_fehlerstatusgrund", prefix);
    snprintf(ENTITY_BOX_TEMP, sizeof(ENTITY_BOX_TEMP), "sensor.%s_box_temp", prefix);
    snprintf(ENTITY_NOZZLE_TEMP, sizeof(ENTITY_NOZZLE_TEMP), "sensor.%s_nozzle_temperature", prefix);
    snprintf(ENTITY_BED_TEMP, sizeof(ENTITY_BED_TEMP), "sensor.%s_bed_temperature", prefix);
    snprintf(ENTITY_PRINT_SPEED, sizeof(ENTITY_PRINT_SPEED), "sensor.%s_print_speed", prefix);
}

void initializePrinter() {
    printer.currentStatus = "";

    printer.printStatus = "";

    printer.remainingTime = "";

    printer.printError = "";

    printer.errorReason = "";

    printer.progress = NAN;

    printer.currentLayer = -1;

    printer.totalLayers = -1;

    printer.nozzleTemp = NAN;

    printer.bedTemp = NAN;

    printer.boxTemp = NAN;

    printer.printSpeed = NAN;

    resolvedPrinterState = PRINTER_STATE_UNKNOWN;

    stateResolutionDirty = true;

    completionEligible = false;

    activeIdleIgnoredLogged = false;

    pendingTerminalSignal = TERMINAL_SIGNAL_NONE;

    observedUnknownStatusCount = 0;

    unknownStatusCapacityReported = false;

    completionTime[0] = '-';
    completionTime[1] = '-';
    completionTime[2] = ':';
    completionTime[3] = '-';
    completionTime[4] = '-';
    completionTime[5] = '\0';

    strlcpy(lastPrinterStateTransition, "NONE", sizeof(lastPrinterStateTransition));

    clearCompletionLayerCache();
}

// ============================================================
// PRINTING DETECTION
// ============================================================

bool printerIsPrinting() {
    return resolvedPrinterState == PRINTER_STATE_PRINTING;
}

void updatePrinterStateMachine() {
    if (consumePendingTerminalSignal())
        return;

    if (resolvedPrinterState == PRINTER_STATE_PRINT_COMPLETE) {
        if (stateResolutionDirty) {
            stateResolutionDirty = false;

            StatusSignal signal = resolveStatusSignal();

            if (signal == SIGNAL_PRINTING) {
                transitionTo(PRINTER_STATE_PRINTING);
                return;
            }

            if (signal == SIGNAL_PAUSED) {
                transitionTo(PRINTER_STATE_PAUSED);
                return;
            }

            if (signal == SIGNAL_ERROR) {
                transitionTo(PRINTER_STATE_ERROR);
                return;
            }

            if (signal == SIGNAL_CANCELLED) {
                transitionTo(PRINTER_STATE_IDLE);
                return;
            }
        }

        if (millis() - completionScreenStarted >= PRINT_COMPLETE_DISPLAY_DURATION_MS) {
            transitionTo(PRINTER_STATE_IDLE);
        }

        return;
    }

    if (!stateResolutionDirty) {
        if (completionEligible && resolvedPrinterState != PRINTER_STATE_PRINTING &&
            resolvedPrinterState != PRINTER_STATE_PAUSED &&
            millis() - lastActiveStateTime > COMPLETION_CONFIRMATION_WINDOW_MS) {
            completionEligible = false;
        }

        return;
    }

    stateResolutionDirty = false;

    StatusSignal signal = resolveStatusSignal();

    switch (signal) {
    case SIGNAL_PRINTING:
        activeIdleIgnoredLogged = false;
        transitionTo(PRINTER_STATE_PRINTING);
        break;

    case SIGNAL_PAUSED:
        activeIdleIgnoredLogged = false;
        transitionTo(PRINTER_STATE_PAUSED);
        break;

    case SIGNAL_ERROR:
        activeIdleIgnoredLogged = false;
        transitionTo(PRINTER_STATE_ERROR);
        break;

    case SIGNAL_COMPLETE:
        activeIdleIgnoredLogged = false;
        if (resolvedPrinterState == PRINTER_STATE_PRINTING ||
            resolvedPrinterState == PRINTER_STATE_PAUSED ||
            (completionEligible &&
             millis() - lastActiveStateTime <= COMPLETION_CONFIRMATION_WINDOW_MS)) {
            transitionTo(PRINTER_STATE_PRINT_COMPLETE);
        } else if (resolvedPrinterState == PRINTER_STATE_UNKNOWN) {
            transitionTo(PRINTER_STATE_IDLE);
        }
        break;

    case SIGNAL_STOPPING:
        activeIdleIgnoredLogged = false;
        // Preserve the current state until the authoritative print-status
        // entity reports the observed terminal value "stopped".
        break;

    case SIGNAL_CANCELLED:
        activeIdleIgnoredLogged = false;
        completionEligible = false;
        transitionTo(PRINTER_STATE_IDLE);
        break;

    case SIGNAL_IDLE:
        if (resolvedPrinterState == PRINTER_STATE_PRINTING ||
            resolvedPrinterState == PRINTER_STATE_PAUSED) {
            if (!activeIdleIgnoredLogged) {
                stateTraceLog("ACTIVE_IDLE_IGNORED", printerStateText(resolvedPrinterState));
                activeIdleIgnoredLogged = true;
            }

            break;
        }

        activeIdleIgnoredLogged = false;
        transitionTo(PRINTER_STATE_IDLE);
        break;

    case SIGNAL_UNKNOWN:
    default:
        activeIdleIgnoredLogged = false;
        if (resolvedPrinterState == PRINTER_STATE_UNKNOWN) {
            transitionTo(PRINTER_STATE_IDLE);
        }
        break;
    }
}

PrinterState getPrinterState() {
    return resolvedPrinterState;
}

const char* printerStateText(PrinterState state) {
    switch (state) {
    case PRINTER_STATE_IDLE:
        return "IDLE";

    case PRINTER_STATE_PRINTING:
        return "PRINTING";

    case PRINTER_STATE_PAUSED:
        return "PAUSED";

    case PRINTER_STATE_ERROR:
        return "ERROR";

    case PRINTER_STATE_PRINT_COMPLETE:
        return "PRINT_COMPLETE";

    case PRINTER_STATE_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

const char* getLastPrinterStateTransition() {
    return lastPrinterStateTransition;
}

const char* getPrintCompletionTime() {
    return completionTime;
}

int getPrintCompletionCurrentLayer() {
    return completionCurrentLayer;
}

int getPrintCompletionTotalLayers() {
    return completionTotalLayers;
}

// REMAINING
// ============================================================

int getRemainingMinutes() {
    if (invalidState(printer.remainingTime)) {
        return -1;
    }

    float value = printer.remainingTime.toFloat();

    if (value < 0)
        value = 0;

    return (int)round(value);
}

String formatRemaining() {
    int total = getRemainingMinutes();

    if (total < 0)
        return "--";

    int hours = total / 60;

    int minutes = total % 60;

    if (hours > 0) {
        char buffer[16];

        snprintf(buffer, sizeof(buffer), "%dh %02dm", hours, minutes);

        return String(buffer);
    }

    return String(minutes) + " min";
}

// ============================================================
// ETA
// ============================================================

String calculateETA() {
    int remaining = getRemainingMinutes();

    if (remaining < 0)
        return "--:--";

    time_t now = time(nullptr);

    if (now < 1000000000UL)
        return "--:--";

    time_t eta = now + ((time_t)remaining * 60);

    struct tm t;

    if (!localtime_r(&eta, &t)) {
        return "--:--";
    }

    char buffer[6];

    snprintf(buffer, sizeof(buffer), "%02d:%02d", t.tm_hour, t.tm_min);

    return String(buffer);
}

// ============================================================
// SPEED MODE
// ============================================================

String speedMode(float value) {
    if (isnan(value))
        return "--";

    int speed = (int)round(value);

    if (speed == 50)
        return "SI";

    if (speed == 100)
        return "BA";

    if (speed == 150)
        return "SP";

    if (speed == 200)
        return "LU";

    return String(speed);
}

// UPDATE PRINTER ENTITY
// ============================================================

void updatePrinterEntity(const String& entity, const String& state) {
    if (entity == ENTITY_CURRENT_STATUS) {
        if (printer.currentStatus != state) {
            printer.currentStatus = state;

            observeStatusValue("current_status", state);

            stateTraceLog("HA_CURRENT_STATUS", state);

            stateResolutionDirty = true;
        }
    }

    else if (entity == ENTITY_PRINT_STATUS) {
        if (printer.printStatus != state) {
            bool incomingActive = state.equalsIgnoreCase("preheating") ||
                                  state.equalsIgnoreCase("printing") ||
                                  state.equalsIgnoreCase("pausing") ||
                                  state.equalsIgnoreCase("paused");

            if (incomingActive && !activePrintStatusObserved() &&
                (resolvedPrinterState == PRINTER_STATE_IDLE ||
                 resolvedPrinterState == PRINTER_STATE_PRINT_COMPLETE)) {
                clearCompletionLayerCache();
            }

            if (state.equalsIgnoreCase("complete")) {
                latchTerminalSignal(TERMINAL_SIGNAL_COMPLETE);
            } else if (state.equalsIgnoreCase("stopped")) {
                latchTerminalSignal(TERMINAL_SIGNAL_STOPPED);
            }

            printer.printStatus = state;

            observeStatusValue("print_status", state);

            stateTraceLog("HA_PRINT_STATUS", state);

            stateResolutionDirty = true;
        }
    }

    else if (entity == ENTITY_REMAINING) {
        if (printer.remainingTime != state) {
            printer.remainingTime = state;

            timeDirty = true;
        }
    }

    else if (entity == ENTITY_PROGRESS) {
        float value = stateToFloat(state);

        if (isnan(value) != isnan(printer.progress) ||
            (!isnan(value) && fabs(value - printer.progress) > 0.01f)) {
            printer.progress = value;

            progressDirty = true;
        }
    }

    else if (entity == ENTITY_CURRENT_LAYER) {
        int value = stateToInt(state);

        if (value >= 0 && (resolvedPrinterState == PRINTER_STATE_PRINTING ||
                           resolvedPrinterState == PRINTER_STATE_PAUSED ||
                           activePrintStatusObserved())) {
            lastValidActiveCurrentLayer = value;
        }

        if (value != printer.currentLayer) {
            printer.currentLayer = value;

            layerSpeedDirty = true;
        }
    }

    else if (entity == ENTITY_TOTAL_LAYERS) {
        int value = stateToInt(state);

        if (value >= 0 && (resolvedPrinterState == PRINTER_STATE_PRINTING ||
                           resolvedPrinterState == PRINTER_STATE_PAUSED ||
                           activePrintStatusObserved())) {
            lastValidActiveTotalLayers = value;
        }

        if (value != printer.totalLayers) {
            printer.totalLayers = value;

            layerSpeedDirty = true;
        }
    }

    else if (entity == ENTITY_NOZZLE_TEMP) {
        float value = stateToFloat(state);

        if (isnan(value) != isnan(printer.nozzleTemp) ||
            (!isnan(value) && fabs(value - printer.nozzleTemp) >= 0.5f)) {
            printer.nozzleTemp = value;

            nozzleDirty = true;
        }
    }

    else if (entity == ENTITY_BED_TEMP) {
        float value = stateToFloat(state);

        if (isnan(value) != isnan(printer.bedTemp) ||
            (!isnan(value) && fabs(value - printer.bedTemp) >= 0.5f)) {
            printer.bedTemp = value;

            bedDirty = true;
        }
    }

    else if (entity == ENTITY_BOX_TEMP) {
        float value = stateToFloat(state);

        if (isnan(value) != isnan(printer.boxTemp) ||
            (!isnan(value) && fabs(value - printer.boxTemp) >= 0.5f)) {
            printer.boxTemp = value;

            chamberDirty = true;
        }
    }

    else if (entity == ENTITY_PRINT_SPEED) {
        float value = stateToFloat(state);

        if (isnan(value) != isnan(printer.printSpeed) ||
            (!isnan(value) && fabs(value - printer.printSpeed) > 0.01f)) {
            printer.printSpeed = value;

            layerSpeedDirty = true;
        }
    }

    else if (entity == ENTITY_PRINT_ERROR) {
        if (printer.printError != state) {
            printer.printError = state;
            stateResolutionDirty = true;
            errorDetailsDirty = true;
            stateTraceLog("HA_PRINT_ERROR", state);
        }
    }

    else if (entity == ENTITY_ERROR_REASON) {
        if (printer.errorReason != state) {
            printer.errorReason = state;
            stateResolutionDirty = true;
            errorDetailsDirty = true;
            stateTraceLog("HA_ERROR_REASON", state);
        }
    }
}

#include "BootStage.h"

#include "StateTrace.h"
#include "Diagnostics.h"

#include <Preferences.h>

namespace {
Preferences bootStagePreferences;
BootStage currentStage = BOOT_STAGE_NONE;
BootStage previousStage = BOOT_STAGE_NONE;
bool persistenceAvailable = false;
bool serialLoggingEnabled = false;
bool traceLoggingEnabled = false;
}

const char* bootStageText(BootStage stage) {
    switch (stage) {
    case BOOT_STAGE_SERIAL_BEGIN: return "SERIAL_BEGIN";
    case BOOT_STAGE_SERIAL_READY: return "SERIAL_READY";
    case BOOT_STAGE_CONFIG_BEGIN: return "CONFIG_BEGIN";
    case BOOT_STAGE_CONFIG_READY: return "CONFIG_READY";
    case BOOT_STAGE_TRACE_READY: return "STATE_TRACE_READY";
    case BOOT_STAGE_TFT_BEGIN: return "TFT_BEGIN";
    case BOOT_STAGE_TFT_READY: return "TFT_READY";
    case BOOT_STAGE_WIFI_MODE_BEGIN: return "WIFI_MODE_BEGIN";
    case BOOT_STAGE_WIFI_MODE_READY: return "WIFI_MODE_READY";
    case BOOT_STAGE_WIFI_MODE_FAILED: return "WIFI_MODE_FAILED";
    case BOOT_STAGE_WIFI_MANAGER_BEGIN: return "WIFI_MANAGER_BEGIN";
    case BOOT_STAGE_STORED_WIFI_BEGIN: return "STORED_WIFI_CONNECT_BEGIN";
    case BOOT_STAGE_CAPTIVE_PORTAL_STARTED: return "CAPTIVE_PORTAL_STARTED";
    case BOOT_STAGE_CAPTIVE_PORTAL_INVALID: return "CAPTIVE_PORTAL_AP_INVALID";
    case BOOT_STAGE_CAPTIVE_PORTAL_WEB_READY: return "CAPTIVE_PORTAL_WEB_READY";
    case BOOT_STAGE_WIFI_CONNECTED: return "WIFI_CONNECTED";
    case BOOT_STAGE_WIFI_CONNECT_FAILED: return "WIFI_CONNECT_FAILED";
    case BOOT_STAGE_NTP_BEGIN: return "NTP_BEGIN";
    case BOOT_STAGE_NTP_READY: return "NTP_READY";
    case BOOT_STAGE_INITIAL_REST_BEGIN: return "INITIAL_HA_REST_BEGIN";
    case BOOT_STAGE_INITIAL_REST_COMPLETE: return "INITIAL_HA_REST_COMPLETE";
    case BOOT_STAGE_WEBSOCKET_INIT_BEGIN: return "WEBSOCKET_INIT_BEGIN";
    case BOOT_STAGE_WEBSOCKET_INIT_READY: return "WEBSOCKET_INIT_READY";
    case BOOT_STAGE_WEBSOCKET_CONNECT_BEGIN: return "WEBSOCKET_CONNECT_BEGIN";
    case BOOT_STAGE_WEBSOCKET_CONNECT_OPENED: return "WEBSOCKET_CONNECT_OPENED";
    case BOOT_STAGE_WEBSOCKET_CONNECT_FAILED: return "WEBSOCKET_CONNECT_FAILED";
    case BOOT_STAGE_WEB_UI_TASK_BEGIN: return "WEB_UI_TASK_BEGIN";
    case BOOT_STAGE_WEB_UI_TASK_READY: return "WEB_UI_TASK_READY";
    case BOOT_STAGE_WEB_SERVER_BEGIN: return "WEB_SERVER_BEGIN";
    case BOOT_STAGE_WEB_SERVER_READY: return "WEB_SERVER_READY";
    case BOOT_STAGE_MDNS_BEGIN: return "MDNS_BEGIN";
    case BOOT_STAGE_MDNS_READY: return "MDNS_READY";
    case BOOT_STAGE_MDNS_FAILED: return "MDNS_FAILED";
    case BOOT_STAGE_SETUP_COMPLETE: return "SETUP_COMPLETE";
    default: return "NONE";
    }
}

void initializeBootStageTracking() {
    persistenceAvailable = bootStagePreferences.begin("boot-stage", false);
    if (persistenceAvailable)
        previousStage = (BootStage)bootStagePreferences.getUChar("stage", BOOT_STAGE_NONE);
}

void enableBootStageSerialLogging() {
    serialLoggingEnabled = true;
}

void enableBootStageTraceLogging() {
    traceLoggingEnabled = true;
}

void markBootStage(BootStage stage) {
    if (stage == currentStage)
        return;

    currentStage = stage;
    const char* label = bootStageText(stage);

    if (persistenceAvailable)
        bootStagePreferences.putUChar("stage", (uint8_t)stage);

    if (serialLoggingEnabled) {
        serialDiagnostic("BOOT_STAGE | %u | %s", (unsigned int)(uint8_t)stage, label);
    }

    if (traceLoggingEnabled)
        stateTraceLog("BOOT_STAGE", label);
}

BootStage getCurrentBootStage() {
    return currentStage;
}

BootStage getPreviousBootStage() {
    return previousStage;
}

bool isBootStagePersistenceAvailable() {
    return persistenceAvailable;
}

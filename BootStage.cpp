#include "BootStage.h"

#include "StateTrace.h"
#include "Diagnostics.h"

#include <Preferences.h>

namespace {
constexpr size_t MAX_BOOT_STAGE_RECORDS = 48;

struct BootStageRecord {
    BootStage stage;
    uint32_t uptimeMs;
    uint32_t deltaMs;
};

struct __attribute__((packed)) PersistedBootStage {
    uint8_t stage;
    uint32_t uptimeMs;
};

Preferences bootStagePreferences;
BootStage currentStage = BOOT_STAGE_NONE;
BootStage previousStage = BOOT_STAGE_NONE;
uint32_t currentStageUptimeMs = 0;
uint32_t previousStageUptimeMs = 0;
uint32_t lastStageUptimeMs = 0;
bool persistenceAvailable = false;
bool serialLoggingEnabled = false;
bool traceLoggingEnabled = false;
BootStageRecord stageRecords[MAX_BOOT_STAGE_RECORDS];
size_t stageRecordCount = 0;
size_t serialRecordIndex = 0;
size_t traceRecordIndex = 0;

void formatStageDetail(const BootStageRecord& record, char* destination, size_t destinationSize) {
    snprintf(destination, destinationSize, "code=%u stage=%s uptime_ms=%lu delta_ms=%lu",
             (unsigned int)(uint8_t)record.stage, bootStageText(record.stage),
             (unsigned long)record.uptimeMs, (unsigned long)record.deltaMs);
}

void emitSerialRecords() {
    while (serialRecordIndex < stageRecordCount) {
        char detail[128];
        formatStageDetail(stageRecords[serialRecordIndex], detail, sizeof(detail));
        serialDiagnostic("BOOT_STAGE | %s", detail);
        serialRecordIndex++;
    }
}

void emitTraceRecords() {
    while (traceRecordIndex < stageRecordCount) {
        char detail[128];
        formatStageDetail(stageRecords[traceRecordIndex], detail, sizeof(detail));
        stateTraceLog("BOOT_STAGE", detail);
        traceRecordIndex++;
    }
}

void recordBootStage(BootStage stage, uint32_t uptimeMs) {
    if (stage == currentStage)
        return;

    BootStageRecord record = {stage, uptimeMs, uptimeMs - lastStageUptimeMs};
    currentStage = stage;
    currentStageUptimeMs = uptimeMs;
    lastStageUptimeMs = uptimeMs;

    if (stageRecordCount < MAX_BOOT_STAGE_RECORDS)
        stageRecords[stageRecordCount++] = record;

    if (persistenceAvailable) {
        PersistedBootStage persisted = {(uint8_t)stage, uptimeMs};
        bootStagePreferences.putBytes("record", &persisted, sizeof(persisted));
    }

    if (serialLoggingEnabled)
        emitSerialRecords();
    if (traceLoggingEnabled)
        emitTraceRecords();
}
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
    case BOOT_STAGE_SETUP_ENTERED: return "SETUP_ENTERED";
    case BOOT_STAGE_TRACKING_READY: return "BOOT_TRACKING_READY";
    case BOOT_STAGE_STATE_TRACE_BEGIN: return "STATE_TRACE_BEGIN";
    case BOOT_STAGE_WIFI_MANAGER_COMPLETE: return "WIFI_MANAGER_COMPLETE";
    default: return "NONE";
    }
}

void initializeBootStageTracking(unsigned long setupEnteredAt) {
    persistenceAvailable = bootStagePreferences.begin("boot-stage", false);
    if (persistenceAvailable) {
        PersistedBootStage persisted = {};
        if (bootStagePreferences.getBytesLength("record") == sizeof(persisted)) {
            bootStagePreferences.getBytes("record", &persisted, sizeof(persisted));
            previousStage = (BootStage)persisted.stage;
            previousStageUptimeMs = persisted.uptimeMs;
        } else {
            previousStage =
                (BootStage)bootStagePreferences.getUChar("stage", BOOT_STAGE_NONE);
            previousStageUptimeMs = bootStagePreferences.getUInt("uptime", 0);
        }
    }

    recordBootStage(BOOT_STAGE_SETUP_ENTERED, setupEnteredAt);
    recordBootStage(BOOT_STAGE_TRACKING_READY, millis());
}

void enableBootStageSerialLogging() {
    serialLoggingEnabled = true;
    serialDiagnostic("BOOT_STAGE_PREVIOUS | code=%u stage=%s uptime_ms=%lu",
                     (unsigned int)(uint8_t)previousStage, bootStageText(previousStage),
                     (unsigned long)previousStageUptimeMs);
    emitSerialRecords();
}

void enableBootStageTraceLogging() {
    traceLoggingEnabled = true;
    char detail[128];
    snprintf(detail, sizeof(detail), "code=%u stage=%s uptime_ms=%lu",
             (unsigned int)(uint8_t)previousStage, bootStageText(previousStage),
             (unsigned long)previousStageUptimeMs);
    stateTraceLog("BOOT_STAGE_PREVIOUS", detail);
    emitTraceRecords();
}

void markBootStage(BootStage stage) {
    recordBootStage(stage, millis());
}

BootStage getCurrentBootStage() {
    return currentStage;
}

BootStage getPreviousBootStage() {
    return previousStage;
}

uint32_t getCurrentBootStageUptimeMs() {
    return currentStageUptimeMs;
}

uint32_t getPreviousBootStageUptimeMs() {
    return previousStageUptimeMs;
}

bool isBootStagePersistenceAvailable() {
    return persistenceAvailable;
}

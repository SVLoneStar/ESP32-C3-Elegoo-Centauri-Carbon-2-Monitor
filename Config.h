#pragma once

#include <Arduino.h>

const uint16_t CURRENT_CONFIG_VERSION = 2;

struct AppConfig {
    uint16_t configVersion;
    char deviceName[33];
    char homeAssistantHost[65];
    uint16_t homeAssistantPort;
    char homeAssistantToken[512];
    char timezone[65];
    char printerEntityPrefix[65];
    char weatherEntity[96];
    uint32_t weatherRefreshIntervalMs;
    uint32_t etaRemainingSwitchIntervalMs;
    bool stateTraceEnabled;
};

extern AppConfig appConfig;

void setConfigDefaults(AppConfig& config);
bool hasValidHomeAssistantConfig(const AppConfig& config);
bool hasValidPrinterEntityConfig(const AppConfig& config);

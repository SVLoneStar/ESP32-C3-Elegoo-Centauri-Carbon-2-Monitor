#pragma once

#include <Arduino.h>

const uint16_t CURRENT_CONFIG_VERSION = 5;
const int16_t WIFI_TX_POWER_DEFAULT = INT16_MAX;
const uint16_t DISPLAY_SLEEP_DEFAULT_MINUTES = 15;

struct WifiTxPowerOption {
    int16_t value;
    const char* label;
};

extern const WifiTxPowerOption WIFI_TX_POWER_OPTIONS[];
extern const size_t WIFI_TX_POWER_OPTION_COUNT;

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
    uint16_t displaySleepTimeoutMinutes;
    int16_t wifiTxPowerQuarterDbm;
    bool stateTraceEnabled;
    bool touchCalibrationValid;
    int16_t touchRawXMin;
    int16_t touchRawXMax;
    int16_t touchRawYMin;
    int16_t touchRawYMax;
    bool touchSwapAxes;
    bool touchInvertX;
    bool touchInvertY;
};

extern AppConfig appConfig;

void setConfigDefaults(AppConfig& config);
bool hasValidHomeAssistantConfig(const AppConfig& config);
bool hasValidPrinterEntityConfig(const AppConfig& config);
bool isSupportedWifiTxPower(int16_t value);
String wifiTxPowerText(int16_t value);
bool isSupportedDisplaySleepTimeout(uint16_t minutes);
String displaySleepTimeoutText(uint16_t minutes);

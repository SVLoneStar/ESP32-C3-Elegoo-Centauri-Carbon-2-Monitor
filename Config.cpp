#include "Config.h"

AppConfig appConfig;

const WifiTxPowerOption WIFI_TX_POWER_OPTIONS[] = {
    {84, "21 dBm"},   {82, "20.5 dBm"}, {80, "20 dBm"}, {78, "19.5 dBm"},
    {76, "19 dBm"},   {74, "18.5 dBm"}, {68, "17 dBm"}, {60, "15 dBm"},
    {52, "13 dBm"},   {44, "11 dBm"},   {34, "8.5 dBm"}, {28, "7 dBm"},
    {20, "5 dBm"},    {8, "2 dBm"},     {-4, "-1 dBm"}
};

const size_t WIFI_TX_POWER_OPTION_COUNT =
    sizeof(WIFI_TX_POWER_OPTIONS) / sizeof(WIFI_TX_POWER_OPTIONS[0]);

namespace {
void copyConfigText(char* destination, size_t destinationSize, const char* source) {
    if (destinationSize == 0)
        return;

    strlcpy(destination, source == nullptr ? "" : source, destinationSize);
}
} // namespace

void setConfigDefaults(AppConfig& config) {
    memset(&config, 0, sizeof(config));

    config.configVersion = CURRENT_CONFIG_VERSION;

    copyConfigText(config.deviceName, sizeof(config.deviceName), "cc2-monitor");

    copyConfigText(config.homeAssistantHost, sizeof(config.homeAssistantHost), "");

    config.homeAssistantPort = 8123;

    copyConfigText(config.homeAssistantToken, sizeof(config.homeAssistantToken), "");

    copyConfigText(config.timezone, sizeof(config.timezone), "UTC0");

    copyConfigText(config.printerEntityPrefix, sizeof(config.printerEntityPrefix),
                   "centauri_carbon_2");

    copyConfigText(config.weatherEntity, sizeof(config.weatherEntity), "");

    config.weatherRefreshIntervalMs = 15UL * 60UL * 1000UL;

    config.etaRemainingSwitchIntervalMs = 5000UL;

    config.wifiTxPowerQuarterDbm = WIFI_TX_POWER_DEFAULT;

    config.stateTraceEnabled = true;

    config.touchCalibrationValid = true;
    config.touchRawXMin = 410;
    config.touchRawXMax = 3850;
    config.touchRawYMin = 370;
    config.touchRawYMax = 3740;
    config.touchSwapAxes = false;
    config.touchInvertX = false;
    config.touchInvertY = false;
}

bool hasValidHomeAssistantConfig(const AppConfig& config) {
    return config.homeAssistantHost[0] != '\0' && config.homeAssistantPort != 0 &&
           config.homeAssistantToken[0] != '\0';
}

bool hasValidPrinterEntityConfig(const AppConfig& config) {
    if (config.printerEntityPrefix[0] == '\0')
        return false;

    for (size_t i = 0; config.printerEntityPrefix[i] != '\0'; i++) {
        char character = config.printerEntityPrefix[i];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }

    return true;
}

bool isSupportedWifiTxPower(int16_t value) {
    if (value == WIFI_TX_POWER_DEFAULT)
        return true;

    for (size_t i = 0; i < WIFI_TX_POWER_OPTION_COUNT; i++) {
        if (WIFI_TX_POWER_OPTIONS[i].value == value)
            return true;
    }
    return false;
}

String wifiTxPowerText(int16_t value) {
    if (value == WIFI_TX_POWER_DEFAULT)
        return "DEFAULT";

    for (size_t i = 0; i < WIFI_TX_POWER_OPTION_COUNT; i++) {
        if (WIFI_TX_POWER_OPTIONS[i].value == value)
            return WIFI_TX_POWER_OPTIONS[i].label;
    }

    int remainder = abs(value) % 4;
    unsigned int decimals = remainder == 0 ? 0 : remainder == 2 ? 1 : 2;
    return String((float)value / 4.0f, decimals) + " dBm";
}

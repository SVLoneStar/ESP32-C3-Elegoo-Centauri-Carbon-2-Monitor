#include "Config.h"

AppConfig appConfig;

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

    config.stateTraceEnabled = true;
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

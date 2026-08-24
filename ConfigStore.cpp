#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
const char* CONFIG_PATH = "/config.json";

const char* CONFIG_TEMP_PATH = "/config.tmp";

bool storeAvailable = false;

void copyJsonText(char* destination, size_t destinationSize, JsonVariantConst value,
                  const char* fallback) {
    if (value.isNull())
        return;

    const char* text = value | fallback;

    strlcpy(destination, text, destinationSize);
}

void sanitizeConfiguration(AppConfig& config) {
    config.deviceName[sizeof(config.deviceName) - 1] = '\0';
    config.homeAssistantHost[sizeof(config.homeAssistantHost) - 1] = '\0';
    config.homeAssistantToken[sizeof(config.homeAssistantToken) - 1] = '\0';
    config.timezone[sizeof(config.timezone) - 1] = '\0';
    config.printerEntityPrefix[sizeof(config.printerEntityPrefix) - 1] = '\0';
    config.weatherEntity[sizeof(config.weatherEntity) - 1] = '\0';

    if (config.deviceName[0] == '\0')
        strlcpy(config.deviceName, "cc2-monitor", sizeof(config.deviceName));

    if (config.timezone[0] == '\0') {
        strlcpy(config.timezone, "UTC0", sizeof(config.timezone));
    }

    if (config.weatherRefreshIntervalMs < 60000UL || config.weatherRefreshIntervalMs > 86400000UL) {
        config.weatherRefreshIntervalMs = 15UL * 60UL * 1000UL;
    }

    if (config.etaRemainingSwitchIntervalMs < 1000UL ||
        config.etaRemainingSwitchIntervalMs > 60000UL) {
        config.etaRemainingSwitchIntervalMs = 5000UL;
    }

    if (!isSupportedWifiTxPower(config.wifiTxPowerQuarterDbm))
        config.wifiTxPowerQuarterDbm = WIFI_TX_POWER_DEFAULT;

    if (config.touchRawXMin < 0 || config.touchRawXMax > 4095 ||
        config.touchRawYMin < 0 || config.touchRawYMax > 4095 ||
        config.touchRawXMax - config.touchRawXMin < 500 ||
        config.touchRawYMax - config.touchRawYMin < 500) {
        config.touchCalibrationValid = false;
    }
}
} // namespace

bool initializeConfigStore() {
    storeAvailable = LittleFS.begin(true);

    setConfigDefaults(appConfig);

    if (!storeAvailable)
        return false;

    loadConfiguration();

    return true;
}

bool loadConfiguration() {
    if (!storeAvailable)
        return false;

    if (!LittleFS.exists(CONFIG_PATH)) {
        setConfigDefaults(appConfig);
        return saveConfiguration(appConfig);
    }

    File file = LittleFS.open(CONFIG_PATH, "r");

    if (!file) {
        setConfigDefaults(appConfig);
        return false;
    }

    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, file);

    file.close();

    uint16_t storedVersion = doc["configVersion"] | 0;

    if (error || storedVersion < 1 || storedVersion > CURRENT_CONFIG_VERSION) {
        setConfigDefaults(appConfig);
        return false;
    }

    AppConfig loaded;
    setConfigDefaults(loaded);

    loaded.configVersion = CURRENT_CONFIG_VERSION;

    copyJsonText(loaded.deviceName, sizeof(loaded.deviceName), doc["deviceName"],
                 loaded.deviceName);

    copyJsonText(loaded.homeAssistantHost, sizeof(loaded.homeAssistantHost),
                 doc["homeAssistantHost"], loaded.homeAssistantHost);

    loaded.homeAssistantPort = doc["homeAssistantPort"] | loaded.homeAssistantPort;

    copyJsonText(loaded.homeAssistantToken, sizeof(loaded.homeAssistantToken),
                 doc["homeAssistantToken"], loaded.homeAssistantToken);

    copyJsonText(loaded.timezone, sizeof(loaded.timezone), doc["timezone"], loaded.timezone);

    copyJsonText(loaded.printerEntityPrefix, sizeof(loaded.printerEntityPrefix),
                 doc["printerEntityPrefix"], loaded.printerEntityPrefix);

    copyJsonText(loaded.weatherEntity, sizeof(loaded.weatherEntity), doc["weatherEntity"],
                 loaded.weatherEntity);

    loaded.weatherRefreshIntervalMs =
        doc["weatherRefreshIntervalMs"] | loaded.weatherRefreshIntervalMs;

    loaded.etaRemainingSwitchIntervalMs =
        doc["etaRemainingSwitchIntervalMs"] | loaded.etaRemainingSwitchIntervalMs;

    loaded.wifiTxPowerQuarterDbm =
        doc["wifiTxPowerQuarterDbm"] | loaded.wifiTxPowerQuarterDbm;

    loaded.stateTraceEnabled = doc["stateTraceEnabled"] | loaded.stateTraceEnabled;

    loaded.touchCalibrationValid =
        doc["touchCalibrationValid"] | loaded.touchCalibrationValid;
    loaded.touchRawXMin = doc["touchRawXMin"] | loaded.touchRawXMin;
    loaded.touchRawXMax = doc["touchRawXMax"] | loaded.touchRawXMax;
    loaded.touchRawYMin = doc["touchRawYMin"] | loaded.touchRawYMin;
    loaded.touchRawYMax = doc["touchRawYMax"] | loaded.touchRawYMax;
    loaded.touchSwapAxes = doc["touchSwapAxes"] | loaded.touchSwapAxes;
    loaded.touchInvertX = doc["touchInvertX"] | loaded.touchInvertX;
    loaded.touchInvertY = doc["touchInvertY"] | loaded.touchInvertY;

    sanitizeConfiguration(loaded);

    appConfig = loaded;

    if (storedVersion != CURRENT_CONFIG_VERSION)
        saveConfiguration(appConfig);

    return true;
}

bool saveConfiguration(const AppConfig& config) {
    if (!storeAvailable)
        return false;

    AppConfig saved = config;

    saved.configVersion = CURRENT_CONFIG_VERSION;

    sanitizeConfiguration(saved);

    StaticJsonDocument<2048> doc;

    doc["configVersion"] = saved.configVersion;
    doc["deviceName"] = saved.deviceName;
    doc["homeAssistantHost"] = saved.homeAssistantHost;
    doc["homeAssistantPort"] = saved.homeAssistantPort;
    doc["homeAssistantToken"] = saved.homeAssistantToken;
    doc["timezone"] = saved.timezone;
    doc["printerEntityPrefix"] = saved.printerEntityPrefix;
    doc["weatherEntity"] = saved.weatherEntity;
    doc["weatherRefreshIntervalMs"] = saved.weatherRefreshIntervalMs;
    doc["etaRemainingSwitchIntervalMs"] = saved.etaRemainingSwitchIntervalMs;
    doc["wifiTxPowerQuarterDbm"] = saved.wifiTxPowerQuarterDbm;
    doc["stateTraceEnabled"] = saved.stateTraceEnabled;
    doc["touchCalibrationValid"] = saved.touchCalibrationValid;
    doc["touchRawXMin"] = saved.touchRawXMin;
    doc["touchRawXMax"] = saved.touchRawXMax;
    doc["touchRawYMin"] = saved.touchRawYMin;
    doc["touchRawYMax"] = saved.touchRawYMax;
    doc["touchSwapAxes"] = saved.touchSwapAxes;
    doc["touchInvertX"] = saved.touchInvertX;
    doc["touchInvertY"] = saved.touchInvertY;

    File file = LittleFS.open(CONFIG_TEMP_PATH, "w");

    if (!file)
        return false;

    bool written = serializeJson(doc, file) > 0;

    file.flush();
    file.close();

    if (!written) {
        LittleFS.remove(CONFIG_TEMP_PATH);
        return false;
    }

    LittleFS.remove(CONFIG_PATH);

    if (!LittleFS.rename(CONFIG_TEMP_PATH, CONFIG_PATH)) {
        LittleFS.remove(CONFIG_TEMP_PATH);
        return false;
    }

    return true;
}

bool clearConfiguration() {
    setConfigDefaults(appConfig);

    if (!storeAvailable)
        return false;

    LittleFS.remove(CONFIG_TEMP_PATH);

    if (!LittleFS.exists(CONFIG_PATH))
        return true;

    return LittleFS.remove(CONFIG_PATH);
}

bool configStoreAvailable() {
    return storeAvailable;
}

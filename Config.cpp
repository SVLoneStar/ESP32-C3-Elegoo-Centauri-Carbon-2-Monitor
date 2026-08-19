#include "Config.h"

AppConfig appConfig;

namespace
{
void copyConfigText(
  char* destination,
  size_t destinationSize,
  const char* source
)
{
  if (destinationSize == 0)
    return;

  strlcpy(
    destination,
    source == nullptr ? "" : source,
    destinationSize
  );
}
}

void setConfigDefaults(
  AppConfig& config
)
{
  memset(&config, 0, sizeof(config));

  config.configVersion =
    CURRENT_CONFIG_VERSION;

  copyConfigText(
    config.deviceName,
    sizeof(config.deviceName),
    "cc2-monitor"
  );

  copyConfigText(
    config.homeAssistantHost,
    sizeof(config.homeAssistantHost),
    "192.0.2.1"
  );

  config.homeAssistantPort =
    8123;

  copyConfigText(
    config.homeAssistantToken,
    sizeof(config.homeAssistantToken),
    ""
  );

  copyConfigText(
    config.timezone,
    sizeof(config.timezone),
    "CET-1CEST,M3.5.0,M10.5.0/3"
  );

  copyConfigText(
    config.weatherEntity,
    sizeof(config.weatherEntity),
    "weather.regensburg"
  );

  config.weatherRefreshIntervalMs =
    15UL * 60UL * 1000UL;

  config.etaRemainingSwitchIntervalMs =
    5000UL;
}

bool hasValidHomeAssistantConfig(
  const AppConfig& config
)
{
  return
    config.homeAssistantHost[0] != '\0' &&
    config.homeAssistantPort != 0 &&
    config.homeAssistantToken[0] != '\0';
}

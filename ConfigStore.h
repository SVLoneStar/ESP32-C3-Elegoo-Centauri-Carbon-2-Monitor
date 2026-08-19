#pragma once

#include "Config.h"

bool initializeConfigStore();
bool loadConfiguration();
bool saveConfiguration(const AppConfig& config);
bool clearConfiguration();
bool configStoreAvailable();

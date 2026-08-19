#pragma once

#include "AppState.h"

void initializeWeather();
void maintainWeather();
void markWeatherDirty();
bool weatherNeedsRedraw();
void drawWeatherFields();
String getWeatherStatus();

#include "Weather.h"
#include "Config.h"
#include "Diagnostics.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
const uint16_t WEATHER_HTTP_TIMEOUT = 2000;

struct ForecastDay {
    bool valid;
    char weekday[10];
    char condition[20];
    float highTemperature;
    float lowTemperature;
};

struct WeatherData {
    bool currentValid;
    char currentCondition[20];
    float currentTemperature;
    ForecastDay forecast[2];
    uint32_t revision;
};

WeatherData weatherData;
portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool weatherRequestRunning = false;

unsigned long lastWeatherRequest = 0;

uint32_t lastDrawnWeatherRevision = UINT32_MAX;

void copyText(char* destination, size_t destinationSize, const char* source) {
    if (source == nullptr || destinationSize == 0) {
        if (destinationSize > 0)
            destination[0] = '\0';

        return;
    }

    strlcpy(destination, source, destinationSize);
}

String weatherRestURL() {
    String url = "http://";

    url += appConfig.homeAssistantHost;
    url += ":";
    url += String(appConfig.homeAssistantPort);
    url += "/api/states/";
    url += appConfig.weatherEntity;

    return url;
}

String weatherForecastURL() {
    String url = "http://";

    url += appConfig.homeAssistantHost;
    url += ":";
    url += String(appConfig.homeAssistantPort);
    url += "/api/services/weather/get_forecasts?return_response";

    return url;
}

void configureWeatherHTTP(HTTPClient& http) {
    http.setConnectTimeout(WEATHER_HTTP_TIMEOUT);

    http.setTimeout(WEATHER_HTTP_TIMEOUT);

    http.addHeader("Authorization", String("Bearer ") + appConfig.homeAssistantToken);
}

bool loadCurrentWeather(WeatherData& result) {
    HTTPClient http;

    if (!http.begin(weatherRestURL()))
        return false;

    configureWeatherHTTP(http);

    unsigned long callStarted = millis();
    int code = http.GET();
    recordBlockingCall("weatherCurrent.GET", millis() - callStarted);

    if (code != 200) {
        http.end();
        return false;
    }

    StaticJsonDocument<96> filter;
    filter["state"] = true;
    filter["attributes"]["temperature"] = true;

    StaticJsonDocument<256> doc;

    DeserializationError error =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    http.end();

    if (error)
        return false;

    const char* condition = doc["state"] | "";

    if (condition[0] == '\0' || strcmp(condition, "unknown") == 0 ||
        strcmp(condition, "unavailable") == 0 || doc["attributes"]["temperature"].isNull()) {
        return false;
    }

    result.currentValid = true;

    copyText(result.currentCondition, sizeof(result.currentCondition), condition);

    result.currentTemperature = doc["attributes"]["temperature"].as<float>();

    return true;
}

bool dateKey(int daysFromToday, char* result, size_t resultSize) {
    time_t now = time(nullptr);

    if (now < 1000000000UL)
        return false;

    struct tm localTime;

    if (!localtime_r(&now, &localTime))
        return false;

    localTime.tm_hour = 12;
    localTime.tm_min = 0;
    localTime.tm_sec = 0;
    localTime.tm_mday += daysFromToday;
    localTime.tm_isdst = -1;

    if (mktime(&localTime) == (time_t)-1)
        return false;

    snprintf(result, resultSize, "%04d-%02d-%02d", localTime.tm_year + 1900, localTime.tm_mon + 1,
             localTime.tm_mday);

    return true;
}

void weekdayFromDate(const char* datetime, char* result, size_t resultSize) {
    int year;
    int month;
    int day;

    if (datetime == nullptr || sscanf(datetime, "%d-%d-%d", &year, &month, &day) != 3) {
        copyText(result, resultSize, "--");
        return;
    }

    struct tm forecastTime = {};
    forecastTime.tm_year = year - 1900;
    forecastTime.tm_mon = month - 1;
    forecastTime.tm_mday = day;
    forecastTime.tm_hour = 12;
    forecastTime.tm_isdst = -1;

    if (mktime(&forecastTime) == (time_t)-1) {
        copyText(result, resultSize, "--");
        return;
    }

    const char* weekdays[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                              "Thursday", "Friday", "Saturday"};

    copyText(result, resultSize, weekdays[forecastTime.tm_wday]);
}

bool readForecastDay(JsonObject forecast, ForecastDay& result) {
    const char* datetime = forecast["datetime"] | "";

    const char* condition = forecast["condition"] | "";

    if (datetime[0] == '\0' || condition[0] == '\0' || forecast["temperature"].isNull() ||
        forecast["templow"].isNull()) {
        return false;
    }

    result.valid = true;

    weekdayFromDate(datetime, result.weekday, sizeof(result.weekday));

    copyText(result.condition, sizeof(result.condition), condition);

    result.highTemperature = forecast["temperature"].as<float>();

    result.lowTemperature = forecast["templow"].as<float>();

    return true;
}

bool loadWeatherForecast(WeatherData& result) {
    HTTPClient http;

    if (!http.begin(weatherForecastURL()))
        return false;

    configureWeatherHTTP(http);

    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<192> request;
    request["type"] = "daily";
    request["entity_id"] = appConfig.weatherEntity;

    uint8_t payload[192];
    size_t payloadLength = serializeJson(request, payload, sizeof(payload));

    if (payloadLength == 0) {
        http.end();
        return false;
    }

    unsigned long callStarted = millis();
    int code = http.POST(payload, payloadLength);
    recordBlockingCall("weatherForecast.POST", millis() - callStarted);

    if (code != 200) {
        http.end();
        return false;
    }

    StaticJsonDocument<256> filter;
    JsonObject forecastFilter =
        filter["service_response"][appConfig.weatherEntity]["forecast"][0].to<JsonObject>();

    forecastFilter["datetime"] = true;
    forecastFilter["condition"] = true;
    forecastFilter["temperature"] = true;
    forecastFilter["templow"] = true;

    StaticJsonDocument<3072> doc;

    DeserializationError error =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    http.end();

    if (error)
        return false;

    JsonArray forecasts =
        doc["service_response"][appConfig.weatherEntity]["forecast"].as<JsonArray>();

    if (forecasts.isNull())
        return false;

    char targetDates[2][11];
    bool haveTargetDates = dateKey(1, targetDates[0], sizeof(targetDates[0])) &&
                           dateKey(2, targetDates[1], sizeof(targetDates[1]));

    bool found[2] = {false, false};

    ForecastDay loadedForecast[2] = {};

    if (haveTargetDates) {
        for (JsonObject forecast : forecasts) {
            const char* datetime = forecast["datetime"] | "";

            for (int i = 0; i < 2; i++) {
                if (!found[i] && strncmp(datetime, targetDates[i], 10) == 0) {
                    found[i] = readForecastDay(forecast, loadedForecast[i]);
                }
            }
        }
    } else {
        int index = 0;

        for (JsonObject forecast : forecasts) {
            if (index >= 2)
                break;

            found[index] = readForecastDay(forecast, loadedForecast[index]);

            index++;
        }
    }

    if (!found[0] || !found[1])
        return false;

    result.forecast[0] = loadedForecast[0];
    result.forecast[1] = loadedForecast[1];

    return true;
}

void weatherRequestTask(void* parameter) {
    WeatherData updated;

    portENTER_CRITICAL(&weatherMux);
    updated = weatherData;
    portEXIT_CRITICAL(&weatherMux);

    unsigned long callStarted = millis();
    loadCurrentWeather(updated);
    recordBlockingCall("loadCurrentWeather", millis() - callStarted);

    callStarted = millis();
    loadWeatherForecast(updated);
    recordBlockingCall("loadWeatherForecast", millis() - callStarted);

    portENTER_CRITICAL(&weatherMux);
    updated.revision = weatherData.revision + 1;
    weatherData = updated;
    weatherRequestRunning = false;
    portEXIT_CRITICAL(&weatherMux);

    vTaskDelete(nullptr);
}

bool conditionIs(const char* condition, const char* expected) {
    return strcmp(condition, expected) == 0;
}

void drawSun(int x, int y, uint16_t color) {
    tft.fillCircle(x, y, 7, color);

    for (int i = 0; i < 8; i++) {
        float angle = i * PI / 4.0f;

        int x1 = x + cos(angle) * 10;
        int y1 = y + sin(angle) * 10;
        int x2 = x + cos(angle) * 13;
        int y2 = y + sin(angle) * 13;

        tft.drawLine(x1, y1, x2, y2, color);
    }
}

void drawCloud(int x, int y, uint16_t color) {
    tft.fillCircle(x - 7, y, 6, color);
    tft.fillCircle(x, y - 4, 8, color);
    tft.fillCircle(x + 8, y, 6, color);
    tft.fillRoundRect(x - 13, y, 27, 8, 3, color);
}

void drawRain(int x, int y, uint16_t color, bool heavy) {
    drawCloud(x, y - 5, C_GREY);

    int count = heavy ? 4 : 3;

    for (int i = 0; i < count; i++) {
        int dropX = x - 10 + i * 7;
        tft.drawLine(dropX, y + 5, dropX - 2, y + 10, color);
    }
}

void drawSnow(int x, int y) {
    drawCloud(x, y - 5, C_GREY);

    for (int i = -1; i <= 1; i++) {
        int snowX = x + i * 9;
        tft.drawFastHLine(snowX - 2, y + 8, 5, C_TEXT);
        tft.drawFastVLine(snowX, y + 6, 5, C_TEXT);
    }
}

void drawWind(int x, int y) {
    tft.drawFastHLine(x - 13, y - 6, 22, C_CYAN);
    tft.drawCircle(x + 9, y - 3, 3, C_CYAN);
    tft.drawFastHLine(x - 10, y + 2, 25, C_CYAN);
    tft.drawCircle(x + 15, y + 5, 3, C_CYAN);
    tft.drawFastHLine(x - 13, y + 10, 18, C_CYAN);
}

void drawWeatherIcon(int x, int y, const char* condition) {
    if (conditionIs(condition, "sunny")) {
        drawSun(x, y, C_ORANGE);
    } else if (conditionIs(condition, "clear-night")) {
        tft.fillCircle(x, y, 11, C_GREY);
        tft.fillCircle(x + 5, y - 4, 10, C_BG);
    } else if (conditionIs(condition, "partlycloudy")) {
        drawSun(x - 7, y - 5, C_ORANGE);
        drawCloud(x + 3, y + 3, C_GREY);
    } else if (conditionIs(condition, "cloudy")) {
        drawCloud(x, y, C_GREY);
    } else if (conditionIs(condition, "rainy") || conditionIs(condition, "pouring")) {
        drawRain(x, y, C_CYAN, conditionIs(condition, "pouring"));
    } else if (conditionIs(condition, "lightning") || conditionIs(condition, "lightning-rainy")) {
        drawCloud(x, y - 5, C_GREY);
        tft.fillTriangle(x + 1, y + 2, x - 5, y + 11, x + 1, y + 9, C_ORANGE);
        tft.fillTriangle(x + 1, y + 8, x - 1, y + 15, x + 7, y + 6, C_ORANGE);
    } else if (conditionIs(condition, "snowy") || conditionIs(condition, "snowy-rainy")) {
        drawSnow(x, y);
    } else if (conditionIs(condition, "fog")) {
        for (int i = -1; i <= 1; i++)
            tft.drawFastHLine(x - 13, y + i * 7, 26, C_GREY);
    } else if (conditionIs(condition, "hail")) {
        drawCloud(x, y - 5, C_GREY);
        tft.fillCircle(x - 8, y + 8, 2, C_CYAN);
        tft.fillCircle(x, y + 10, 2, C_CYAN);
        tft.fillCircle(x + 8, y + 8, 2, C_CYAN);
    } else if (conditionIs(condition, "windy") || conditionIs(condition, "windy-variant")) {
        drawWind(x, y);
    } else if (conditionIs(condition, "exceptional")) {
        tft.drawCircle(x, y, 12, C_RED);
        tft.drawFastVLine(x, y - 7, 10, C_RED);
        tft.fillCircle(x, y + 7, 1, C_RED);
    } else {
        tft.drawCircle(x, y, 12, C_RED);
        tft.drawFastVLine(x, y - 7, 10, C_RED);
        tft.fillCircle(x, y + 7, 1, C_RED);
    }
}

void drawColumnHeading(const char* heading, int centerX) {
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(C_CYAN);

    int16_t x1;
    int16_t y1;
    uint16_t width;
    uint16_t height;

    tft.getTextBounds(heading, 0, 94, &x1, &y1, &width, &height);

    tft.setCursor(centerX - width / 2, 94);
    tft.print(heading);
}

void drawTemperatureText(const String& text, int centerX) {
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(C_TEXT);

    int16_t x1;
    int16_t y1;
    uint16_t width;
    uint16_t height;

    tft.getTextBounds(text, 0, 153, &x1, &y1, &width, &height);

    tft.setCursor(centerX - width / 2, 153);
    tft.print(text);
}
} // namespace

void initializeWeather() {
    memset(&weatherData, 0, sizeof(weatherData));

    weatherData.currentTemperature = NAN;
    weatherData.forecast[0].highTemperature = NAN;
    weatherData.forecast[0].lowTemperature = NAN;
    weatherData.forecast[1].highTemperature = NAN;
    weatherData.forecast[1].lowTemperature = NAN;

    lastWeatherRequest = millis() - appConfig.weatherRefreshIntervalMs;

    idleWeatherDirty = true;
}

void maintainWeather() {
    if (weatherRequestRunning || WiFi.status() != WL_CONNECTED ||
        !hasValidHomeAssistantConfig(appConfig) || appConfig.weatherEntity[0] == '\0' ||
        millis() - lastWeatherRequest < appConfig.weatherRefreshIntervalMs) {
        return;
    }

    lastWeatherRequest = millis();

    weatherRequestRunning = true;

    BaseType_t created = xTaskCreate(weatherRequestTask, "weather-http", 8192, nullptr, 1, nullptr);

    if (created != pdPASS) {
        weatherRequestRunning = false;

        portENTER_CRITICAL(&weatherMux);
        weatherData.revision++;
        portEXIT_CRITICAL(&weatherMux);
    }
}

void markWeatherDirty() {
    idleWeatherDirty = true;
}

bool weatherNeedsRedraw() {
    uint32_t revision;

    portENTER_CRITICAL(&weatherMux);
    revision = weatherData.revision;
    portEXIT_CRITICAL(&weatherMux);

    return idleWeatherDirty || revision != lastDrawnWeatherRevision;
}

void drawWeatherFields() {
    WeatherData shown;

    portENTER_CRITICAL(&weatherMux);
    shown = weatherData;
    portEXIT_CRITICAL(&weatherMux);

    tft.fillRect(0, 80, 320, 78, C_BG);

    const int centers[] = {53, 160, 267};

    drawColumnHeading("NOW", centers[0]);
    drawColumnHeading(shown.forecast[0].valid ? shown.forecast[0].weekday : "--", centers[1]);
    drawColumnHeading(shown.forecast[1].valid ? shown.forecast[1].weekday : "--", centers[2]);

    if (shown.currentValid) {
        drawWeatherIcon(centers[0], 119, shown.currentCondition);

        String currentTemperature = String((int)round(shown.currentTemperature));

        currentTemperature += (char)247;

        currentTemperature += "C";

        drawTemperatureText(currentTemperature, centers[0]);
    } else {
        drawTemperatureText("--", centers[0]);
    }

    for (int i = 0; i < 2; i++) {
        if (!shown.forecast[i].valid) {
            drawTemperatureText("-- / --", centers[i + 1]);
            continue;
        }

        drawWeatherIcon(centers[i + 1], 119, shown.forecast[i].condition);

        String temperatures = String((int)round(shown.forecast[i].highTemperature));

        temperatures += "/";
        temperatures += String((int)round(shown.forecast[i].lowTemperature));

        temperatures += (char)247;
        temperatures += "C";

        drawTemperatureText(temperatures, centers[i + 1]);
    }

    lastDrawnWeatherRevision = shown.revision;

    idleWeatherDirty = false;
}

String getWeatherStatus() {
    if (!hasValidHomeAssistantConfig(appConfig) || appConfig.weatherEntity[0] == '\0') {
        return "DISABLED - CONFIGURATION INCOMPLETE";
    }

    if (weatherRequestRunning)
        return "REFRESHING";

    WeatherData status;

    portENTER_CRITICAL(&weatherMux);
    status = weatherData;
    portEXIT_CRITICAL(&weatherMux);

    bool forecastValid = status.forecast[0].valid && status.forecast[1].valid;

    if (status.currentValid && forecastValid) {
        return "CURRENT AND FORECAST AVAILABLE";
    }

    if (status.currentValid || forecastValid) {
        return "PARTIAL DATA";
    }

    return "WAITING FOR WEATHER DATA";
}

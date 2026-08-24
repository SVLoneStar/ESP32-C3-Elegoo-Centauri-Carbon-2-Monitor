#include "WebUI.h"

#include "AppState.h"
#include "Config.h"
#include "ConfigStore.h"
#include "Diagnostics.h"
#include "HomeAssistant.h"
#include "PrinterData.h"
#include "StateTrace.h"
#include "Version.h"
#include "Weather.h"
#include "BootStage.h"
#include "TouchInput.h"

#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
WiFiServer server(80);

bool serverStarted = false;

void sendHeader(WiFiClient& client, int statusCode, const char* statusText,
                const char* contentType = "text/html") {
    client.print(F("HTTP/1.1 "));
    client.print(statusCode);
    client.print(' ');
    client.println(statusText);
    client.print(F("Content-Type: "));
    client.print(contentType);
    client.println(F("; charset=utf-8"));
    client.println(F("Connection: close"));
    client.println();
}

void printEscaped(WiFiClient& client, const String& value) {
    for (size_t i = 0; i < value.length(); i++) {
        switch (value.charAt(i)) {
        case '&':
            client.print(F("&amp;"));
            break;
        case '<':
            client.print(F("&lt;"));
            break;
        case '>':
            client.print(F("&gt;"));
            break;
        case '"':
            client.print(F("&quot;"));
            break;
        case '\'':
            client.print(F("&#39;"));
            break;
        default:
            client.print(value.charAt(i));
            break;
        }
    }
}

void pageStart(WiFiClient& client, const char* title) {
    client.print(
        F("<!doctype html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<style>body{font-family:system-ui,sans-serif;max-width:760px;margin:auto;padding:18px;"
          "background:#111827;color:#e5e7eb}"
          "a{color:#67e8f9;margin-right:18px}section{background:#1f2937;padding:16px;margin:16px "
          "0;border-radius:8px}"
          "label{display:block;margin-top:12px}input,select{box-sizing:border-box;width:100%;padding:9px;"
          "margin-top:4px}"
          "button{padding:10px 16px;margin-top:14px}table{width:100%;border-collapse:collapse}"
          "td{padding:7px;border-bottom:1px solid #374151}td:first-child{color:#9ca3af;width:48%}"
          ".ok{color:#4ade80}.bad{color:#f87171}.note{color:#fbbf24}</style></head><body>"
          "<nav><a href='/'>Status</a><a href='/configuration'>Configuration</a><a "
          "href='/diagnostics'>Diagnostics</a><a href='/maintenance'>Maintenance</a></nav><h1>"));
    printEscaped(client, title);
    client.print(F("</h1>"));
}

void pageEnd(WiFiClient& client) {
    client.print(F("</body></html>"));
}

void statusRow(WiFiClient& client, const char* label, const String& value,
               const char* cssClass = "", const char* dataKey = "") {
    client.print(F("<tr><td>"));
    printEscaped(client, label);
    client.print(F("</td><td class='"));
    client.print(cssClass);
    client.print(F("'"));
    if (dataKey[0] != '\0') {
        client.print(F(" data-key='"));
        client.print(dataKey);
        client.print('\'');
    }
    client.print('>');
    printEscaped(client, value);
    client.print(F("</td></tr>"));
}

void liveStatusScript(WiFiClient& client) {
    client.print(F("<script>(()=>{async function u(){try{const r=await fetch('/api/status',{cache:"
                   "'no-store'});if(!r.ok)return;const d=await r.json();document.querySelectorAll("
                   "'[data-key]').forEach(e=>{const v=d[e.dataset.key];if(v!==undefined)e.textContent="
                   "v;});}catch(e){}}u();setInterval(u,2000);})();</script>"));
}

void printJsonString(WiFiClient& client, const String& value) {
    client.print('"');
    for (size_t i = 0; i < value.length(); i++) {
        char c = value.charAt(i);
        if (c == '"' || c == '\\') {
            client.print('\\');
            client.print(c);
        } else if (c == '\n') {
            client.print(F("\\n"));
        } else if (c == '\r') {
            client.print(F("\\r"));
        } else if ((uint8_t)c >= 0x20) {
            client.print(c);
        }
    }
    client.print('"');
}

void jsonField(WiFiClient& client, const char* name, const String& value, bool& first) {
    if (!first)
        client.print(',');
    first = false;
    printJsonString(client, name);
    client.print(':');
    printJsonString(client, value);
}

String wifiValue(bool connected, const String& value) {
    return connected ? value : "--";
}

void writeStatusJson(WiFiClient& client) {
    bool wifi = WiFi.status() == WL_CONNECTED;
    bool first = true;

    sendHeader(client, 200, "OK", "application/json");
    client.print('{');
    jsonField(client, "firmwareVersion", FIRMWARE_VERSION, first);
    jsonField(client, "gitRevision", FIRMWARE_GIT_REVISION, first);
    jsonField(client, "buildState", FIRMWARE_BUILD_STATE, first);
    jsonField(client, "buildDateTime", String(FIRMWARE_BUILD_DATE) + " " + FIRMWARE_BUILD_TIME,
              first);
    jsonField(client, "wifiState", wifi ? "CONNECTED" : "OFFLINE", first);
    jsonField(client, "wifiSsid", wifiValue(wifi, WiFi.SSID()), first);
    jsonField(client, "wifiIp", wifiValue(wifi, WiFi.localIP().toString()), first);
    jsonField(client, "wifiRssi", wifi ? String(WiFi.RSSI()) + " dBm" : "--", first);
    jsonField(client, "wifiChannel", wifi ? String(WiFi.channel()) : "--", first);
    jsonField(client, "wifiGateway", wifiValue(wifi, WiFi.gatewayIP().toString()), first);
    jsonField(client, "wifiDns", wifiValue(wifi, WiFi.dnsIP().toString()), first);
    jsonField(client, "wifiTxPowerConfigured", getConfiguredWifiTxPower(), first);
    jsonField(client, "wifiTxPowerEffective", getEffectiveWifiTxPower(), first);
    jsonField(client, "wifiTxPowerApplyStatus", getWifiTxPowerApplyStatus(), first);
    jsonField(client, "haHost", appConfig.homeAssistantHost, first);
    jsonField(client, "haPort", String(appConfig.homeAssistantPort), first);
    jsonField(client, "websocketState", wsConnected ? "CONNECTED" : "OFFLINE", first);
    jsonField(client, "authState", authenticated ? "AUTHENTICATED" : "NOT AUTHENTICATED", first);
    jsonField(client, "subscriptionState", subscribed ? "SUBSCRIBED" : "NOT SUBSCRIBED", first);
    jsonField(client, "connectAttempts", String(connectAttempts), first);
    jsonField(client, "successfulConnections", String(successfulConnects), first);
    jsonField(client, "disconnectCount", String(disconnectCount), first);
    jsonField(client, "earlyDisconnects", String(getConsecutiveEarlyDisconnectCount()), first);
    jsonField(client, "clientReinitializations", String(getWebSocketClientReinitializationCount()),
              first);
    jsonField(client, "printerConfig",
              hasValidPrinterEntityConfig(appConfig) ? "CONFIGURED" : "INCOMPLETE OR INVALID",
              first);
    jsonField(client, "printerPrefix",
              appConfig.printerEntityPrefix[0] != '\0' ? appConfig.printerEntityPrefix : "--", first);
    jsonField(client, "printerState", printerStateText(getPrinterState()), first);
    jsonField(client, "rawCurrentStatus",
              printer.currentStatus.length() > 0 ? printer.currentStatus : "--", first);
    jsonField(client, "rawPrintStatus",
              printer.printStatus.length() > 0 ? printer.printStatus : "--", first);
    jsonField(client, "printError", printer.printError.length() > 0 ? printer.printError : "--",
              first);
    jsonField(client, "errorReason", printer.errorReason.length() > 0 ? printer.errorReason : "--",
              first);
    jsonField(client, "lastTransition", getLastPrinterStateTransition(), first);
    jsonField(client, "messageCount", String(messageCount), first);
    jsonField(client, "triggerCount", String(triggerCount), first);
    jsonField(client, "weatherEntity",
              appConfig.weatherEntity[0] != '\0' ? appConfig.weatherEntity : "--", first);
    jsonField(client, "weatherEnabled", appConfig.weatherEntity[0] != '\0' ? "ENABLED" : "DISABLED",
              first);
    jsonField(client, "weatherStatus", getWeatherStatus(), first);
    jsonField(client, "traceState", appConfig.stateTraceEnabled ? "ENABLED" : "DISABLED", first);
    jsonField(client, "traceSize", String(getStateTraceSize()) + " bytes", first);
    jsonField(client, "bootCount", String(bootCount), first);
    jsonField(client, "uptime", getUptimeString(), first);
    jsonField(client, "resetReason", resetReasonWithCode(lastResetReason), first);
    jsonField(client, "bootStage", bootStageText(getCurrentBootStage()), first);
    jsonField(client, "bootStageCode", String((uint8_t)getCurrentBootStage()), first);
    jsonField(client, "bootStageDisplay",
              String(bootStageText(getCurrentBootStage())) + " (" +
                  String((uint8_t)getCurrentBootStage()) + "), uptime " +
                  String(getCurrentBootStageUptimeMs()) + " ms",
              first);
    jsonField(client, "previousBootStage", bootStageText(getPreviousBootStage()), first);
    jsonField(client, "previousBootStageCode", String((uint8_t)getPreviousBootStage()), first);
    jsonField(client, "previousBootStageUptimeMs", String(getPreviousBootStageUptimeMs()), first);
    jsonField(client, "bootStagePersistence",
              isBootStagePersistenceAvailable() ? "AVAILABLE" : "UNAVAILABLE", first);
    jsonField(client, "freeHeap", String(ESP.getFreeHeap()) + " bytes", first);
    jsonField(client, "minimumFreeHeap", String(ESP.getMinFreeHeap()) + " bytes", first);
    jsonField(client, "maximumLoopDuration", String(getMaximumLoopDuration()) + " ms", first);
    jsonField(client, "loopOver100Ms", String(getLoopOver100MsCount()), first);
    jsonField(client, "loopOver500Ms", String(getLoopOver500MsCount()), first);
    jsonField(client, "loopOver1Second", String(getLoopOver1SecondCount()), first);
    jsonField(client, "loopOver5Seconds", String(getLoopOver5SecondsCount()), first);
    jsonField(client, "touchDetected", touchDetected() ? "INITIALIZED" : "NOT INITIALIZED", first);
    jsonField(client, "touchCalibration", touchCalibrationStatus(), first);
    jsonField(client, "touchRawXRange",
              String(appConfig.touchRawXMin) + " - " + String(appConfig.touchRawXMax), first);
    jsonField(client, "touchRawYRange",
              String(appConfig.touchRawYMin) + " - " + String(appConfig.touchRawYMax), first);
    jsonField(client, "touchAxisOrientation",
              appConfig.touchSwapAxes ? "SWAPPED" : "NORMAL", first);
    jsonField(client, "touchInvertX", appConfig.touchInvertX ? "YES" : "NO", first);
    jsonField(client, "touchInvertY", appConfig.touchInvertY ? "YES" : "NO", first);
    client.print('}');
}

void showStatus(WiFiClient& client) {
    bool wifi = WiFi.status() == WL_CONNECTED;

    sendHeader(client, 200, "OK");
    pageStart(client, "Status");
    client.print(F("<section><table>"));

    statusRow(client, "Firmware", FIRMWARE_IDENTIFIER);
    statusRow(client, "Build state", FIRMWARE_BUILD_STATE, "", "buildState");
    statusRow(client, "Build date/time", String(FIRMWARE_BUILD_DATE) + " " + FIRMWARE_BUILD_TIME);
    statusRow(client, "WiFi connection", wifi ? "CONNECTED" : "OFFLINE", wifi ? "ok" : "bad",
              "wifiState");
    statusRow(client, "ESP IP", wifi ? WiFi.localIP().toString() : "--", "", "wifiIp");
    statusRow(client, "HA WebSocket", wsConnected ? "CONNECTED" : "OFFLINE",
              wsConnected ? "ok" : "bad", "websocketState");
    statusRow(client, "HA authentication", authenticated ? "AUTHENTICATED" : "NOT AUTHENTICATED",
              authenticated ? "ok" : "bad", "authState");
    statusRow(client, "HA subscription", subscribed ? "SUBSCRIBED" : "NOT SUBSCRIBED",
              subscribed ? "ok" : "bad", "subscriptionState");
    statusRow(client, "Printer entity configuration",
              hasValidPrinterEntityConfig(appConfig) ? "CONFIGURED" : "INCOMPLETE OR INVALID",
              hasValidPrinterEntityConfig(appConfig) ? "ok" : "bad");
    statusRow(client, "Printer entity prefix",
              appConfig.printerEntityPrefix[0] != '\0' ? appConfig.printerEntityPrefix : "--");
    statusRow(client, "Printer state", printerStateText(getPrinterState()), "", "printerState");
    statusRow(client, "HA current status (raw)",
              printer.currentStatus.length() > 0 ? printer.currentStatus : "--", "", "rawCurrentStatus");
    statusRow(client, "HA print status (raw)",
              printer.printStatus.length() > 0 ? printer.printStatus : "--", "", "rawPrintStatus");
    statusRow(client, "Weather", getWeatherStatus(), "", "weatherStatus");
    statusRow(client, "StateTrace logging", appConfig.stateTraceEnabled ? "ENABLED" : "DISABLED", "",
              "traceState");
    statusRow(client, "Boot count", String(bootCount), "", "bootCount");
    statusRow(client, "Uptime", getUptimeString(), "", "uptime");
    statusRow(client, "Reset reason", resetReasonWithCode(lastResetReason), "", "resetReason");
    statusRow(client, "Current boot stage",
              String(bootStageText(getCurrentBootStage())) + " (" +
                  String((uint8_t)getCurrentBootStage()) + "), uptime " +
                  String(getCurrentBootStageUptimeMs()) + " ms",
              "", "bootStageDisplay");
    statusRow(client, "Previous boot last stage",
              String(bootStageText(getPreviousBootStage())) + " (" +
                  String((uint8_t)getPreviousBootStage()) + "), uptime " +
                  String(getPreviousBootStageUptimeMs()) + " ms");
    statusRow(client, "Boot-stage persistence",
              isBootStagePersistenceAvailable() ? "AVAILABLE" : "UNAVAILABLE", "",
              "bootStagePersistence");
    statusRow(client, "Free heap", String(ESP.getFreeHeap()) + " bytes", "", "freeHeap");
    statusRow(client, "Minimum free heap", String(ESP.getMinFreeHeap()) + " bytes", "",
              "minimumFreeHeap");

    client.print(F("</table></section>"));
    liveStatusScript(client);
    pageEnd(client);
}

void diagnosticSection(WiFiClient& client, const char* title) {
    client.print(F("<section><h2>"));
    printEscaped(client, title);
    client.print(F("</h2><table>"));
}

void showDiagnostics(WiFiClient& client) {
    bool wifi = WiFi.status() == WL_CONNECTED;

    sendHeader(client, 200, "OK");
    pageStart(client, "Diagnostics");
    diagnosticSection(client, "Firmware / build");
    statusRow(client, "Firmware version", FIRMWARE_VERSION, "", "firmwareVersion");
    statusRow(client, "Git revision", FIRMWARE_GIT_REVISION, "", "gitRevision");
    statusRow(client, "Build state", FIRMWARE_BUILD_STATE, "", "buildState");
    statusRow(client, "Compiler build date/time", String(FIRMWARE_BUILD_DATE) + " " + FIRMWARE_BUILD_TIME,
              "", "buildDateTime");
    client.print(F("</table></section>"));
    diagnosticSection(client, "Device");
    statusRow(client, "Uptime", getUptimeString(), "", "uptime");
    statusRow(client, "Boot count", String(bootCount), "", "bootCount");
    statusRow(client, "Reset reason", resetReasonWithCode(lastResetReason), "", "resetReason");
    statusRow(client, "Current boot stage",
              String(bootStageText(getCurrentBootStage())) + " (" +
                  String((uint8_t)getCurrentBootStage()) + "), uptime " +
                  String(getCurrentBootStageUptimeMs()) + " ms",
              "", "bootStageDisplay");
    statusRow(client, "Previous boot last stage",
              String(bootStageText(getPreviousBootStage())) + " (" +
                  String((uint8_t)getPreviousBootStage()) + "), uptime " +
                  String(getPreviousBootStageUptimeMs()) + " ms");
    statusRow(client, "Boot-stage persistence",
              isBootStagePersistenceAvailable() ? "AVAILABLE" : "UNAVAILABLE", "",
              "bootStagePersistence");
    statusRow(client, "Free heap", String(ESP.getFreeHeap()) + " bytes", "", "freeHeap");
    statusRow(client, "Minimum free heap", String(ESP.getMinFreeHeap()) + " bytes", "",
              "minimumFreeHeap");
    statusRow(client, "Maximum loop duration", String(getMaximumLoopDuration()) + " ms", "",
              "maximumLoopDuration");
    statusRow(client, "Loops over 100 ms", String(getLoopOver100MsCount()), "", "loopOver100Ms");
    statusRow(client, "Loops over 500 ms", String(getLoopOver500MsCount()), "", "loopOver500Ms");
    statusRow(client, "Loops over 1 second", String(getLoopOver1SecondCount()), "",
              "loopOver1Second");
    statusRow(client, "Loops over 5 seconds", String(getLoopOver5SecondsCount()), "",
              "loopOver5Seconds");
    client.print(F("</table></section>"));
    diagnosticSection(client, "Touch");
    statusRow(client, "Touch detected", touchDetected() ? "INITIALIZED" : "NOT INITIALIZED", "",
              "touchDetected");
    statusRow(client, "Calibration", touchCalibrationStatus(), "", "touchCalibration");
    statusRow(client, "Raw X range",
              String(appConfig.touchRawXMin) + " - " + String(appConfig.touchRawXMax), "",
              "touchRawXRange");
    statusRow(client, "Raw Y range",
              String(appConfig.touchRawYMin) + " - " + String(appConfig.touchRawYMax), "",
              "touchRawYRange");
    statusRow(client, "Axis orientation", appConfig.touchSwapAxes ? "SWAPPED" : "NORMAL", "",
              "touchAxisOrientation");
    statusRow(client, "Invert X", appConfig.touchInvertX ? "YES" : "NO", "", "touchInvertX");
    statusRow(client, "Invert Y", appConfig.touchInvertY ? "YES" : "NO", "", "touchInvertY");
    client.print(F("</table><form method='post' action='/touch-calibration/start'><button "
                   "type='submit'>"));
    client.print(touchCalibrationValid() ? F("Recalibrate Touch") : F("Calibrate Touch"));
    client.print(F("</button></form><form method='post' action='/touch-calibration/cancel'>"
                   "<button type='submit'>Cancel touch calibration</button></form></section>"));
    diagnosticSection(client, "WiFi");
    statusRow(client, "Connection", wifi ? "CONNECTED" : "OFFLINE", wifi ? "ok" : "bad", "wifiState");
    statusRow(client, "SSID", wifiValue(wifi, WiFi.SSID()), "", "wifiSsid");
    statusRow(client, "IP address", wifiValue(wifi, WiFi.localIP().toString()), "", "wifiIp");
    statusRow(client, "RSSI", wifi ? String(WiFi.RSSI()) + " dBm" : "--", "", "wifiRssi");
    statusRow(client, "Channel", wifi ? String(WiFi.channel()) : "--", "", "wifiChannel");
    statusRow(client, "Gateway", wifiValue(wifi, WiFi.gatewayIP().toString()), "", "wifiGateway");
    statusRow(client, "DNS server", wifiValue(wifi, WiFi.dnsIP().toString()), "", "wifiDns");
    statusRow(client, "Configured TX power", getConfiguredWifiTxPower(), "",
              "wifiTxPowerConfigured");
    statusRow(client, "Effective TX power", getEffectiveWifiTxPower(), "",
              "wifiTxPowerEffective");
    statusRow(client, "TX power application", getWifiTxPowerApplyStatus(), "",
              "wifiTxPowerApplyStatus");
    client.print(F("</table></section>"));
    diagnosticSection(client, "Home Assistant");
    statusRow(client, "Configured host / IP", appConfig.homeAssistantHost, "", "haHost");
    statusRow(client, "Configured port", String(appConfig.homeAssistantPort), "", "haPort");
    statusRow(client, "WebSocket", wsConnected ? "CONNECTED" : "OFFLINE", "", "websocketState");
    statusRow(client, "Authentication", authenticated ? "AUTHENTICATED" : "NOT AUTHENTICATED", "",
              "authState");
    statusRow(client, "Subscription", subscribed ? "SUBSCRIBED" : "NOT SUBSCRIBED", "",
              "subscriptionState");
    statusRow(client, "Connect attempts", String(connectAttempts), "", "connectAttempts");
    statusRow(client, "Successful connections", String(successfulConnects), "", "successfulConnections");
    statusRow(client, "Disconnect count", String(disconnectCount), "", "disconnectCount");
    statusRow(client, "Consecutive early disconnects", String(getConsecutiveEarlyDisconnectCount()),
              "", "earlyDisconnects");
    statusRow(client, "Client reinitializations", String(getWebSocketClientReinitializationCount()),
              "", "clientReinitializations");
    client.print(F("</table></section>"));
    diagnosticSection(client, "Printer");
    statusRow(client, "Resolved state", printerStateText(getPrinterState()), "", "printerState");
    statusRow(client, "Raw current status", printer.currentStatus.length() ? printer.currentStatus : "--",
              "", "rawCurrentStatus");
    statusRow(client, "Raw print status", printer.printStatus.length() ? printer.printStatus : "--", "",
              "rawPrintStatus");
    statusRow(client, "Print error", printer.printError.length() ? printer.printError : "--", "",
              "printError");
    statusRow(client, "Error reason", printer.errorReason.length() ? printer.errorReason : "--", "",
              "errorReason");
    statusRow(client, "Entity prefix", appConfig.printerEntityPrefix, "", "printerPrefix");
    statusRow(client, "Last state transition", getLastPrinterStateTransition(), "", "lastTransition");
    client.print(F("</table></section>"));
    diagnosticSection(client, "Events");
    statusRow(client, "WebSocket messages", String(messageCount), "", "messageCount");
    statusRow(client, "Printer triggers", String(triggerCount), "", "triggerCount");
    client.print(F("</table></section>"));
    diagnosticSection(client, "Weather");
    statusRow(client, "Configured entity", appConfig.weatherEntity, "", "weatherEntity");
    statusRow(client, "Weather", appConfig.weatherEntity[0] ? "ENABLED" : "DISABLED", "",
              "weatherEnabled");
    statusRow(client, "Retrieval status", getWeatherStatus(), "", "weatherStatus");
    client.print(F("</table></section>"));
    diagnosticSection(client, "Temporary StateTrace diagnostics");
    statusRow(client, "Logging", appConfig.stateTraceEnabled ? "ENABLED" : "DISABLED", "",
              "traceState");
    statusRow(client, "Current file size", String(getStateTraceSize()) + " bytes", "", "traceSize");
    client.print(F("</table><p><a href='/state-trace/download'>Download state trace</a></p>"
                   "<p><a href='/state-trace/clear-confirm'>Clear state trace</a></p></section>"));
    liveStatusScript(client);
    pageEnd(client);
}

void input(WiFiClient& client, const char* label, const char* name, const String& value,
           const char* type = "text") {
    client.print(F("<label>"));
    printEscaped(client, label);
    client.print(F("<input name='"));
    client.print(name);
    client.print(F("' type='"));
    client.print(type);
    client.print(F("' value='"));
    printEscaped(client, value);
    client.print(F("'></label>"));
}

void stateTraceSelect(WiFiClient& client) {
    client.print(F("<label>StateTrace logging<select name='stateTraceEnabled'>"
                   "<option value='1'"));

    if (appConfig.stateTraceEnabled)
        client.print(F(" selected"));

    client.print(F(">Enabled</option><option value='0'"));

    if (!appConfig.stateTraceEnabled)
        client.print(F(" selected"));

    client.print(F(">Disabled</option></select></label>"));
}

void wifiTxPowerSelect(WiFiClient& client) {
    client.print(F("<label>WiFi transmit-power limit<select name='wifiTxPower'>"
                   "<option value='default'"));
    if (appConfig.wifiTxPowerQuarterDbm == WIFI_TX_POWER_DEFAULT)
        client.print(F(" selected"));
    client.print(F(">Default (core-managed)</option>"));

    for (size_t i = 0; i < WIFI_TX_POWER_OPTION_COUNT; i++) {
        client.print(F("<option value='"));
        client.print(WIFI_TX_POWER_OPTIONS[i].value);
        client.print('\'');
        if (appConfig.wifiTxPowerQuarterDbm == WIFI_TX_POWER_OPTIONS[i].value)
            client.print(F(" selected"));
        client.print('>');
        printEscaped(client, WIFI_TX_POWER_OPTIONS[i].label);
        client.print(F("</option>"));
    }
    client.print(F("</select></label>"));
}

void showConfiguration(WiFiClient& client) {
    sendHeader(client, 200, "OK");
    pageStart(client, "Configuration");
    client.print(F("<section><form method='post' action='/configuration'>"));

    client.print(F("<h2>WiFi / network</h2>"));
    input(client, "Device / mDNS name", "deviceName", appConfig.deviceName);
    wifiTxPowerSelect(client);
    client.print(F("<h2>Home Assistant and display</h2>"));
    input(client, "Home Assistant host or IP", "haHost", appConfig.homeAssistantHost);
    input(client, "Home Assistant port", "haPort", String(appConfig.homeAssistantPort), "number");
    input(client, "Home Assistant long-lived access token (leave empty to preserve)", "haToken", "",
          "password");
    input(client, "Timezone POSIX string", "timezone", appConfig.timezone);
    input(client, "Printer entity prefix", "printerPrefix", appConfig.printerEntityPrefix);
    input(client, "Weather entity", "weatherEntity", appConfig.weatherEntity);
    input(client, "Weather refresh interval (minutes)", "weatherMinutes",
          String(appConfig.weatherRefreshIntervalMs / 60000UL), "number");
    input(client, "ETA / Remaining switch interval (seconds)", "etaSeconds",
          String(appConfig.etaRemainingSwitchIntervalMs / 1000UL), "number");
    stateTraceSelect(client);

    client.print(F("<button type='submit'>Save configuration</button></form>"
                   "<p class='note'>Saved changes take effect after restart.</p></section>"));
    pageEnd(client);
}

int hexValue(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

String urlDecode(const String& encoded) {
    String decoded;
    decoded.reserve(encoded.length());

    for (size_t i = 0; i < encoded.length(); i++) {
        char character = encoded.charAt(i);

        if (character == '+') {
            decoded += ' ';
        } else if (character == '%' && i + 2 < encoded.length()) {
            int high = hexValue(encoded.charAt(i + 1));
            int low = hexValue(encoded.charAt(i + 2));

            if (high >= 0 && low >= 0) {
                decoded += (char)((high << 4) | low);
                i += 2;
            } else {
                decoded += character;
            }
        } else {
            decoded += character;
        }
    }

    return decoded;
}

String formValue(const String& body, const char* name) {
    String prefix = String(name) + "=";
    int start = 0;

    while (start < (int)body.length()) {
        int end = body.indexOf('&', start);
        if (end < 0)
            end = body.length();

        if (body.substring(start, start + prefix.length()) == prefix)
            return urlDecode(body.substring(start + prefix.length(), end));

        start = end + 1;
    }

    return "";
}

void copyFormValue(const String& body, const char* name, char* destination,
                   size_t destinationSize) {
    String value = formValue(body, name);
    value.trim();
    strlcpy(destination, value.c_str(), destinationSize);
}

void saveConfigurationForm(WiFiClient& client, const String& body) {
    AppConfig pending = appConfig;

    copyFormValue(body, "deviceName", pending.deviceName, sizeof(pending.deviceName));
    copyFormValue(body, "haHost", pending.homeAssistantHost, sizeof(pending.homeAssistantHost));
    copyFormValue(body, "timezone", pending.timezone, sizeof(pending.timezone));
    copyFormValue(body, "printerPrefix", pending.printerEntityPrefix,
                  sizeof(pending.printerEntityPrefix));
    copyFormValue(body, "weatherEntity", pending.weatherEntity, sizeof(pending.weatherEntity));
    pending.stateTraceEnabled = formValue(body, "stateTraceEnabled") == "1";

    String wifiTxPower = formValue(body, "wifiTxPower");
    if (wifiTxPower == "default") {
        pending.wifiTxPowerQuarterDbm = WIFI_TX_POWER_DEFAULT;
    } else if (wifiTxPower.length() > 0) {
        int16_t value = (int16_t)wifiTxPower.toInt();
        pending.wifiTxPowerQuarterDbm =
            isSupportedWifiTxPower(value) ? value : WIFI_TX_POWER_DEFAULT;
    }

    long port = formValue(body, "haPort").toInt();
    if (port > 0 && port <= 65535)
        pending.homeAssistantPort = (uint16_t)port;

    String token = formValue(body, "haToken");
    token.trim();
    if (token.length() > 0)
        strlcpy(pending.homeAssistantToken, token.c_str(), sizeof(pending.homeAssistantToken));

    long weatherMinutes = formValue(body, "weatherMinutes").toInt();
    if (weatherMinutes >= 1 && weatherMinutes <= 1440)
        pending.weatherRefreshIntervalMs = (uint32_t)weatherMinutes * 60000UL;

    long etaSeconds = formValue(body, "etaSeconds").toInt();
    if (etaSeconds >= 1 && etaSeconds <= 60)
        pending.etaRemainingSwitchIntervalMs = (uint32_t)etaSeconds * 1000UL;

    bool saved = saveConfiguration(pending);

    sendHeader(client, saved ? 200 : 500, saved ? "OK" : "Internal Server Error");
    pageStart(client, saved ? "Configuration saved" : "Save failed");
    client.print(
        saved ? F("<section><p>Configuration saved. Restart the device to apply it.</p></section>")
              : F("<section><p class='bad'>Configuration could not be written to LittleFS. Runtime "
                  "settings are unchanged.</p></section>"));
    pageEnd(client);
}

void showMaintenance(WiFiClient& client) {
    sendHeader(client, 200, "OK");
    pageStart(client, "Maintenance");
    client.print(F("<section><h2>Restart</h2><form method='post' action='/restart'>"
                   "<button type='submit'>Restart device</button></form></section>"
                   "<section><h2>Clear application configuration</h2>"
                   "<p>This restores compiled defaults on the next boot.</p>"
                   "<form method='post' action='/clear-configuration'>"
                   "<button type='submit'>Clear configuration</button></form></section>"
                   "<section><h2>Reset WiFi credentials</h2>"
                   "<p>This erases only stored WiFi credentials. Home Assistant and other "
                   "application configuration remain unchanged.</p>"
                   "<a href='/reset-wifi-confirm'>Review WiFi reset</a></section>"));

    client.print(F("<section><h2>Temporary state trace diagnostics</h2>"
                   "<p>Current log size: "));
    client.print(getStateTraceSize());
    client.print(F(" bytes</p><p><a href='/state-trace/download'>Download state trace</a></p>"
                   "<p><a href='/state-trace/clear-confirm'>Clear state trace</a></p>"
                   "</section>"));
    pageEnd(client);
}

void showWiFiResetConfirmation(WiFiClient& client) {
    sendHeader(client, 200, "OK");
    pageStart(client, "Confirm WiFi reset");
    client.print(F(
        "<section><p class='note'>This will disconnect the device, erase its stored WiFi "
        "credentials, and restart it into the Elegoo-Monitor-Setup captive portal.</p>"
        "<p>Home Assistant and all other LittleFS application configuration will be preserved.</p>"
        "<form method='post' action='/reset-wifi'>"
        "<button type='submit'>Confirm WiFi reset</button></form>"
        "<p><a href='/maintenance'>Cancel</a></p></section>"));
    pageEnd(client);
}

void showStateTraceClearConfirmation(WiFiClient& client) {
    sendHeader(client, 200, "OK");
    pageStart(client, "Clear temporary state trace");
    client.print(F("<section><p class='note'>This permanently deletes the temporary printer-state "
                   "trace log.</p>"
                   "<form method='post' action='/state-trace/clear'>"
                   "<button type='submit'>Confirm trace clear</button></form>"
                   "<p><a href='/maintenance'>Cancel</a></p></section>"));
    pageEnd(client);
}

void downloadStateTrace(WiFiClient& client) {
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/plain; charset=utf-8"));
    client.println(F("Content-Disposition: attachment; filename=state_trace.log"));
    client.println(F("Connection: close"));
    client.println();

    streamStateTrace(client);
}

void simplePage(WiFiClient& client, int code, const char* title, const char* message) {
    sendHeader(client, code, code == 200 ? "OK" : "Internal Server Error");
    pageStart(client, title);
    client.print(F("<section><p>"));
    printEscaped(client, message);
    client.print(F("</p></section>"));
    pageEnd(client);
}

void startTouchCalibrationPage(WiFiClient& client) {
    bool started = startTouchCalibration();
    simplePage(client, started ? 200 : 500,
               started ? "Touch calibration started" : "Touch calibration unavailable",
               started ? "Follow the four targets shown on the TFT. Existing calibration is kept "
                         "until the new measurements pass validation and are saved."
                       : "Touch was not detected or calibration is already active.");
}

void cancelTouchCalibrationPage(WiFiClient& client) {
    cancelTouchCalibration();
    simplePage(client, 200, "Touch calibration cancelled",
               "The previous saved calibration remains unchanged.");
}

void handleRequest(WiFiClient& client) {
    unsigned long requestStarted = millis();

    client.setTimeout(250);

    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    int firstSpace = requestLine.indexOf(' ');
    int secondSpace = requestLine.indexOf(' ', firstSpace + 1);

    if (firstSpace < 1 || secondSpace < 0)
        return;

    String method = requestLine.substring(0, firstSpace);
    String path = requestLine.substring(firstSpace + 1, secondSpace);
    int contentLength = 0;

    while (client.connected()) {
        String header = client.readStringUntil('\n');
        header.trim();

        if (header.length() == 0)
            break;

        if (header.startsWith("Content-Length:")) {
            contentLength = header.substring(15).toInt();
            if (contentLength < 0 || contentLength > 2048)
                contentLength = 0;
        }
    }

    String body;
    if (contentLength > 0) {
        body.reserve(contentLength);
        unsigned long deadline = millis() + 1000UL;

        while ((int)body.length() < contentLength && millis() < deadline) {
            while (client.available() && (int)body.length() < contentLength)
                body += (char)client.read();

            vTaskDelay(1);
        }
    }

    if (method == "GET" && path == "/")
        showStatus(client);
    else if (method == "GET" && path == "/api/status")
        writeStatusJson(client);
    else if (method == "GET" && path == "/configuration")
        showConfiguration(client);
    else if (method == "POST" && path == "/configuration")
        saveConfigurationForm(client, body);
    else if (method == "GET" && path == "/maintenance")
        showMaintenance(client);
    else if (method == "GET" && path == "/diagnostics")
        showDiagnostics(client);
    else if (method == "POST" && path == "/touch-calibration/start")
        startTouchCalibrationPage(client);
    else if (method == "POST" && path == "/touch-calibration/cancel")
        cancelTouchCalibrationPage(client);
    else if (method == "GET" && path == "/reset-wifi-confirm")
        showWiFiResetConfirmation(client);
    else if (method == "GET" && path == "/state-trace/download")
        downloadStateTrace(client);
    else if (method == "GET" && path == "/state-trace/clear-confirm")
        showStateTraceClearConfirmation(client);
    else if (method == "POST" && path == "/restart") {
        simplePage(client, 200, "Restarting", "The device is restarting.");
        client.flush();
        delay(250);
        ESP.restart();
    } else if (method == "POST" && path == "/clear-configuration") {
        bool cleared = clearConfiguration();
        simplePage(client, cleared ? 200 : 500, cleared ? "Configuration cleared" : "Clear failed",
                   cleared ? "Configuration cleared. The device is restarting with safe defaults."
                           : "Configuration could not be cleared.");

        if (cleared) {
            client.flush();
            delay(250);
            ESP.restart();
        }
    } else if (method == "POST" && path == "/reset-wifi") {
        simplePage(client, 200, "WiFi credentials cleared",
                   "Stored WiFi credentials were cleared. The device is restarting into "
                   "captive-portal setup mode.");
        client.flush();
        delay(250);
        clearStoredWiFiCredentials();
        ESP.restart();
    } else if (method == "POST" && path == "/state-trace/clear") {
        bool cleared = clearStateTrace();

        simplePage(client, cleared ? 200 : 500,
                   cleared ? "State trace cleared" : "Trace clear failed",
                   cleared ? "The temporary state trace was cleared."
                           : "The temporary state trace could not be cleared.");
    } else {
        sendHeader(client, 404, "Not Found", "text/plain");
        client.print(F("Not found"));
    }

    recordBlockingCall("WebUI.handleRequest", millis() - requestStarted);
}

void webUITask(void* parameter) {
    while (true) {
        if (!serverStarted && !isWiFiProvisioningActive() && WiFi.status() == WL_CONNECTED) {
            markBootStage(BOOT_STAGE_WEB_SERVER_BEGIN);
            server.begin();
            serverStarted = true;
            markBootStage(BOOT_STAGE_WEB_SERVER_READY);

            unsigned long callStarted = millis();
            markBootStage(BOOT_STAGE_MDNS_BEGIN);
            bool mdnsReady = appConfig.deviceName[0] != '\0' && MDNS.begin(appConfig.deviceName);
            if (mdnsReady) {
                MDNS.addService("http", "tcp", 80);
                markBootStage(BOOT_STAGE_MDNS_READY);
            } else {
                markBootStage(BOOT_STAGE_MDNS_FAILED);
            }
            recordBlockingCall("MDNS.begin", millis() - callStarted);
        }

        if (serverStarted) {
            WiFiClient client = server.available();

            if (client) {
                handleRequest(client);
                client.stop();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
} // namespace

void initializeWebUI() {
    xTaskCreate(webUITask, "web-ui", 8192, nullptr, 1, nullptr);
}

bool isWebUIReady() {
    return serverStarted;
}

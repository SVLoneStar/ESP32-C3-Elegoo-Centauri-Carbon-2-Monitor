#include "WebUI.h"

#include "AppState.h"
#include "Config.h"
#include "ConfigStore.h"
#include "Diagnostics.h"
#include "HomeAssistant.h"
#include "Weather.h"

#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace
{
WiFiServer server(80);

bool serverStarted =
  false;

void sendHeader(
  WiFiClient& client,
  int statusCode,
  const char* statusText,
  const char* contentType = "text/html"
)
{
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

void printEscaped(
  WiFiClient& client,
  const String& value
)
{
  for (size_t i = 0; i < value.length(); i++)
  {
    switch (value.charAt(i))
    {
      case '&': client.print(F("&amp;")); break;
      case '<': client.print(F("&lt;")); break;
      case '>': client.print(F("&gt;")); break;
      case '"': client.print(F("&quot;")); break;
      case '\'': client.print(F("&#39;")); break;
      default: client.print(value.charAt(i)); break;
    }
  }
}

void pageStart(
  WiFiClient& client,
  const char* title
)
{
  client.print(F(
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:system-ui,sans-serif;max-width:760px;margin:auto;padding:18px;background:#111827;color:#e5e7eb}"
    "a{color:#67e8f9;margin-right:18px}section{background:#1f2937;padding:16px;margin:16px 0;border-radius:8px}"
    "label{display:block;margin-top:12px}input{box-sizing:border-box;width:100%;padding:9px;margin-top:4px}"
    "button{padding:10px 16px;margin-top:14px}table{width:100%;border-collapse:collapse}"
    "td{padding:7px;border-bottom:1px solid #374151}td:first-child{color:#9ca3af;width:48%}"
    ".ok{color:#4ade80}.bad{color:#f87171}.note{color:#fbbf24}</style></head><body>"
    "<nav><a href='/'>Status</a><a href='/configuration'>Configuration</a><a href='/maintenance'>Maintenance</a></nav><h1>"
  ));
  printEscaped(client, title);
  client.print(F("</h1>"));
}

void pageEnd(
  WiFiClient& client
)
{
  client.print(F("</body></html>"));
}

void statusRow(
  WiFiClient& client,
  const char* label,
  const String& value,
  const char* cssClass = ""
)
{
  client.print(F("<tr><td>"));
  printEscaped(client, label);
  client.print(F("</td><td class='"));
  client.print(cssClass);
  client.print(F("'>"));
  printEscaped(client, value);
  client.print(F("</td></tr>"));
}

void showStatus(
  WiFiClient& client
)
{
  bool wifi =
    WiFi.status() == WL_CONNECTED;

  sendHeader(client, 200, "OK");
  pageStart(client, "Status");
  client.print(F("<section><table>"));

  statusRow(client, "WiFi connection", wifi ? "CONNECTED" : "OFFLINE", wifi ? "ok" : "bad");
  statusRow(client, "ESP IP", wifi ? WiFi.localIP().toString() : "--");
  statusRow(client, "HA WebSocket", wsConnected ? "CONNECTED" : "OFFLINE", wsConnected ? "ok" : "bad");
  statusRow(client, "HA authentication", authenticated ? "AUTHENTICATED" : "NOT AUTHENTICATED", authenticated ? "ok" : "bad");
  statusRow(client, "HA subscription", subscribed ? "SUBSCRIBED" : "NOT SUBSCRIBED", subscribed ? "ok" : "bad");
  statusRow(client, "Weather", getWeatherStatus());
  statusRow(client, "Boot count", String(bootCount));
  statusRow(client, "Uptime", getUptimeString());
  statusRow(client, "Reset reason", resetReasonText(lastResetReason));
  statusRow(client, "Free heap", String(ESP.getFreeHeap()));
  statusRow(client, "Minimum free heap", String(ESP.getMinFreeHeap()));

  client.print(F("</table></section>"));
  pageEnd(client);
}

void input(
  WiFiClient& client,
  const char* label,
  const char* name,
  const String& value,
  const char* type = "text"
)
{
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

void showConfiguration(
  WiFiClient& client
)
{
  sendHeader(client, 200, "OK");
  pageStart(client, "Configuration");
  client.print(F("<section><form method='post' action='/configuration'>"));

  input(client, "Device / mDNS name", "deviceName", appConfig.deviceName);
  input(client, "Home Assistant host or IP", "haHost", appConfig.homeAssistantHost);
  input(client, "Home Assistant port", "haPort", String(appConfig.homeAssistantPort), "number");
  input(client, "Home Assistant long-lived access token (leave empty to preserve)", "haToken", "", "password");
  input(client, "Timezone POSIX string", "timezone", appConfig.timezone);
  input(client, "Weather entity", "weatherEntity", appConfig.weatherEntity);
  input(client, "Weather refresh interval (minutes)", "weatherMinutes", String(appConfig.weatherRefreshIntervalMs / 60000UL), "number");
  input(client, "ETA / Remaining switch interval (seconds)", "etaSeconds", String(appConfig.etaRemainingSwitchIntervalMs / 1000UL), "number");

  client.print(F("<button type='submit'>Save configuration</button></form>"
                 "<p class='note'>Saved changes take effect after restart.</p></section>"));
  pageEnd(client);
}

int hexValue(
  char character
)
{
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

String urlDecode(
  const String& encoded
)
{
  String decoded;
  decoded.reserve(encoded.length());

  for (size_t i = 0; i < encoded.length(); i++)
  {
    char character = encoded.charAt(i);

    if (character == '+')
    {
      decoded += ' ';
    }
    else if (character == '%' && i + 2 < encoded.length())
    {
      int high = hexValue(encoded.charAt(i + 1));
      int low = hexValue(encoded.charAt(i + 2));

      if (high >= 0 && low >= 0)
      {
        decoded += (char)((high << 4) | low);
        i += 2;
      }
      else
      {
        decoded += character;
      }
    }
    else
    {
      decoded += character;
    }
  }

  return decoded;
}

String formValue(
  const String& body,
  const char* name
)
{
  String prefix = String(name) + "=";
  int start = 0;

  while (start < (int)body.length())
  {
    int end = body.indexOf('&', start);
    if (end < 0) end = body.length();

    if (body.substring(start, start + prefix.length()) == prefix)
      return urlDecode(body.substring(start + prefix.length(), end));

    start = end + 1;
  }

  return "";
}

void copyFormValue(
  const String& body,
  const char* name,
  char* destination,
  size_t destinationSize
)
{
  String value = formValue(body, name);
  value.trim();
  strlcpy(destination, value.c_str(), destinationSize);
}

void saveConfigurationForm(
  WiFiClient& client,
  const String& body
)
{
  AppConfig pending = appConfig;

  copyFormValue(body, "deviceName", pending.deviceName, sizeof(pending.deviceName));
  copyFormValue(body, "haHost", pending.homeAssistantHost, sizeof(pending.homeAssistantHost));
  copyFormValue(body, "timezone", pending.timezone, sizeof(pending.timezone));
  copyFormValue(body, "weatherEntity", pending.weatherEntity, sizeof(pending.weatherEntity));

  long port = formValue(body, "haPort").toInt();
  if (port > 0 && port <= 65535) pending.homeAssistantPort = (uint16_t)port;

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
  client.print(saved
    ? F("<section><p>Configuration saved. Restart the device to apply it.</p></section>")
    : F("<section><p class='bad'>Configuration could not be written to LittleFS. Runtime settings are unchanged.</p></section>"));
  pageEnd(client);
}

void showMaintenance(
  WiFiClient& client
)
{
  sendHeader(client, 200, "OK");
  pageStart(client, "Maintenance");
  client.print(F("<section><h2>Restart</h2><form method='post' action='/restart'>"
                 "<button type='submit'>Restart device</button></form></section>"
                 "<section><h2>Clear application configuration</h2>"
                 "<p>This restores compiled defaults on the next boot.</p>"
                 "<form method='post' action='/clear-configuration'>"
                 "<button type='submit'>Clear configuration</button></form></section>"
                 "<section><h2>Reset WiFi credentials</h2>"
                 "<p>This erases only stored WiFi credentials. Home Assistant and other application configuration remain unchanged.</p>"
                 "<a href='/reset-wifi-confirm'>Review WiFi reset</a></section>"));
  pageEnd(client);
}

void showWiFiResetConfirmation(
  WiFiClient& client
)
{
  sendHeader(client, 200, "OK");
  pageStart(client, "Confirm WiFi reset");
  client.print(F("<section><p class='note'>This will disconnect the device, erase its stored WiFi credentials, and restart it into the Elegoo-Monitor-Setup captive portal.</p>"
                 "<p>Home Assistant and all other LittleFS application configuration will be preserved.</p>"
                 "<form method='post' action='/reset-wifi'>"
                 "<button type='submit'>Confirm WiFi reset</button></form>"
                 "<p><a href='/maintenance'>Cancel</a></p></section>"));
  pageEnd(client);
}

void simplePage(
  WiFiClient& client,
  int code,
  const char* title,
  const char* message
)
{
  sendHeader(client, code, code == 200 ? "OK" : "Internal Server Error");
  pageStart(client, title);
  client.print(F("<section><p>"));
  printEscaped(client, message);
  client.print(F("</p></section>"));
  pageEnd(client);
}

void handleRequest(
  WiFiClient& client
)
{
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

  while (client.connected())
  {
    String header = client.readStringUntil('\n');
    header.trim();

    if (header.length() == 0)
      break;

    if (header.startsWith("Content-Length:"))
    {
      contentLength = header.substring(15).toInt();
      if (contentLength < 0 || contentLength > 2048) contentLength = 0;
    }
  }

  String body;
  if (contentLength > 0)
  {
    body.reserve(contentLength);
    unsigned long deadline = millis() + 1000UL;

    while ((int)body.length() < contentLength && millis() < deadline)
    {
      while (client.available() && (int)body.length() < contentLength)
        body += (char)client.read();

      vTaskDelay(1);
    }
  }

  if (method == "GET" && path == "/")
    showStatus(client);
  else if (method == "GET" && path == "/configuration")
    showConfiguration(client);
  else if (method == "POST" && path == "/configuration")
    saveConfigurationForm(client, body);
  else if (method == "GET" && path == "/maintenance")
    showMaintenance(client);
  else if (method == "GET" && path == "/reset-wifi-confirm")
    showWiFiResetConfirmation(client);
  else if (method == "POST" && path == "/restart")
  {
    simplePage(client, 200, "Restarting", "The device is restarting.");
    client.flush();
    delay(250);
    ESP.restart();
  }
  else if (method == "POST" && path == "/clear-configuration")
  {
    bool cleared = clearConfiguration();
    simplePage(
      client,
      cleared ? 200 : 500,
      cleared ? "Configuration cleared" : "Clear failed",
      cleared
        ? "Configuration cleared. The device is restarting with safe defaults."
        : "Configuration could not be cleared."
    );

    if (cleared)
    {
      client.flush();
      delay(250);
      ESP.restart();
    }
  }
  else if (method == "POST" && path == "/reset-wifi")
  {
    simplePage(
      client,
      200,
      "WiFi credentials cleared",
      "Stored WiFi credentials were cleared. The device is restarting into captive-portal setup mode."
    );
    client.flush();
    delay(250);
    clearStoredWiFiCredentials();
    ESP.restart();
  }
  else
  {
    sendHeader(client, 404, "Not Found", "text/plain");
    client.print(F("Not found"));
  }
}

void webUITask(
  void* parameter
)
{
  while (true)
  {
    if (
      !serverStarted &&
      !isWiFiProvisioningActive() &&
      WiFi.status() == WL_CONNECTED
    )
    {
      server.begin();
      serverStarted = true;

      if (appConfig.deviceName[0] != '\0' && MDNS.begin(appConfig.deviceName))
        MDNS.addService("http", "tcp", 80);
    }

    if (serverStarted)
    {
      WiFiClient client = server.available();

      if (client)
      {
        handleRequest(client);
        client.stop();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}

void initializeWebUI()
{
  xTaskCreate(
    webUITask,
    "web-ui",
    8192,
    nullptr,
    1,
    nullptr
  );
}

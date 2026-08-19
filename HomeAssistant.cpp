#include "HomeAssistant.h"
#include "Config.h"
#include "PrinterData.h"
#include <WiFiManager.h>

namespace
{
volatile bool wifiProvisioningActive =
  false;
}

// REST URL
// ============================================================

String buildRestURL(
  const char* entity
)
{
  String url =
    "http://";


  url +=
    appConfig.homeAssistantHost;


  url +=
    ":";


  url +=
    String(
      appConfig.homeAssistantPort
    );


  url +=
    "/api/states/";


  url +=
    entity;


  return url;
}


// ============================================================
// LOAD INITIAL ENTITY
// ============================================================

void loadInitialEntity(
  const char* entity
)
{
  HTTPClient http;


  String url =
    buildRestURL(
      entity
    );


  if (!http.begin(url))
  {
    Serial.print(
      "REST begin failed: "
    );


    Serial.println(
      entity
    );


    return;
  }


  http.setConnectTimeout(
    5000
  );


  http.setTimeout(
    5000
  );


  http.addHeader(
    "Authorization",
    String("Bearer ") +
    appConfig.homeAssistantToken
  );


  int code =
    http.GET();


  if (
    code !=
    200
  )
  {
    Serial.print(
      "REST "
    );


    Serial.print(
      entity
    );


    Serial.print(
      " -> "
    );


    Serial.println(
      code
    );


    http.end();


    return;
  }


  StaticJsonDocument<64> filter;


  filter["state"] =
    true;


  DynamicJsonDocument doc(
    256
  );


  DeserializationError error =
    deserializeJson(
      doc,
      http.getStream(),
      DeserializationOption::Filter(
        filter
      )
    );


  http.end();


  if (error)
  {
    Serial.print(
      "REST JSON error: "
    );


    Serial.println(
      entity
    );


    return;
  }


  String state =
    doc["state"] |
    "";


  updatePrinterEntity(
    entity,
    state
  );


  Serial.print(
    entity
  );


  Serial.print(
    " = "
  );


  Serial.println(
    state
  );
}


// ============================================================
// INITIAL PRINTER DATA
// ============================================================

void loadInitialPrinterData()
{
  if (!hasValidHomeAssistantConfig(appConfig))
  {
    Serial.println("Home Assistant configuration is incomplete.");
    return;
  }

  Serial.println();
  Serial.println(
    "Loading initial printer states..."
  );


  const char* entities[] =
  {
    ENTITY_CURRENT_STATUS,
    ENTITY_PRINT_STATUS,

    ENTITY_REMAINING,
    ENTITY_PROGRESS,

    ENTITY_CURRENT_LAYER,
    ENTITY_TOTAL_LAYERS,

    ENTITY_NOZZLE_TEMP,
    ENTITY_BED_TEMP,
    ENTITY_BOX_TEMP,

    ENTITY_PRINT_SPEED,

    ENTITY_PRINT_ERROR,
    ENTITY_ERROR_REASON
  };


  const int count =
    sizeof(entities) /
    sizeof(entities[0]);


  for (
    int i = 0;
    i < count;
    i++
  )
  {
    loadInitialEntity(
      entities[i]
    );


    delay(
      150
    );
  }


  Serial.println(
    "Initial states complete."
  );
}


// ============================================================
// RESET WS STATE
// ============================================================

void resetWebSocketState()
{
  wsConnected =
    false;


  authenticated =
    false;


  subscriptionPending =
    false;


  subscribed =
    false;


  headerStatusDirty =
    true;


  idleConnectionDirty =
    true;
}


// ============================================================
// AUTH
// ============================================================

void sendAuth()
{
  String message =
    "{\"type\":\"auth\",\"access_token\":\"";


  message +=
    appConfig.homeAssistantToken;


  message +=
    "\"}";


  Serial.println(
    "-> Sending LLAT"
  );


  client.send(
    message
  );
}


// ============================================================
// SUBSCRIBE PRINTER ONLY
// ============================================================

void subscribePrinterTriggers()
{
  String message;


  message.reserve(
    1500
  );


  message =
    "{"
    "\"id\":1,"
    "\"type\":\"subscribe_trigger\","
    "\"trigger\":{"
      "\"platform\":\"state\","
      "\"entity_id\":[\"";


  message += ENTITY_REMAINING;
  message += "\",\"";

  message += ENTITY_TOTAL_LAYERS;
  message += "\",\"";

  message += ENTITY_CURRENT_LAYER;
  message += "\",\"";

  message += ENTITY_PROGRESS;
  message += "\",\"";

  message += ENTITY_CURRENT_STATUS;
  message += "\",\"";

  message += ENTITY_PRINT_STATUS;
  message += "\",\"";

  message += ENTITY_PRINT_ERROR;
  message += "\",\"";

  message += ENTITY_ERROR_REASON;
  message += "\",\"";

  message += ENTITY_BOX_TEMP;
  message += "\",\"";

  message += ENTITY_NOZZLE_TEMP;
  message += "\",\"";

  message += ENTITY_BED_TEMP;
  message += "\",\"";

  message += ENTITY_PRINT_SPEED;


  message +=
    "\"]"
    "}"
    "}";


  Serial.println();
  Serial.println(
    "-> Sending printer-only subscribe_trigger"
  );


  Serial.print(
    "Subscription JSON size: "
  );


  Serial.print(
    message.length()
  );


  Serial.println(
    " bytes"
  );


  if (
    client.send(
      message
    )
  )
  {
    subscriptionPending =
      true;


    Serial.println(
      "   subscribe_trigger sent"
    );
  }
  else
  {
    subscriptionPending =
      false;


    subscribed =
      false;


    Serial.println(
      "   SUBSCRIPTION SEND FAILED"
    );
  }
}


// ============================================================
// HANDLE TRIGGER
// ============================================================

void handleTriggerMessage(
  const String& data
)
{
  StaticJsonDocument<256> filter;


  filter
    ["event"]
    ["variables"]
    ["trigger"]
    ["entity_id"] =
    true;


  filter
    ["event"]
    ["variables"]
    ["trigger"]
    ["to_state"]
    ["state"] =
    true;


  DynamicJsonDocument doc(
    512
  );


  DeserializationError error =
    deserializeJson(
      doc,
      data,
      DeserializationOption::Filter(
        filter
      )
    );


  if (error)
  {
    Serial.print(
      "Trigger JSON error: "
    );


    Serial.println(
      error.c_str()
    );


    return;
  }


  String entity =
    doc
      ["event"]
      ["variables"]
      ["trigger"]
      ["entity_id"] |
    "";


  if (
    entity.length() ==
    0
  )
  {
    return;
  }


  String state =
    doc
      ["event"]
      ["variables"]
      ["trigger"]
      ["to_state"]
      ["state"] |
    "";


  updatePrinterEntity(
    entity,
    state
  );


  triggerCount++;


  Serial.print(
    "[TRIGGER] "
  );


  Serial.print(
    entity
  );


  Serial.print(
    " = "
  );


  Serial.println(
    state
  );
}


// ============================================================
// MESSAGE CALLBACK
// ============================================================

void onMessageCallback(
  WebsocketsClient& wsClient,
  WebsocketsMessage message
)
{
  messageCount++;


  const String& data =
    message.data();


  if (
    data.indexOf(
      "\"type\":\"auth_required\""
    ) >=
    0
  )
  {
    Serial.println(
      "*** AUTH REQUIRED ***"
    );


    sendAuth();


    return;
  }


  if (
    data.indexOf(
      "\"type\":\"auth_ok\""
    ) >=
    0
  )
  {
    Serial.println(
      "*** AUTH OK ***"
    );


    authenticated =
      true;


    reconnectDelayMs =
      2000;


    delay(
      100
    );


    subscribePrinterTriggers();


    headerStatusDirty =
      true;


    return;
  }


  if (
    data.indexOf(
      "\"type\":\"auth_invalid\""
    ) >=
    0
  )
  {
    authenticated =
      false;


    subscriptionPending =
      false;


    subscribed =
      false;


    headerStatusDirty =
      true;


    idleConnectionDirty =
      true;


    Serial.println(
      "*** AUTH INVALID ***"
    );


    return;
  }


  if (
    data.indexOf(
      "\"type\":\"result\""
    ) >=
      0 &&
    data.indexOf(
      "\"id\":1"
    ) >=
      0
  )
  {
    if (
      data.indexOf(
        "\"success\":true"
      ) >=
      0
    )
    {
      subscriptionPending =
        false;


      subscribed =
        true;


      Serial.println(
        "*** PRINTER TRIGGER SUBSCRIPTION OK ***"
      );
    }
    else
    {
      subscriptionPending =
        false;


      subscribed =
        false;


      Serial.println(
        "*** PRINTER TRIGGER SUBSCRIPTION FAILED ***"
      );
    }


    headerStatusDirty =
      true;


    idleConnectionDirty =
      true;


    return;
  }


  if (
    !authenticated ||
    !subscribed
  )
  {
    return;
  }


  if (
    data.indexOf(
      "\"type\":\"event\""
    ) >=
      0 &&
    data.indexOf(
      "\"trigger\""
    ) >=
      0
  )
  {
    handleTriggerMessage(
      data
    );
  }
}


// ============================================================
// WS EVENT CALLBACK
// ============================================================

void onEventCallback(
  WebsocketsEvent event,
  String data
)
{
  switch (event)
  {
    case WebsocketsEvent::ConnectionOpened:
    {
      wsConnected =
        true;


      successfulConnects++;


      headerStatusDirty =
        true;


      Serial.println();
      Serial.println(
        "*** WEBSOCKET CONNECTED ***"
      );


      break;
    }


    case WebsocketsEvent::ConnectionClosed:
    {
      disconnectCount++;


      Serial.println();
      Serial.println(
        "*** WEBSOCKET DISCONNECTED ***"
      );


      resetWebSocketState();


      lastReconnectAttempt =
        millis();


      break;
    }


    case WebsocketsEvent::GotPing:
      break;


    case WebsocketsEvent::GotPong:
      break;
  }
}


// ============================================================
// WIFI
// ============================================================

bool connectWiFi()
{
  WiFi.mode(
    WIFI_STA
  );

  Serial.print(
    "Connecting WiFi"
  );

  WiFiManager wifiManager;

  wifiManager.setConnectTimeout(20);
  wifiManager.setSaveConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(300);

  wifiProvisioningActive =
    true;

  bool connected =
    wifiManager.autoConnect(
      "Elegoo-Monitor-Setup"
    );

  wifiProvisioningActive =
    false;

  if (!connected)
  {
    Serial.println();
    Serial.println(
      "*** WIFI CONNECT FAILED ***"
    );


    return false;
  }


  Serial.println(
    "*** WIFI CONNECTED ***"
  );


  Serial.print(
    "IP: "
  );


  Serial.println(
    WiFi.localIP()
  );


  headerStatusDirty =
    true;


  return true;
}


// ============================================================
// WIFI RECOVERY
// ============================================================

void maintainWiFi()
{
  static wl_status_t previousStatus =
    WiFi.status();


  wl_status_t currentStatus =
    WiFi.status();


  if (
    currentStatus !=
    previousStatus
  )
  {
    previousStatus =
      currentStatus;


    headerStatusDirty =
      true;


    idleConnectionDirty =
      true;
  }


  if (
    currentStatus ==
    WL_CONNECTED
  )
  {
    return;
  }


  resetWebSocketState();


  if (
    millis() -
      lastWiFiReconnectAttempt <
      WIFI_RECONNECT_INTERVAL
  )
  {
    return;
  }


  lastWiFiReconnectAttempt =
    millis();


  Serial.println(
    "*** WIFI LOST - reconnecting ***"
  );


  WiFi.disconnect();


  delay(
    100
  );


  WiFi.reconnect();
}


// ============================================================
// WIFI PROVISIONING STATE / RESET
// ============================================================

void clearStoredWiFiCredentials()
{
  WiFiManager wifiManager;
  wifiManager.resetSettings();
}


bool isWiFiProvisioningActive()
{
  return wifiProvisioningActive;
}


// ============================================================
// WS URL
// ============================================================

String buildWebSocketURL()
{
  String url =
    "ws://";


  url +=
    appConfig.homeAssistantHost;


  url +=
    ":";


  url +=
    String(
      appConfig.homeAssistantPort
    );


  url +=
    "/api/websocket";


  return url;
}


// ============================================================
// WS CONNECT
// ============================================================

bool tryWebSocketConnect()
{
  if (!hasValidHomeAssistantConfig(appConfig))
  {
    resetWebSocketState();
    return false;
  }

  if (
    WiFi.status() !=
    WL_CONNECTED
  )
  {
    return false;
  }


  connectAttempts++;


  String url =
    buildWebSocketURL();


  Serial.println();


  Serial.print(
    "WebSocket attempt #"
  );


  Serial.println(
    connectAttempts
  );


  Serial.print(
    "Free heap before connect: "
  );


  Serial.println(
    ESP.getFreeHeap()
  );


  bool ok =
    client.connect(
      url
    );


  if (ok)
  {
    Serial.println(
      "*** connect() SUCCESS ***"
    );


    return true;
  }


  Serial.println(
    "*** connect() FAILED ***"
  );


  resetWebSocketState();


  if (
    reconnectDelayMs <
    5000
  )
  {
    reconnectDelayMs =
      5000;
  }
  else
  {
    reconnectDelayMs *=
      2;


    if (
      reconnectDelayMs >
      MAX_RECONNECT_DELAY
    )
    {
      reconnectDelayMs =
        MAX_RECONNECT_DELAY;
    }
  }


  return false;
}


// ============================================================
// MAINTAIN WS
// ============================================================

void maintainWebSocket()
{
  if (
    WiFi.status() !=
    WL_CONNECTED
  )
  {
    return;
  }


  if (
    wsConnected
  )
  {
    client.poll();


    return;
  }


  if (
    millis() -
      lastReconnectAttempt <
      reconnectDelayMs
  )
  {
    return;
  }


  lastReconnectAttempt =
    millis();


  tryWebSocketConnect();
}

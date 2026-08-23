#include "HomeAssistant.h"
#include "Config.h"
#include "Diagnostics.h"
#include "PrinterData.h"
#include "StateTrace.h"
#include "BootStage.h"
#include <WiFiManager.h>
#include <new>

namespace {
volatile bool wifiProvisioningActive = false;

uint16_t consecutiveEarlyDisconnects = 0;

bool connectionOpenedForCurrentAttempt = false;

bool clientReinitializationPending = false;

bool clientReinitializing = false;

bool clientWasReinitialized = false;

uint32_t clientReinitializationCount = 0;

bool initialWebSocketConnectObserved = false;

void onWiFiManagerAPStarted(WiFiManager*) {
    markBootStage(BOOT_STAGE_CAPTIVE_PORTAL_STARTED);

    wifi_mode_t mode = WiFi.getMode();
    IPAddress apAddress = WiFi.softAPIP();
    if (!(mode & WIFI_AP) || apAddress == IPAddress(0, 0, 0, 0))
        markBootStage(BOOT_STAGE_CAPTIVE_PORTAL_INVALID);
}

void increaseEarlyDisconnectBackoff() {
    if (reconnectDelayMs >= MAX_RECONNECT_DELAY / 2) {
        reconnectDelayMs = MAX_RECONNECT_DELAY;
    } else {
        reconnectDelayMs *= 2;
    }
}

void recreateWebSocketClient() {
    clientReinitializationPending = false;
    clientReinitializing = true;

    client.~WebsocketsClient();
    new (&client) WebsocketsClient();

    client.onMessage(onMessageCallback);
    client.onEvent(onEventCallback);

    clientReinitializing = false;
    clientWasReinitialized = true;
    clientReinitializationCount++;

    serialDiagnostic("*** WEBSOCKET CLIENT REINITIALIZED ***");
    stateTraceLog("WS_CLIENT_REINITIALIZED", "after 3 early disconnects");
}
}

// REST URL
// ============================================================

String buildRestURL(const char* entity) {
    String url = "http://";

    url += appConfig.homeAssistantHost;

    url += ":";

    url += String(appConfig.homeAssistantPort);

    url += "/api/states/";

    url += entity;

    return url;
}

// ============================================================
// LOAD INITIAL ENTITY
// ============================================================

void loadInitialEntity(const char* entity) {
    HTTPClient http;

    String url = buildRestURL(entity);

    if (!http.begin(url)) {
        serialDiagnostic("REST begin failed: entity=%s", entity);

        return;
    }

    http.setConnectTimeout(5000);

    http.setTimeout(5000);

    http.addHeader("Authorization", String("Bearer ") + appConfig.homeAssistantToken);

    unsigned long callStarted = millis();
    int code = http.GET();
    recordBlockingCall("printerREST.GET", millis() - callStarted);

    if (code != 200) {
        serialDiagnostic("REST result | entity=%s http_code=%d", entity, code);

        http.end();

        return;
    }

    StaticJsonDocument<64> filter;

    filter["state"] = true;

    DynamicJsonDocument doc(256);

    DeserializationError error =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    http.end();

    if (error) {
        serialDiagnostic("REST JSON error: entity=%s", entity);

        return;
    }

    String state = doc["state"] | "";

    updatePrinterEntity(entity, state);

    serialDiagnostic("REST state | entity=%s state=%s", entity, state.c_str());
}

// ============================================================
// INITIAL PRINTER DATA
// ============================================================

void loadInitialPrinterData() {
    if (!hasValidHomeAssistantConfig(appConfig)) {
        serialDiagnostic("Home Assistant configuration is incomplete.");
        return;
    }

    if (!hasValidPrinterEntityConfig(appConfig)) {
        serialDiagnostic("Printer entity prefix is incomplete or invalid.");
        return;
    }

    serialDiagnostic("Loading initial printer states...");

    const char* entities[] = {ENTITY_CURRENT_STATUS, ENTITY_PRINT_STATUS,

                              ENTITY_REMAINING,      ENTITY_PROGRESS,

                              ENTITY_CURRENT_LAYER,  ENTITY_TOTAL_LAYERS,

                              ENTITY_NOZZLE_TEMP,    ENTITY_BED_TEMP,     ENTITY_BOX_TEMP,

                              ENTITY_PRINT_SPEED,

                              ENTITY_PRINT_ERROR,    ENTITY_ERROR_REASON};

    const int count = sizeof(entities) / sizeof(entities[0]);

    for (int i = 0; i < count; i++) {
        loadInitialEntity(entities[i]);

        delay(150);
    }

    serialDiagnostic("Initial states complete.");
}

// ============================================================
// RESET WS STATE
// ============================================================

void resetWebSocketState() {
    wsConnected = false;

    authenticated = false;

    subscriptionPending = false;

    subscribed = false;

    headerStatusDirty = true;

    idleConnectionDirty = true;
}

// ============================================================
// AUTH
// ============================================================

void sendAuth() {
    String message = "{\"type\":\"auth\",\"access_token\":\"";

    message += appConfig.homeAssistantToken;

    message += "\"}";

    serialDiagnostic("-> Sending LLAT");

    client.send(message);
}

// ============================================================
// SUBSCRIBE PRINTER ONLY
// ============================================================

void subscribePrinterTriggers() {
    if (!hasValidPrinterEntityConfig(appConfig)) {
        subscriptionPending = false;
        subscribed = false;
        serialDiagnostic("Printer trigger subscription skipped: entity prefix invalid.");
        return;
    }

    String message;

    message.reserve(1500);

    message = "{"
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

    message += "\"]"
               "}"
               "}";

    serialDiagnostic("-> Sending printer-only subscribe_trigger");

    serialDiagnostic("Subscription JSON size: %u bytes", (unsigned int)message.length());

    if (client.send(message)) {
        subscriptionPending = true;

        serialDiagnostic("   subscribe_trigger sent");
    } else {
        subscriptionPending = false;

        subscribed = false;

        serialDiagnostic("   SUBSCRIPTION SEND FAILED");
    }
}

// ============================================================
// HANDLE TRIGGER
// ============================================================

void handleTriggerMessage(const String& data) {
    StaticJsonDocument<256> filter;

    filter["event"]["variables"]["trigger"]["entity_id"] = true;

    filter["event"]["variables"]["trigger"]["to_state"]["state"] = true;

    DynamicJsonDocument doc(512);

    DeserializationError error = deserializeJson(doc, data, DeserializationOption::Filter(filter));

    if (error) {
        serialDiagnostic("Trigger JSON error: %s", error.c_str());

        return;
    }

    String entity = doc["event"]["variables"]["trigger"]["entity_id"] | "";

    if (entity.length() == 0) {
        return;
    }

    String state = doc["event"]["variables"]["trigger"]["to_state"]["state"] | "";

    updatePrinterEntity(entity, state);

    triggerCount++;

    serialDiagnostic("[TRIGGER] entity=%s state=%s", entity.c_str(), state.c_str());
}

// ============================================================
// MESSAGE CALLBACK
// ============================================================

void onMessageCallback(WebsocketsClient& wsClient, WebsocketsMessage message) {
    messageCount++;

    const String& data = message.data();

    if (data.indexOf("\"type\":\"auth_required\"") >= 0) {
        serialDiagnostic("*** AUTH REQUIRED ***");

        sendAuth();

        return;
    }

    if (data.indexOf("\"type\":\"auth_ok\"") >= 0) {
        serialDiagnostic("*** AUTH OK ***");

        authenticated = true;

        delay(100);

        subscribePrinterTriggers();

        headerStatusDirty = true;

        return;
    }

    if (data.indexOf("\"type\":\"auth_invalid\"") >= 0) {
        authenticated = false;

        subscriptionPending = false;

        subscribed = false;

        headerStatusDirty = true;

        idleConnectionDirty = true;

        serialDiagnostic("*** AUTH INVALID ***");

        return;
    }

    if (data.indexOf("\"type\":\"result\"") >= 0 && data.indexOf("\"id\":1") >= 0) {
        if (data.indexOf("\"success\":true") >= 0) {
            subscriptionPending = false;

            subscribed = true;

            serialDiagnostic("*** PRINTER TRIGGER SUBSCRIPTION OK ***");

            if (consecutiveEarlyDisconnects > 0 || clientWasReinitialized) {
                String recovery = "early_disconnects=";
                recovery += String(consecutiveEarlyDisconnects);

                stateTraceLog("WS_RECOVERY_SUCCESS", recovery);
                serialDiagnostic("*** WEBSOCKET RECOVERY SUCCESSFUL ***");
            }

            consecutiveEarlyDisconnects = 0;
            clientWasReinitialized = false;
            reconnectDelayMs = 2000;
        } else {
            subscriptionPending = false;

            subscribed = false;

            serialDiagnostic("*** PRINTER TRIGGER SUBSCRIPTION FAILED ***");
        }

        headerStatusDirty = true;

        idleConnectionDirty = true;

        return;
    }

    if (!authenticated || !subscribed) {
        return;
    }

    if (data.indexOf("\"type\":\"event\"") >= 0 && data.indexOf("\"trigger\"") >= 0) {
        handleTriggerMessage(data);
    }
}

// ============================================================
// WS EVENT CALLBACK
// ============================================================

void onEventCallback(WebsocketsEvent event, String data) {
    switch (event) {
    case WebsocketsEvent::ConnectionOpened: {
        wsConnected = true;

        connectionOpenedForCurrentAttempt = true;

        successfulConnects++;

        headerStatusDirty = true;

        serialDiagnostic("*** WEBSOCKET CONNECTED ***");

        stateTraceLog("WEBSOCKET", "CONNECTED");

        break;
    }

    case WebsocketsEvent::ConnectionClosed: {
        if (clientReinitializing)
            break;

        disconnectCount++;

        serialDiagnostic("*** WEBSOCKET DISCONNECTED ***");

        stateTraceLog("WEBSOCKET", "DISCONNECTED");

        bool earlyDisconnect = connectionOpenedForCurrentAttempt && !(authenticated && subscribed);

        if (earlyDisconnect) {
            consecutiveEarlyDisconnects++;
            increaseEarlyDisconnectBackoff();

            serialDiagnostic("Early WebSocket disconnects: count=%u",
                             (unsigned int)consecutiveEarlyDisconnects);

            String detail = "count=";
            detail += String(consecutiveEarlyDisconnects);
            detail += " backoff_ms=";
            detail += String(reconnectDelayMs);

            stateTraceLog("WS_EARLY_DISCONNECT", detail);

            if (consecutiveEarlyDisconnects >= 3)
                clientReinitializationPending = true;
        }

        connectionOpenedForCurrentAttempt = false;

        resetWebSocketState();

        lastReconnectAttempt = millis();

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

bool connectWiFi() {
    markBootStage(BOOT_STAGE_WIFI_MODE_BEGIN);
    bool wifiModeReady = WiFi.mode(WIFI_STA);
    markBootStage(wifiModeReady ? BOOT_STAGE_WIFI_MODE_READY : BOOT_STAGE_WIFI_MODE_FAILED);

    serialDiagnostic("WiFi setup       : starting");

    markBootStage(BOOT_STAGE_WIFI_MANAGER_BEGIN);
    WiFiManager wifiManager;

    wifiManager.setConnectTimeout(20);
    wifiManager.setSaveConnectTimeout(20);
    wifiManager.setConfigPortalTimeout(300);
    wifiManager.setAPCallback(onWiFiManagerAPStarted);

    wifiProvisioningActive = true;

    markBootStage(BOOT_STAGE_STORED_WIFI_BEGIN);
    bool connected = wifiManager.autoConnect("Elegoo-Monitor-Setup");

    wifiProvisioningActive = false;

    if (!connected) {
        markBootStage(BOOT_STAGE_WIFI_CONNECT_FAILED);
        serialDiagnostic("*** WIFI CONNECT FAILED ***");
        serialDiagnostic("WiFi result | status=%d last_result=%d mode=%d", (int)WiFi.status(),
                         (int)wifiManager.getLastConxResult(), (int)WiFi.getMode());

        return false;
    }

    markBootStage(BOOT_STAGE_WIFI_CONNECTED);
    serialDiagnostic("*** WIFI CONNECTED ***");

    String localIp = WiFi.localIP().toString();
    serialDiagnostic("WiFi IP          : %s", localIp.c_str());

    headerStatusDirty = true;

    return true;
}

// ============================================================
// WIFI RECOVERY
// ============================================================

void maintainWiFi() {
    static wl_status_t previousStatus = WiFi.status();

    wl_status_t currentStatus = WiFi.status();

    if (currentStatus != previousStatus) {
        previousStatus = currentStatus;

        headerStatusDirty = true;

        idleConnectionDirty = true;
    }

    if (currentStatus == WL_CONNECTED) {
        return;
    }

    resetWebSocketState();

    if (millis() - lastWiFiReconnectAttempt < WIFI_RECONNECT_INTERVAL) {
        return;
    }

    lastWiFiReconnectAttempt = millis();

    serialDiagnostic("*** WIFI LOST - reconnecting ***");

    WiFi.disconnect();

    delay(100);

    WiFi.reconnect();
}

// ============================================================
// WIFI PROVISIONING STATE / RESET
// ============================================================

void clearStoredWiFiCredentials() {
    WiFiManager wifiManager;
    wifiManager.resetSettings();
}

bool isWiFiProvisioningActive() {
    return wifiProvisioningActive;
}

// ============================================================
// WS URL
// ============================================================

String buildWebSocketURL() {
    String url = "ws://";

    url += appConfig.homeAssistantHost;

    url += ":";

    url += String(appConfig.homeAssistantPort);

    url += "/api/websocket";

    return url;
}

// ============================================================
// WS CONNECT
// ============================================================

bool tryWebSocketConnect() {
    if (!hasValidHomeAssistantConfig(appConfig)) {
        resetWebSocketState();
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (!hasValidPrinterEntityConfig(appConfig)) {
        resetWebSocketState();
        return false;
    }

    if (clientReinitializationPending)
        recreateWebSocketClient();

    connectAttempts++;

    connectionOpenedForCurrentAttempt = false;

    String url = buildWebSocketURL();

    serialDiagnostic("WebSocket connect | attempt=%lu free_heap=%u",
                     (unsigned long)connectAttempts, (unsigned int)ESP.getFreeHeap());

    bool observeInitialConnect = !initialWebSocketConnectObserved;
    if (observeInitialConnect)
        markBootStage(BOOT_STAGE_WEBSOCKET_CONNECT_BEGIN);

    unsigned long callStarted = millis();
    bool ok = client.connect(url);
    recordBlockingCall("WebsocketsClient.connect", millis() - callStarted);
    initialWebSocketConnectObserved = true;

    if (ok) {
        if (observeInitialConnect)
            markBootStage(BOOT_STAGE_WEBSOCKET_CONNECT_OPENED);
        serialDiagnostic("*** connect() SUCCESS ***");

        return true;
    }

    if (observeInitialConnect)
        markBootStage(BOOT_STAGE_WEBSOCKET_CONNECT_FAILED);
    serialDiagnostic("*** connect() FAILED ***");

    resetWebSocketState();

    if (reconnectDelayMs < 5000) {
        reconnectDelayMs = 5000;
    } else {
        reconnectDelayMs *= 2;

        if (reconnectDelayMs > MAX_RECONNECT_DELAY) {
            reconnectDelayMs = MAX_RECONNECT_DELAY;
        }
    }

    return false;
}

// ============================================================
// MAINTAIN WS
// ============================================================

void maintainWebSocket() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (wsConnected) {
        client.poll();

        return;
    }

    if (millis() - lastReconnectAttempt < reconnectDelayMs) {
        return;
    }

    lastReconnectAttempt = millis();

    tryWebSocketConnect();
}

uint16_t getConsecutiveEarlyDisconnectCount() {
    return consecutiveEarlyDisconnects;
}

uint32_t getWebSocketClientReinitializationCount() {
    return clientReinitializationCount;
}

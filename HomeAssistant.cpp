#include "HomeAssistant.h"
#include "Config.h"
#include "Diagnostics.h"
#include "PrinterData.h"
#include "StateTrace.h"
#include "BootStage.h"
#include "Display.h"
#include <WiFiManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <new>

namespace {
constexpr uint8_t INITIAL_ENTITY_COUNT = 12;
constexpr uint8_t INITIAL_TRANSPORT_FAILURE_LIMIT = 2;
constexpr size_t INITIAL_STATE_SIZE = 256;
constexpr unsigned long HA_STARTUP_GRACE_MS = 1500;

enum InitialEntityLoadResult : uint8_t {
    INITIAL_ENTITY_OK,
    INITIAL_ENTITY_FAILED,
    INITIAL_ENTITY_TIMEOUT
};

enum InitialLoadEventType : uint8_t {
    INITIAL_LOAD_ENTITY_RESULT,
    INITIAL_LOAD_FINISHED
};

struct InitialLoadEvent {
    InitialLoadEventType type;
    uint8_t entityIndex;
    uint8_t completed;
    uint8_t successful;
    InitialEntityLoadResult result;
    bool aborted;
    bool timedOut;
    int httpCode;
    uint32_t entityGeneration;
    uint32_t requestElapsedMs;
    uint32_t totalElapsedMs;
    char state[INITIAL_STATE_SIZE];
};

QueueHandle_t initialLoadQueue = nullptr;
TaskHandle_t initialLoadTaskHandle = nullptr;
volatile bool initialLoadRunning = false;
bool initialLoadStarted = false;
bool initialLoadRequested = false;
volatile uint32_t printerEntityGenerations[INITIAL_ENTITY_COUNT] = {};
unsigned long haStartupReadyAt = 0;
bool haStartupGracePending = false;
bool wifiTxPowerApplyAttempted = false;
bool wifiTxPowerApplied = false;
bool wifiTxPowerEffectiveValid = false;
int16_t effectiveWifiTxPowerQuarterDbm = 0;

volatile bool wifiProvisioningActive = false;

uint16_t consecutiveEarlyDisconnects = 0;

bool connectionOpenedForCurrentAttempt = false;

bool clientReinitializationPending = false;

bool clientReinitializing = false;

bool clientWasReinitialized = false;

uint32_t clientReinitializationCount = 0;

bool initialWebSocketConnectObserved = false;

void beginHomeAssistantStartupGrace() {
    haStartupReadyAt = millis() + HA_STARTUP_GRACE_MS;
    haStartupGracePending = true;
    updateBootHomeAssistantStatus("CONNECTING");
    serialDiagnostic("HA_STARTUP_GRACE | started duration_ms=%lu",
                     (unsigned long)HA_STARTUP_GRACE_MS);
}

bool homeAssistantStartupGraceActive() {
    if (!haStartupGracePending)
        return false;
    if ((int32_t)(millis() - haStartupReadyAt) < 0)
        return true;

    haStartupGracePending = false;
    serialDiagnostic("HA_STARTUP_GRACE | complete");
    return false;
}

const char* initialEntityAt(uint8_t index) {
    const char* entities[INITIAL_ENTITY_COUNT] = {
        ENTITY_CURRENT_STATUS, ENTITY_PRINT_STATUS, ENTITY_REMAINING,   ENTITY_PROGRESS,
        ENTITY_CURRENT_LAYER,  ENTITY_TOTAL_LAYERS, ENTITY_NOZZLE_TEMP, ENTITY_BED_TEMP,
        ENTITY_BOX_TEMP,       ENTITY_PRINT_SPEED,   ENTITY_PRINT_ERROR, ENTITY_ERROR_REASON
    };
    return index < INITIAL_ENTITY_COUNT ? entities[index] : "";
}

int initialEntityIndex(const String& entity) {
    for (uint8_t i = 0; i < INITIAL_ENTITY_COUNT; i++) {
        if (entity == initialEntityAt(i))
            return i;
    }
    return -1;
}

const char* httpClientErrorName(int code) {
    switch (code) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return "HTTPC_ERROR_CONNECTION_REFUSED";
    case HTTPC_ERROR_SEND_HEADER_FAILED: return "HTTPC_ERROR_SEND_HEADER_FAILED";
    case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return "HTTPC_ERROR_SEND_PAYLOAD_FAILED";
    case HTTPC_ERROR_NOT_CONNECTED: return "HTTPC_ERROR_NOT_CONNECTED";
    case HTTPC_ERROR_CONNECTION_LOST: return "HTTPC_ERROR_CONNECTION_LOST";
    case HTTPC_ERROR_NO_STREAM: return "HTTPC_ERROR_NO_STREAM";
    case HTTPC_ERROR_NO_HTTP_SERVER: return "HTTPC_ERROR_NO_HTTP_SERVER";
    case HTTPC_ERROR_TOO_LESS_RAM: return "HTTPC_ERROR_TOO_LESS_RAM";
    case HTTPC_ERROR_ENCODING: return "HTTPC_ERROR_ENCODING";
    case HTTPC_ERROR_STREAM_WRITE: return "HTTPC_ERROR_STREAM_WRITE";
    case HTTPC_ERROR_READ_TIMEOUT: return "HTTPC_ERROR_READ_TIMEOUT";
    default: return "HTTPC_ERROR_UNKNOWN";
    }
}

void onWiFiManagerAPStarted(WiFiManager*) {
    markBootStage(BOOT_STAGE_CAPTIVE_PORTAL_STARTED);
    updateBootWiFiStatus("SETUP AP");

    wifi_mode_t mode = WiFi.getMode();
    IPAddress apAddress = WiFi.softAPIP();
    if (!(mode & WIFI_AP) || apAddress == IPAddress(0, 0, 0, 0)) {
        markBootStage(BOOT_STAGE_CAPTIVE_PORTAL_INVALID);
        updateBootWiFiStatus("FAILED");
    }
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

InitialEntityLoadResult fetchInitialEntity(const char* entity, const char* host, uint16_t port,
                                           const char* token, char* state, size_t stateSize,
                                           int& httpCode, uint32_t& elapsedMs) {
    HTTPClient http;
    String url = "http://";
    url += host;
    url += ":";
    url += String(port);
    url += "/api/states/";
    url += entity;

    if (!http.begin(url)) {
        httpCode = HTTPC_ERROR_NO_HTTP_SERVER;
        elapsedMs = 0;
        return INITIAL_ENTITY_FAILED;
    }

    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.addHeader("Authorization", String("Bearer ") + token);

    unsigned long requestStarted = millis();
    httpCode = http.GET();
    elapsedMs = millis() - requestStarted;

    if (httpCode != 200) {
        http.end();
        return httpCode == HTTPC_ERROR_READ_TIMEOUT ? INITIAL_ENTITY_TIMEOUT
                                                    : INITIAL_ENTITY_FAILED;
    }

    StaticJsonDocument<64> filter;
    filter["state"] = true;
    DynamicJsonDocument doc(256);
    DeserializationError error =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (error)
        return INITIAL_ENTITY_FAILED;

    const char* parsedState = doc["state"] | "";
    if (strlen(parsedState) >= stateSize)
        return INITIAL_ENTITY_FAILED;

    strlcpy(state, parsedState, stateSize);
    return INITIAL_ENTITY_OK;
}

void initialPrinterDataTask(void*) {
    char host[sizeof(appConfig.homeAssistantHost)];
    char token[sizeof(appConfig.homeAssistantToken)];
    strlcpy(host, appConfig.homeAssistantHost, sizeof(host));
    strlcpy(token, appConfig.homeAssistantToken, sizeof(token));
    uint16_t port = appConfig.homeAssistantPort;
    uint8_t completed = 0;
    uint8_t successful = 0;
    uint8_t consecutiveTransportFailures = 0;
    bool aborted = false;
    bool timedOut = false;
    unsigned long totalStarted = millis();

    for (uint8_t i = 0; i < INITIAL_ENTITY_COUNT; i++) {
        InitialLoadEvent event = {};
        event.type = INITIAL_LOAD_ENTITY_RESULT;
        event.entityIndex = i;
        event.entityGeneration = printerEntityGenerations[i];
        event.result = fetchInitialEntity(initialEntityAt(i), host, port, token, event.state,
                                          sizeof(event.state), event.httpCode,
                                          event.requestElapsedMs);
        completed++;
        event.completed = completed;

        if (event.result == INITIAL_ENTITY_OK) {
            successful++;
            consecutiveTransportFailures = 0;
        } else if (event.httpCode < 0) {
            consecutiveTransportFailures++;
            if (event.result == INITIAL_ENTITY_TIMEOUT)
                timedOut = true;
        } else {
            consecutiveTransportFailures = 0;
        }

        xQueueSend(initialLoadQueue, &event, pdMS_TO_TICKS(100));

        if (consecutiveTransportFailures >= INITIAL_TRANSPORT_FAILURE_LIMIT) {
            aborted = true;
            break;
        }

        delay(150);
    }

    InitialLoadEvent finished = {};
    finished.type = INITIAL_LOAD_FINISHED;
    finished.completed = completed;
    finished.successful = successful;
    finished.aborted = aborted;
    finished.timedOut = timedOut;
    finished.totalElapsedMs = millis() - totalStarted;
    xQueueSend(initialLoadQueue, &finished, pdMS_TO_TICKS(100));
    initialLoadRunning = false;
    initialLoadTaskHandle = nullptr;
    vTaskDelete(nullptr);
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
    char state[INITIAL_STATE_SIZE];
    int httpCode = 0;
    uint32_t elapsedMs = 0;
    InitialEntityLoadResult result =
        fetchInitialEntity(entity, appConfig.homeAssistantHost, appConfig.homeAssistantPort,
                           appConfig.homeAssistantToken, state, sizeof(state), httpCode, elapsedMs);
    recordBlockingCall("printerREST.GET", elapsedMs);
    if (result == INITIAL_ENTITY_OK)
        updatePrinterEntity(entity, state);
}

// ============================================================
// INITIAL PRINTER DATA
// ============================================================

void loadInitialPrinterData() {
    if (initialLoadStarted || initialLoadRunning)
        return;

    if (!hasValidHomeAssistantConfig(appConfig)) {
        initialLoadStarted = true;
        serialDiagnostic("Home Assistant configuration is incomplete.");
        updateBootHomeAssistantStatus("DISABLED");
        updateBootPrinterProgress(0, INITIAL_ENTITY_COUNT, "DISABLED");
        return;
    }

    if (!hasValidPrinterEntityConfig(appConfig)) {
        initialLoadStarted = true;
        serialDiagnostic("Printer entity prefix is incomplete or invalid.");
        updateBootPrinterProgress(0, INITIAL_ENTITY_COUNT, "DISABLED");
        return;
    }

    if (WiFi.status() != WL_CONNECTED || homeAssistantStartupGraceActive()) {
        initialLoadRequested = true;
        updateBootHomeAssistantStatus("CONNECTING");
        updateBootPrinterProgress(0, INITIAL_ENTITY_COUNT, "WAITING");
        return;
    }

    initialLoadRequested = false;
    initialLoadStarted = true;

    initialLoadQueue = xQueueCreate(INITIAL_ENTITY_COUNT + 1, sizeof(InitialLoadEvent));
    if (initialLoadQueue == nullptr) {
        serialDiagnostic("INITIAL_REST | start_failed reason=queue_allocation");
        updateBootHomeAssistantStatus("FAILED");
        updateBootPrinterProgress(0, INITIAL_ENTITY_COUNT, "FAILED");
        return;
    }

    initialLoadRunning = true;
    markBootStage(BOOT_STAGE_INITIAL_REST_BEGIN);
    updateBootHomeAssistantStatus("CONNECTING");
    updateBootPrinterProgress(0, INITIAL_ENTITY_COUNT, "WAITING");
    serialDiagnostic("INITIAL_REST | task_started total=%u", INITIAL_ENTITY_COUNT);

    BaseType_t created = xTaskCreate(initialPrinterDataTask, "initial-printer-rest", 8192, nullptr,
                                     1, &initialLoadTaskHandle);
    if (created != pdPASS) {
        initialLoadRunning = false;
        initialLoadTaskHandle = nullptr;
        vQueueDelete(initialLoadQueue);
        initialLoadQueue = nullptr;
        serialDiagnostic("INITIAL_REST | start_failed reason=task_creation");
        updateBootHomeAssistantStatus("FAILED");
        updateBootPrinterProgress(0, INITIAL_ENTITY_COUNT, "FAILED");
    }
}

void serviceInitialPrinterDataLoad() {
    if (initialLoadRequested && !initialLoadStarted && WiFi.status() == WL_CONNECTED &&
        !homeAssistantStartupGraceActive()) {
        loadInitialPrinterData();
    }

    if (initialLoadQueue == nullptr)
        return;

    InitialLoadEvent event;
    while (xQueueReceive(initialLoadQueue, &event, 0) == pdTRUE) {
        if (event.type == INITIAL_LOAD_ENTITY_RESULT) {
            const char* entity = initialEntityAt(event.entityIndex);
            if (event.result == INITIAL_ENTITY_OK) {
                if (printerEntityGenerations[event.entityIndex] == event.entityGeneration) {
                    updatePrinterEntity(entity, event.state);
                } else {
                    serialDiagnostic("INITIAL_REST | stale_result_skipped entity=%s", entity);
                }
            } else if (event.httpCode < 0) {
                serialDiagnostic("REST result | entity=%s http_code=%d error=%s", entity,
                                 event.httpCode, httpClientErrorName(event.httpCode));
            } else if (event.httpCode == 200) {
                serialDiagnostic("REST result | entity=%s http_code=200 error=INVALID_RESPONSE",
                                 entity);
            } else {
                serialDiagnostic("REST result | entity=%s http_code=%d", entity, event.httpCode);
            }

            updateBootPrinterProgress(event.completed, INITIAL_ENTITY_COUNT);
            serialDiagnostic("INITIAL_REST | progress=%u/%u entity=%s elapsed_ms=%lu",
                             event.completed, INITIAL_ENTITY_COUNT, entity,
                             (unsigned long)event.requestElapsedMs);
            continue;
        }

        const char* finalStatus = event.successful == INITIAL_ENTITY_COUNT
                                      ? "OK"
                                      : event.timedOut ? "TIMEOUT" : "FAILED";
        updateBootPrinterProgress(event.completed, INITIAL_ENTITY_COUNT, finalStatus);
        if (event.aborted) {
            serialDiagnostic("INITIAL_REST | aborted reason=consecutive_transport_failures "
                             "completed=%u/%u elapsed_ms=%lu",
                             event.completed, INITIAL_ENTITY_COUNT,
                             (unsigned long)event.totalElapsedMs);
        } else {
            serialDiagnostic("INITIAL_REST | completed successful=%u/%u elapsed_ms=%lu",
                             event.successful, INITIAL_ENTITY_COUNT,
                             (unsigned long)event.totalElapsedMs);
        }
        markBootStage(BOOT_STAGE_INITIAL_REST_COMPLETE);
        vQueueDelete(initialLoadQueue);
        initialLoadQueue = nullptr;
        return;
    }
}

bool initialPrinterDataLoadRunning() {
    return initialLoadRunning;
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

    int entityIndex = initialEntityIndex(entity);
    if (entityIndex >= 0)
        printerEntityGenerations[entityIndex]++;

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
            updateBootHomeAssistantStatus("OK");

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
    unsigned long callStarted = millis();
    bool wifiModeReady = WiFi.mode(WIFI_STA);
    unsigned long callElapsed = millis() - callStarted;
    markBootStage(wifiModeReady ? BOOT_STAGE_WIFI_MODE_READY : BOOT_STAGE_WIFI_MODE_FAILED);
    recordSetupTiming("WiFi.mode", callElapsed);

    wifiTxPowerApplyAttempted = appConfig.wifiTxPowerQuarterDbm != WIFI_TX_POWER_DEFAULT;
    wifiTxPowerApplied = false;
    wifiTxPowerEffectiveValid = false;

    if (wifiModeReady && !wifiTxPowerApplyAttempted) {
        effectiveWifiTxPowerQuarterDbm = (int16_t)WiFi.getTxPower();
        wifiTxPowerEffectiveValid = true;
        String effectivePower = getEffectiveWifiTxPower();
        serialDiagnostic("WiFi TX power | configured=DEFAULT effective=%s",
                         effectivePower.c_str());
    } else if (wifiModeReady) {
        wifiTxPowerApplied =
            WiFi.setTxPower((wifi_power_t)appConfig.wifiTxPowerQuarterDbm);
        if (wifiTxPowerApplied) {
            effectiveWifiTxPowerQuarterDbm = (int16_t)WiFi.getTxPower();
            wifiTxPowerEffectiveValid = true;
        }

        String configuredPower = getConfiguredWifiTxPower();
        String effectivePower = getEffectiveWifiTxPower();
        serialDiagnostic("WiFi TX power | configured=%s applied=%s effective=%s",
                         configuredPower.c_str(), wifiTxPowerApplied ? "YES" : "NO",
                         effectivePower.c_str());
    } else {
        String configuredPower = getConfiguredWifiTxPower();
        serialDiagnostic("WiFi TX power | configured=%s applied=NO effective=UNKNOWN",
                         configuredPower.c_str());
    }

    serialDiagnostic("WiFi setup       : starting");

    markBootStage(BOOT_STAGE_WIFI_MANAGER_BEGIN);
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(false);

    wifiManager.setConnectTimeout(20);
    wifiManager.setSaveConnectTimeout(20);
    wifiManager.setConfigPortalTimeout(300);
    wifiManager.setAPCallback(onWiFiManagerAPStarted);

    wifiProvisioningActive = true;

    markBootStage(BOOT_STAGE_STORED_WIFI_BEGIN);
    callStarted = millis();
    bool connected = wifiManager.autoConnect("Elegoo-Monitor-Setup");
    callElapsed = millis() - callStarted;
    markBootStage(BOOT_STAGE_WIFI_MANAGER_COMPLETE);
    recordSetupTiming("WiFiManager.autoConnect", callElapsed);

    wifiProvisioningActive = false;

    if (!connected) {
        markBootStage(BOOT_STAGE_WIFI_CONNECT_FAILED);
        serialDiagnostic("*** WIFI CONNECT FAILED ***");
        serialDiagnostic("WiFi result | status=%d last_result=%d mode=%d", (int)WiFi.status(),
                         (int)wifiManager.getLastConxResult(), (int)WiFi.getMode());

        return false;
    }

    markBootStage(BOOT_STAGE_WIFI_CONNECTED);
    beginHomeAssistantStartupGrace();
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

        if (currentStatus == WL_CONNECTED)
            beginHomeAssistantStartupGrace();
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
    wifiManager.setDebugOutput(false);
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

    if (homeAssistantStartupGraceActive())
        return;

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

String getConfiguredWifiTxPower() {
    return wifiTxPowerText(appConfig.wifiTxPowerQuarterDbm);
}

String getEffectiveWifiTxPower() {
    return wifiTxPowerEffectiveValid ? wifiTxPowerText(effectiveWifiTxPowerQuarterDbm) : "UNKNOWN";
}

String getWifiTxPowerApplyStatus() {
    if (!wifiTxPowerApplyAttempted)
        return "DEFAULT - NOT APPLIED";
    return wifiTxPowerApplied ? "APPLIED" : "FAILED";
}

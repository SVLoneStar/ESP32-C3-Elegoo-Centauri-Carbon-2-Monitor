#pragma once
#include "AppState.h"

String buildRestURL(const char* entity);
void loadInitialEntity(const char* entity);
void loadInitialPrinterData();
void serviceInitialPrinterDataLoad();
bool initialPrinterDataLoadRunning();
void resetWebSocketState();
void sendAuth();
void subscribePrinterTriggers();
void handleTriggerMessage(const String& data);
void onMessageCallback(WebsocketsClient& wsClient, WebsocketsMessage message);
void onEventCallback(WebsocketsEvent event, String data);
bool connectWiFi();
void maintainWiFi();
void clearStoredWiFiCredentials();
bool isWiFiProvisioningActive();
String buildWebSocketURL();
bool tryWebSocketConnect();
void maintainWebSocket();
uint16_t getConsecutiveEarlyDisconnectCount();
uint32_t getWebSocketClientReinitializationCount();
String getConfiguredWifiTxPower();
String getEffectiveWifiTxPower();
String getWifiTxPowerApplyStatus();

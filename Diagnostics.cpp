#include "Diagnostics.h"

// BOOT COUNTER
// ============================================================

void initBootCounter()
{
  preferences.begin(
    "cc2monitor",
    false
  );


  bootCount =
    preferences.getUInt(
      "bootCount",
      0
    );


  bootCount++;


  preferences.putUInt(
    "bootCount",
    bootCount
  );


  preferences.end();
}


// ============================================================
// RESET REASON
// ============================================================

String resetReasonText(
  esp_reset_reason_t reason
)
{
  switch (reason)
  {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";

    case ESP_RST_POWERON:
      return "POWER ON";

    case ESP_RST_EXT:
      return "EXTERNAL";

    case ESP_RST_SW:
      return "SOFTWARE";

    case ESP_RST_PANIC:
      return "PANIC";

    case ESP_RST_INT_WDT:
      return "INT WDT";

    case ESP_RST_TASK_WDT:
      return "TASK WDT";

    case ESP_RST_WDT:
      return "WATCHDOG";

    case ESP_RST_DEEPSLEEP:
      return "DEEP SLEEP";

    case ESP_RST_BROWNOUT:
      return "BROWNOUT";

    case ESP_RST_SDIO:
      return "SDIO";

    default:
      return "OTHER";
  }
}


void printResetReason()
{
  lastResetReason =
    esp_reset_reason();


  Serial.println();
  Serial.println(
    "========== LAST RESET =========="
  );


  Serial.print(
    "Boot count   : "
  );

  Serial.println(
    bootCount
  );


  Serial.print(
    "Reset reason : "
  );

  Serial.println(
    resetReasonText(
      lastResetReason
    )
  );


  Serial.println(
    "================================"
  );
}


// ============================================================
// UPTIME
// ============================================================

String getUptimeString()
{
  unsigned long totalSeconds =
    millis() /
    1000UL;


  unsigned long days =
    totalSeconds /
    86400UL;


  totalSeconds %=
    86400UL;


  unsigned long hours =
    totalSeconds /
    3600UL;


  totalSeconds %=
    3600UL;


  unsigned long minutes =
    totalSeconds /
    60UL;


  char buffer[24];


  if (
    days >
    0
  )
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "%lud %02luh %02lum",
      days,
      hours,
      minutes
    );
  }
  else
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "%02luh %02lum",
      hours,
      minutes
    );
  }


  return String(
    buffer
  );

}

// SERIAL STATUS
// ============================================================

void printStatus()
{
  Serial.println();
  Serial.println(
    "============ STATUS ============"
  );


  Serial.print(
    "Boot count       : "
  );

  Serial.println(
    bootCount
  );


  Serial.print(
    "Last reset       : "
  );

  Serial.println(
    resetReasonText(
      lastResetReason
    )
  );


  Serial.print(
    "Uptime           : "
  );

  Serial.println(
    getUptimeString()
  );


  Serial.print(
    "Mode             : "
  );

  Serial.println(
    currentDisplayMode ==
      MODE_PRINTING
      ? "PRINTING"
      : "IDLE"
  );


  Serial.print(
    "WiFi             : "
  );

  Serial.println(
    WiFi.status() ==
      WL_CONNECTED
      ? "CONNECTED"
      : "OFFLINE"
  );


  Serial.print(
    "WebSocket        : "
  );

  Serial.println(
    wsConnected
      ? "CONNECTED"
      : "OFFLINE"
  );


  Serial.print(
    "Authenticated    : "
  );

  Serial.println(
    authenticated
      ? "YES"
      : "NO"
  );


  Serial.print(
    "Subscribed       : "
  );

  Serial.println(
    subscribed
      ? "YES"
      : "NO"
  );


  Serial.print(
    "Connect attempts : "
  );

  Serial.println(
    connectAttempts
  );


  Serial.print(
    "Successful       : "
  );

  Serial.println(
    successfulConnects
  );


  Serial.print(
    "Disconnects      : "
  );

  Serial.println(
    disconnectCount
  );


  Serial.print(
    "WS messages      : "
  );

  Serial.println(
    messageCount
  );


  Serial.print(
    "Printer triggers : "
  );

  Serial.println(
    triggerCount
  );


  Serial.print(
    "Free heap        : "
  );

  Serial.println(
    ESP.getFreeHeap()
  );


  Serial.print(
    "Min free heap    : "
  );

  Serial.println(
    ESP.getMinFreeHeap()
  );


  Serial.println(
    "================================"
  );
}

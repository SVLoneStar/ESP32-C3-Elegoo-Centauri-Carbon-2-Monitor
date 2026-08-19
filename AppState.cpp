#include "AppState.h"

// USER CONFIG
// ============================================================

const char* WIFI_SSID =
  "Wlan4Mi0nly102";

const char* WIFI_PASSWORD =
  "Afew$more4wlan";


// ============================================================
// HOME ASSISTANT ENTITIES
// ============================================================

const char* ENTITY_REMAINING =
  "sensor.centauri_carbon_2_remaining_print_time";

const char* ENTITY_TOTAL_LAYERS =
  "sensor.centauri_carbon_2_total_layers";

const char* ENTITY_CURRENT_LAYER =
  "sensor.centauri_carbon_2_current_layer";

const char* ENTITY_PROGRESS =
  "sensor.centauri_carbon_2_percent_complete";

const char* ENTITY_CURRENT_STATUS =
  "sensor.centauri_carbon_2_aktueller_status";

const char* ENTITY_PRINT_STATUS =
  "sensor.centauri_carbon_2_druckstatus";

const char* ENTITY_PRINT_ERROR =
  "sensor.centauri_carbon_2_druckfehler";

const char* ENTITY_ERROR_REASON =
  "sensor.centauri_carbon_2_fehlerstatusgrund";

const char* ENTITY_BOX_TEMP =
  "sensor.centauri_carbon_2_box_temp";

const char* ENTITY_NOZZLE_TEMP =
  "sensor.centauri_carbon_2_nozzle_temperature";

const char* ENTITY_BED_TEMP =
  "sensor.centauri_carbon_2_bed_temperature";

const char* ENTITY_PRINT_SPEED =
  "sensor.centauri_carbon_2_print_speed";


// ============================================================
// TFT
// ============================================================

#define TFT_SCK   4
#define TFT_MOSI  6
#define TFT_CS    7
#define TFT_DC    3
#define TFT_RST   1

SPIClass tftSPI(FSPI);

Adafruit_ILI9341 tft(
  &tftSPI,
  TFT_DC,
  TFT_CS,
  TFT_RST
);


// ============================================================
// COLORS
// ============================================================

#define C_BG       ILI9341_BLACK
#define C_TEXT     ILI9341_WHITE
#define C_DIM      0x7BEF
#define C_GREY     0xBDF7

#define C_CYAN     ILI9341_CYAN
#define C_GREEN    ILI9341_GREEN
#define C_ORANGE   0xFD20
#define C_RED      ILI9341_RED

#define C_BAR_BG   0x2104


// ============================================================
// BOOT DIAGNOSTICS
// ============================================================

Preferences preferences;

uint32_t bootCount =
  0;

esp_reset_reason_t lastResetReason =
  ESP_RST_UNKNOWN;


// ============================================================
// PRINTER DATA
// ============================================================

PrinterData printer;


// ============================================================
// DISPLAY MODE
// ============================================================

DisplayMode currentDisplayMode =
  MODE_UNKNOWN;


// ============================================================
// WEBSOCKET
// ============================================================

WebsocketsClient client;

bool wsConnected =
  false;

bool authenticated =
  false;

bool subscriptionPending =
  false;

bool subscribed =
  false;


// ============================================================
// CONNECTION COUNTERS
// ============================================================

unsigned long connectAttempts =
  0;

unsigned long successfulConnects =
  0;

unsigned long disconnectCount =
  0;

unsigned long messageCount =
  0;

unsigned long triggerCount =
  0;


// ============================================================
// RECONNECT BACKOFF
// ============================================================

unsigned long lastReconnectAttempt =
  0;

unsigned long reconnectDelayMs =
  2000;

extern const unsigned long MAX_RECONNECT_DELAY =
  30000;


// ============================================================
// WIFI RECOVERY
// ============================================================

unsigned long lastWiFiReconnectAttempt =
  0;

extern const unsigned long WIFI_RECONNECT_INTERVAL =
  5000;


// ============================================================
// DISPLAY DIRTY FLAGS
// ============================================================

bool fullRedrawNeeded =
  true;

bool headerStatusDirty =
  true;

bool clockDirty =
  true;

bool dateDirty =
  true;

bool progressDirty =
  true;

bool timeDirty =
  true;

bool layerSpeedDirty =
  true;

bool nozzleDirty =
  true;

bool bedDirty =
  true;

bool chamberDirty =
  true;

bool idleConnectionDirty =
  true;

bool idleDiagnosticsDirty =
  true;

bool idleWeatherDirty =
  true;


// ============================================================
// DISPLAY CACHE
// ============================================================

String lastShownClock =
  "";

String lastShownDate =
  "";

bool lastShownWifi =
  false;

bool lastShownHA =
  false;

bool headerStateInitialized =
  false;


// ============================================================
// TIMERS
// ============================================================

unsigned long lastDisplayUpdate =
  0;

unsigned long lastTimePageSwitch =
  0;

unsigned long lastSerialStatus =
  0;

unsigned long lastIdleDiagnosticMinute =
  ULONG_MAX;


// ============================================================
// ETA / REMAINING
// ============================================================

bool showETA =
  true;

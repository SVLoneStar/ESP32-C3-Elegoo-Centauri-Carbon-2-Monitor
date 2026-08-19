#include "Display.h"
#include "Diagnostics.h"
#include "PrinterData.h"
#include "TimeHelpers.h"

// CENTER TEXT
// ============================================================

void drawCenteredText(
  const String& text,
  int baselineY,
  const GFXfont* font,
  uint16_t color
)
{
  tft.setFont(
    font
  );


  tft.setTextColor(
    color
  );


  int16_t x1;
  int16_t y1;

  uint16_t w;
  uint16_t h;


  tft.getTextBounds(
    text,
    0,
    baselineY,
    &x1,
    &y1,
    &w,
    &h
  );


  int x =
    (
      320 -
      w
    ) /
    2;


  if (x < 0)
    x = 0;


  tft.setCursor(
    x,
    baselineY
  );


  tft.print(
    text
  );

}

// ICONS
// ============================================================

void drawNozzleIcon(
  int x,
  int y,
  uint16_t color
)
{
  tft.fillRoundRect(
    x + 4,
    y,
    16,
    8,
    2,
    color
  );


  tft.fillRect(
    x + 8,
    y + 8,
    8,
    4,
    color
  );


  tft.fillTriangle(
    x + 6,
    y + 12,
    x + 18,
    y + 12,
    x + 12,
    y + 21,
    color
  );


  tft.drawFastVLine(
    x + 12,
    y + 21,
    2,
    color
  );
}


void drawBedIcon(
  int x,
  int y,
  uint16_t color
)
{
  tft.fillRoundRect(
    x,
    y + 11,
    26,
    5,
    2,
    color
  );


  tft.drawFastHLine(
    x + 3,
    y + 19,
    20,
    color
  );


  tft.drawFastVLine(
    x + 5,
    y + 16,
    6,
    color
  );


  tft.drawFastVLine(
    x + 21,
    y + 16,
    6,
    color
  );


  tft.drawLine(
    x + 5,
    y + 7,
    x + 8,
    y + 2,
    color
  );


  tft.drawLine(
    x + 12,
    y + 7,
    x + 15,
    y + 2,
    color
  );


  tft.drawLine(
    x + 19,
    y + 7,
    x + 22,
    y + 2,
    color
  );
}


void drawChamberIcon(
  int x,
  int y,
  uint16_t color
)
{
  tft.drawRoundRect(
    x + 2,
    y,
    24,
    24,
    3,
    color
  );


  tft.drawCircle(
    x + 14,
    y + 15,
    3,
    color
  );


  tft.fillCircle(
    x + 14,
    y + 15,
    1,
    color
  );


  tft.drawFastVLine(
    x + 14,
    y + 6,
    8,
    color
  );


  tft.drawFastHLine(
    x + 6,
    y + 20,
    16,
    color
  );
}


// ============================================================
// STATIC HEADER
// ============================================================

void drawStaticHeader()
{
  tft.setFont(
    &FreeSansBold9pt7b
  );


  tft.setTextColor(
    C_TEXT
  );


  tft.setCursor(
    8,
    22
  );


  tft.print(
    "CENT. CARBON 2"
  );


  tft.drawFastHLine(
    0,
    32,
    320,
    C_DIM
  );


  headerStatusDirty =
    true;


  clockDirty =
    true;
}


// ============================================================
// HEADER STATUS ONLY
// ============================================================

void updateHeaderStatus()
{
  bool wifi =
    WiFi.status() ==
    WL_CONNECTED;


  bool ha =
    wsConnected &&
    authenticated &&
    subscribed;


  if (
    !headerStateInitialized ||
    wifi != lastShownWifi
  )
  {
    tft.fillCircle(
      224,
      16,
      5,
      wifi
        ? C_GREEN
        : C_RED
    );


    lastShownWifi =
      wifi;
  }


  if (
    !headerStateInitialized ||
    ha != lastShownHA
  )
  {
    tft.fillCircle(
      242,
      16,
      5,
      ha
        ? C_CYAN
        : C_RED
    );


    lastShownHA =
      ha;


    idleConnectionDirty =
      true;
  }


  headerStateInitialized =
    true;


  headerStatusDirty =
    false;
}


// ============================================================
// HEADER CLOCK ONLY
// ============================================================

void updateHeaderClock()
{
  String now =
    getClock();


  if (
    now ==
    lastShownClock &&
    !clockDirty
  )
  {
    return;
  }


  tft.fillRect(
    258,
    2,
    62,
    27,
    C_BG
  );


  tft.setFont(
    &FreeSans9pt7b
  );


  tft.setTextColor(
    C_TEXT
  );


  tft.setCursor(
    263,
    21
  );


  tft.print(
    now
  );


  lastShownClock =
    now;


  clockDirty =
    false;
}


// ============================================================
// PRINTING STATIC LAYOUT
// ============================================================

void drawPrintingLayout()
{
  tft.fillScreen(
    C_BG
  );


  drawStaticHeader();


  tft.drawFastHLine(
    8,
    83,
    304,
    C_DIM
  );


  tft.drawFastHLine(
    8,
    151,
    304,
    C_DIM
  );


  tft.drawFastHLine(
    8,
    183,
    304,
    C_DIM
  );


  progressDirty =
    true;


  timeDirty =
    true;


  layerSpeedDirty =
    true;


  nozzleDirty =
    true;


  bedDirty =
    true;


  chamberDirty =
    true;
}


// ============================================================
// IDLE STATIC LAYOUT
// ============================================================

void drawIdleLayout()
{
  tft.fillScreen(
    C_BG
  );


  drawStaticHeader();


  drawCenteredText(
    "PRINTER IDLE",
    68,
    &FreeSansBold18pt7b,
    C_CYAN
  );


  tft.drawFastHLine(
    20,
    158,
    280,
    C_DIM
  );


  dateDirty =
    true;


  idleConnectionDirty =
    true;


  idleDiagnosticsDirty =
    true;


  lastIdleDiagnosticMinute =
    ULONG_MAX;
}


// ============================================================
// PROGRESS
// ============================================================

void updateProgress()
{
  float progress =
    printer.progress;


  const int barX =
    8;

  const int barY =
    43;

  const int barW =
    244;

  const int barH =
    30;


  tft.fillRoundRect(
    barX,
    barY,
    barW,
    barH,
    6,
    C_BAR_BG
  );


  if (!isnan(progress))
  {
    float p =
      constrain(
        progress,
        0.0f,
        100.0f
      );


    int fill =
      (barW - 4) *
      p /
      100.0f;


    if (fill > 0)
    {
      tft.fillRoundRect(
        barX + 2,
        barY + 2,
        fill,
        barH - 4,
        5,
        C_CYAN
      );
    }
  }


  tft.fillRect(
    258,
    39,
    62,
    40,
    C_BG
  );


  tft.setFont(
    &FreeSansBold18pt7b
  );


  tft.setTextColor(
    C_TEXT
  );


  tft.setCursor(
    260,
    70
  );


  if (isnan(progress))
  {
    tft.print(
      "--"
    );
  }
  else
  {
    tft.print(
      (int)round(progress)
    );
  }


  tft.setFont(
    &FreeSansBold12pt7b
  );


  tft.print(
    "%"
  );


  progressDirty =
    false;
}


// ============================================================
// ETA / REMAINING
// ============================================================

void updateTimeField()
{
  String value;

  String label;


  if (showETA)
  {
    value =
      calculateETA();


    label =
      "Finish at";
  }
  else
  {
    value =
      formatRemaining();


    label =
      "Remaining";
  }


  tft.fillRect(
    0,
    84,
    320,
    66,
    C_BG
  );


  tft.setFont(
    &FreeSansBold18pt7b
  );


  tft.setTextColor(
    C_TEXT
  );


  int16_t x1;
  int16_t y1;

  uint16_t w;
  uint16_t h;


  tft.getTextBounds(
    value,
    0,
    0,
    &x1,
    &y1,
    &w,
    &h
  );


  tft.setCursor(
    (
      320 -
      w
    ) /
    2,
    119
  );


  tft.print(
    value
  );


  tft.setFont(
    &FreeSans9pt7b
  );


  tft.setTextColor(
    C_DIM
  );


  tft.getTextBounds(
    label,
    0,
    0,
    &x1,
    &y1,
    &w,
    &h
  );


  tft.setCursor(
    (
      320 -
      w
    ) /
    2,
    143
  );


  tft.print(
    label
  );


  timeDirty =
    false;
}


// ============================================================
// LAYER + SPEED
// ============================================================

void updateLayerAndSpeed()
{
  tft.fillRect(
    0,
    152,
    320,
    30,
    C_BG
  );


  tft.setFont(
    &FreeSans9pt7b
  );


  tft.setTextColor(
    C_DIM
  );


  tft.setCursor(
    8,
    175
  );


  tft.print(
    "Layer"
  );


  tft.setFont(
    &FreeSansBold12pt7b
  );


  tft.setTextColor(
    C_TEXT
  );


  tft.setCursor(
    60,
    176
  );


  if (
    printer.currentLayer >=
    0
  )
  {
    tft.print(
      printer.currentLayer
    );
  }
  else
  {
    tft.print(
      "-"
    );
  }


  tft.print(
    " / "
  );


  if (
    printer.totalLayers >=
    0
  )
  {
    tft.print(
      printer.totalLayers
    );
  }
  else
  {
    tft.print(
      "-"
    );
  }


  tft.setFont(
    &FreeSans9pt7b
  );


  tft.setTextColor(
    C_DIM
  );


  tft.setCursor(
    225,
    175
  );


  tft.print(
    "Speed"
  );


  tft.setFont(
    &FreeSansBold12pt7b
  );


  tft.setTextColor(
    C_CYAN
  );


  tft.setCursor(
    280,
    176
  );


  tft.print(
    speedMode(
      printer.printSpeed
    )
  );


  layerSpeedDirty =
    false;
}


// ============================================================
// TEMPERATURE FIELD
// ============================================================

void drawTemperatureField(
  int x,
  int iconType,
  float value,
  uint16_t color
)
{
  tft.fillRect(
    x,
    187,
    101,
    51,
    C_BG
  );


  if (
    iconType ==
    0
  )
  {
    drawNozzleIcon(
      x + 3,
      211,
      color
    );
  }
  else if (
    iconType ==
    1
  )
  {
    drawBedIcon(
      x + 2,
      207,
      color
    );
  }
  else
  {
    drawChamberIcon(
      x + 1,
      210,
      color
    );
  }


  tft.setFont(
    &FreeSansBold12pt7b
  );


  tft.setTextColor(
    color
  );


  tft.setCursor(
    x + 34,
    231
  );


  if (isnan(value))
  {
    tft.print(
      "--"
    );
  }
  else
  {
    tft.print(
      (int)round(value)
    );
  }


  tft.setFont(
    &FreeSans9pt7b
  );


  tft.setTextColor(
    C_GREY
  );


  tft.print(
    (char)247
  );


  tft.print(
    "C"
  );
}


// ============================================================
// IDLE DATE
// ============================================================

void updateIdleDate()
{
  String date =
    getLongDate();


  if (
    date ==
      lastShownDate &&
    !dateDirty
  )
  {
    return;
  }


  tft.fillRect(
    0,
    75,
    320,
    37,
    C_BG
  );


  drawCenteredText(
    date,
    101,
    &FreeSans9pt7b,
    C_TEXT
  );


  lastShownDate =
    date;


  dateDirty =
    false;
}


// ============================================================
// IDLE LARGE CLOCK
// ============================================================

void updateIdleLargeClock()
{
  String now =
    getClock();


  tft.fillRect(
    0,
    111,
    320,
    43,
    C_BG
  );


  drawCenteredText(
    now,
    143,
    &FreeSansBold18pt7b,
    C_TEXT
  );
}


// ============================================================
// IDLE HA STATUS
// ============================================================

void updateIdleConnection()
{
  bool haOK =
    wsConnected &&
    authenticated &&
    subscribed;


  tft.fillRect(
    0,
    162,
    320,
    27,
    C_BG
  );


  drawCenteredText(
    haOK
      ? "HOME ASSISTANT CONNECTED"
      : "HOME ASSISTANT OFFLINE",
    182,
    &FreeSansBold9pt7b,
    haOK
      ? C_GREEN
      : C_RED
  );


  idleConnectionDirty =
    false;
}


// ============================================================
// IDLE DIAGNOSTICS
// ============================================================

void updateIdleDiagnostics()
{
  tft.fillRect(
    0,
    191,
    320,
    48,
    C_BG
  );


  String line1 =
    "Boots ";

  line1 +=
    String(
      bootCount
    );


  line1 +=
    "   Uptime ";

  line1 +=
    getUptimeString();


  drawCenteredText(
    line1,
    210,
    &FreeSans9pt7b,
    C_DIM
  );


  String line2 =
    "Last reset: ";

  line2 +=
    resetReasonText(
      lastResetReason
    );


  drawCenteredText(
    line2,
    233,
    &FreeSans9pt7b,
    lastResetReason ==
      ESP_RST_PANIC
      ? C_RED
      : C_DIM
  );


  idleDiagnosticsDirty =
    false;
}


// ============================================================
// CHECK CLOCK / DATE CHANGES
// ============================================================

void checkTimeChanges()
{
  String clockNow =
    getClock();


  if (
    clockNow !=
    lastShownClock
  )
  {
    clockDirty =
      true;


    if (
      currentDisplayMode ==
      MODE_IDLE
    )
    {
      /*
        Large idle clock uses same HH:MM,
        therefore redraw once per minute too.
      */

      // handled below by clockDirty
    }


    /*
      ETA changes with current time even if remaining sensor
      has not changed.
    */

    if (
      currentDisplayMode ==
      MODE_PRINTING &&
      showETA
    )
    {
      timeDirty =
        true;
    }
  }


  if (
    currentDisplayMode ==
    MODE_IDLE
  )
  {
    String dateNow =
      getLongDate();


    if (
      dateNow !=
      lastShownDate
    )
    {
      dateDirty =
        true;
    }


    unsigned long currentMinute =
      millis() /
      60000UL;


    if (
      currentMinute !=
      lastIdleDiagnosticMinute
    )
    {
      lastIdleDiagnosticMinute =
        currentMinute;


      idleDiagnosticsDirty =
        true;
    }
  }
}


// ============================================================
// DISPLAY UPDATE
// ============================================================

void updateDisplay()
{
  // ----------------------------------------------------------
  // MODE
  // ----------------------------------------------------------

  DisplayMode wantedMode =
    printerIsPrinting()
      ? MODE_PRINTING
      : MODE_IDLE;


  if (
    wantedMode !=
    currentDisplayMode
  )
  {
    currentDisplayMode =
      wantedMode;


    fullRedrawNeeded =
      true;


    if (
      currentDisplayMode ==
      MODE_PRINTING
    )
    {
      showETA =
        true;


      lastTimePageSwitch =
        millis();
    }
  }


  // ----------------------------------------------------------
  // FULL REDRAW ONLY ON MODE CHANGE
  // ----------------------------------------------------------

  if (
    fullRedrawNeeded
  )
  {
    headerStateInitialized =
      false;


    lastShownClock =
      "";


    if (
      currentDisplayMode ==
      MODE_PRINTING
    )
    {
      drawPrintingLayout();
    }
    else
    {
      drawIdleLayout();
    }


    fullRedrawNeeded =
      false;
  }


  // ----------------------------------------------------------
  // DETECT TIME CHANGES
  // ----------------------------------------------------------

  checkTimeChanges();


  // ----------------------------------------------------------
  // HEADER
  // ----------------------------------------------------------

  if (
    headerStatusDirty
  )
  {
    updateHeaderStatus();
  }


  if (
    clockDirty
  )
  {
    updateHeaderClock();


    if (
      currentDisplayMode ==
      MODE_IDLE
    )
    {
      updateIdleLargeClock();
    }
  }


  // ----------------------------------------------------------
  // PRINTING
  // ----------------------------------------------------------

  if (
    currentDisplayMode ==
    MODE_PRINTING
  )
  {
    if (
      millis() -
        lastTimePageSwitch >=
        TIME_PAGE_MS
    )
    {
      lastTimePageSwitch =
        millis();


      showETA =
        !showETA;


      timeDirty =
        true;
    }


    if (
      progressDirty
    )
    {
      updateProgress();
    }


    if (
      timeDirty
    )
    {
      updateTimeField();
    }


    if (
      layerSpeedDirty
    )
    {
      updateLayerAndSpeed();
    }


    if (
      nozzleDirty
    )
    {
      drawTemperatureField(
        4,
        0,
        printer.nozzleTemp,
        C_RED
      );


      nozzleDirty =
        false;
    }


    if (
      bedDirty
    )
    {
      drawTemperatureField(
        109,
        1,
        printer.bedTemp,
        C_ORANGE
      );


      bedDirty =
        false;
    }


    if (
      chamberDirty
    )
    {
      drawTemperatureField(
        214,
        2,
        printer.boxTemp,
        C_GREEN
      );


      chamberDirty =
        false;
    }


    return;
  }


  // ----------------------------------------------------------
  // IDLE
  // ----------------------------------------------------------

  if (
    dateDirty
  )
  {
    updateIdleDate();
  }


  if (
    idleConnectionDirty
  )
  {
    updateIdleConnection();
  }


  if (
    idleDiagnosticsDirty
  )
  {
    updateIdleDiagnostics();
  }
}

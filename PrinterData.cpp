#include "PrinterData.h"

// GENERIC HELPERS
// ============================================================

bool invalidState(
  String value
)
{
  value.trim();
  value.toLowerCase();


  return
    value.length() == 0 ||
    value == "unknown" ||
    value == "unavailable" ||
    value == "none" ||
    value == "null";
}


float stateToFloat(
  const String& state
)
{
  if (invalidState(state))
    return NAN;


  return
    state.toFloat();
}


int stateToInt(
  const String& state
)
{
  if (invalidState(state))
    return -1;


  return
    state.toInt();
}


// ============================================================
// PRINTER INIT
// ============================================================

void initializePrinter()
{
  printer.currentStatus =
    "";

  printer.printStatus =
    "";

  printer.remainingTime =
    "";

  printer.printError =
    "";

  printer.errorReason =
    "";

  printer.progress =
    NAN;

  printer.currentLayer =
    -1;

  printer.totalLayers =
    -1;

  printer.nozzleTemp =
    NAN;

  printer.bedTemp =
    NAN;

  printer.boxTemp =
    NAN;

  printer.printSpeed =
    NAN;
}


// ============================================================
// PRINTING DETECTION
// ============================================================

bool printerIsPrinting()
{
  String status =
    printer.currentStatus +
    " " +
    printer.printStatus;


  status.toLowerCase();


  if (
    status.indexOf("print") >= 0 ||
    status.indexOf("druck") >= 0 ||
    status.indexOf("working") >= 0
  )
  {
    return true;
  }


  if (
    !isnan(printer.progress) &&
    printer.progress > 0.0f &&
    printer.progress < 100.0f
  )
  {
    return true;
  }


  return false;

}

// REMAINING
// ============================================================

int getRemainingMinutes()
{
  if (
    invalidState(
      printer.remainingTime
    )
  )
  {
    return -1;
  }


  float value =
    printer.remainingTime.toFloat();


  if (value < 0)
    value = 0;


  return
    (int)round(value);
}


String formatRemaining()
{
  int total =
    getRemainingMinutes();


  if (total < 0)
    return "--";


  int hours =
    total /
    60;


  int minutes =
    total %
    60;


  if (hours > 0)
  {
    char buffer[16];


    snprintf(
      buffer,
      sizeof(buffer),
      "%dh %02dm",
      hours,
      minutes
    );


    return String(buffer);
  }


  return
    String(minutes) +
    " min";
}


// ============================================================
// ETA
// ============================================================

String calculateETA()
{
  int remaining =
    getRemainingMinutes();


  if (remaining < 0)
    return "--:--";


  time_t now =
    time(nullptr);


  if (now < 1000000000UL)
    return "--:--";


  time_t eta =
    now +
    (
      (time_t)remaining *
      60
    );


  struct tm t;


  if (!localtime_r(
        &eta,
        &t
      ))
  {
    return "--:--";
  }


  char buffer[6];


  snprintf(
    buffer,
    sizeof(buffer),
    "%02d:%02d",
    t.tm_hour,
    t.tm_min
  );


  return String(buffer);
}


// ============================================================
// SPEED MODE
// ============================================================

String speedMode(
  float value
)
{
  if (isnan(value))
    return "--";


  int speed =
    (int)round(value);


  if (speed == 50)
    return "SI";


  if (speed == 100)
    return "BA";


  if (speed == 150)
    return "SP";


  if (speed == 200)
    return "LU";


  return String(speed);

}

// UPDATE PRINTER ENTITY
// ============================================================

void updatePrinterEntity(
  const String& entity,
  const String& state
)
{
  if (
    entity ==
    ENTITY_CURRENT_STATUS
  )
  {
    if (
      printer.currentStatus !=
      state
    )
    {
      printer.currentStatus =
        state;


      fullRedrawNeeded =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_PRINT_STATUS
  )
  {
    if (
      printer.printStatus !=
      state
    )
    {
      printer.printStatus =
        state;


      fullRedrawNeeded =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_REMAINING
  )
  {
    if (
      printer.remainingTime !=
      state
    )
    {
      printer.remainingTime =
        state;


      timeDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_PROGRESS
  )
  {
    float value =
      stateToFloat(
        state
      );


    if (
      isnan(value) !=
        isnan(printer.progress) ||
      (
        !isnan(value) &&
        fabs(
          value -
          printer.progress
        ) >
          0.01f
      )
    )
    {
      printer.progress =
        value;


      progressDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_CURRENT_LAYER
  )
  {
    int value =
      stateToInt(
        state
      );


    if (
      value !=
      printer.currentLayer
    )
    {
      printer.currentLayer =
        value;


      layerSpeedDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_TOTAL_LAYERS
  )
  {
    int value =
      stateToInt(
        state
      );


    if (
      value !=
      printer.totalLayers
    )
    {
      printer.totalLayers =
        value;


      layerSpeedDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_NOZZLE_TEMP
  )
  {
    float value =
      stateToFloat(
        state
      );


    if (
      isnan(value) !=
        isnan(printer.nozzleTemp) ||
      (
        !isnan(value) &&
        fabs(
          value -
          printer.nozzleTemp
        ) >=
          0.5f
      )
    )
    {
      printer.nozzleTemp =
        value;


      nozzleDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_BED_TEMP
  )
  {
    float value =
      stateToFloat(
        state
      );


    if (
      isnan(value) !=
        isnan(printer.bedTemp) ||
      (
        !isnan(value) &&
        fabs(
          value -
          printer.bedTemp
        ) >=
          0.5f
      )
    )
    {
      printer.bedTemp =
        value;


      bedDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_BOX_TEMP
  )
  {
    float value =
      stateToFloat(
        state
      );


    if (
      isnan(value) !=
        isnan(printer.boxTemp) ||
      (
        !isnan(value) &&
        fabs(
          value -
          printer.boxTemp
        ) >=
          0.5f
      )
    )
    {
      printer.boxTemp =
        value;


      chamberDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_PRINT_SPEED
  )
  {
    float value =
      stateToFloat(
        state
      );


    if (
      isnan(value) !=
        isnan(printer.printSpeed) ||
      (
        !isnan(value) &&
        fabs(
          value -
          printer.printSpeed
        ) >
          0.01f
      )
    )
    {
      printer.printSpeed =
        value;


      layerSpeedDirty =
        true;
    }
  }


  else if (
    entity ==
    ENTITY_PRINT_ERROR
  )
  {
    printer.printError =
      state;
  }


  else if (
    entity ==
    ENTITY_ERROR_REASON
  )
  {
    printer.errorReason =
      state;
  }
}

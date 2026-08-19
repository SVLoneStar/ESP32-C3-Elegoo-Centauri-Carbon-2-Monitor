#include "TimeHelpers.h"

// NTP
// ============================================================

void startNTP()
{
  configTzTime(
    "CET-1CEST,M3.5.0,M10.5.0/3",
    "pool.ntp.org",
    "time.nist.gov"
  );
}


// ============================================================
// CLOCK
// ============================================================

String getClock()
{
  time_t now =
    time(nullptr);


  if (now < 1000000000UL)
    return "--:--";


  struct tm t;


  if (!localtime_r(
        &now,
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
// DATE
// ============================================================

String getLongDate()
{
  time_t now =
    time(nullptr);


  if (now < 1000000000UL)
    return "Date not synchronized";


  struct tm t;


  if (!localtime_r(
        &now,
        &t
      ))
  {
    return "Invalid date";
  }


  const char* weekdays[] =
  {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday"
  };


  const char* months[] =
  {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
  };


  String result;


  result +=
    weekdays[t.tm_wday];

  result +=
    ", ";

  result +=
    months[t.tm_mon];

  result +=
    " ";

  result +=
    String(t.tm_mday);

  result +=
    ", ";

  result +=
    String(t.tm_year + 1900);


  return result;
}

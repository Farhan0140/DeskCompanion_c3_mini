#pragma once
// =======================================================================
// SECRETS — WiFi fallback + NTP. Keep this file out of git (.gitignore it).
// =======================================================================

#define WIFI_SSID       " "
#define WIFI_PASSWORD   "`         `"

// NTP
#define NTP_SERVER_1    "pool.ntp.org"
#define NTP_SERVER_2    "time.google.com"
// Bangladesh Standard Time = UTC+6, no daylight saving
#define GMT_OFFSET_SEC  (6 * 3600)
#define DST_OFFSET_SEC  0

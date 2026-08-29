#pragma once
// =======================================================================
// SECRETS — WiFi + Firebase. Keep this file out of git (.gitignore it).
// =======================================================================

#define WIFI_SSID       " "
#define WIFI_PASSWORD   "`         `"

// Your Firebase Realtime Database base URL (no trailing slash).
#define FIREBASE_HOST   "https://deskcompanionc3-default-rtdb.asia-southeast1.firebasedatabase.app"

// Optional: Firebase legacy "database secret" or a token minted for a
// service account / signed-in user. Leave empty ("") ONLY if your RTDB
// rules are set to public test-mode read/write — fine for prototyping,
// NOT fine to leave that way permanently. See README "Firebase security".
#define FIREBASE_AUTH   ""

// NTP
#define NTP_SERVER_1    "pool.ntp.org"
#define NTP_SERVER_2    "time.google.com"
// Bangladesh Standard Time = UTC+6, no daylight saving
#define GMT_OFFSET_SEC  (6 * 3600)
#define DST_OFFSET_SEC  0

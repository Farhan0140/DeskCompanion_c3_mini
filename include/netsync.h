#pragma once
// =======================================================================
// NETSYNC MODULE — declarations only; see netsync.cpp.
//
// netInit() is the only thing the main loop needs to call: it starts
// a background FreeRTOS task that owns WiFi/NTP/server-health-check
// entirely from then on. There is no netUpdate() to call from loop() —
// that's the point: networking (including the potentially slow HTTPS
// health check) never blocks button/IR polling.
// =======================================================================
#include <Arduino.h>

void netInit();

// Useful anywhere you want a real formatted timestamp (returns false
// if NTP hasn't synced yet or WiFi is down). Safe to call from the
// main loop even though NTP itself is serviced by the background task.
bool getLocalTimeStr(char *out, size_t outLen);

// Call once the WiFi setup portal (see wifi_portal.h) has just saved
// fresh credentials to NVS — reloads them and immediately (re)tries
// WiFi.begin() with the new SSID/password instead of waiting out the
// normal 15s retry timer on the old ones. Safe to call from the main loop.
void netRequestReconnect();

// True once netsync has an active WiFi connection. Safe to poll from
// the main loop (e.g. the WiFi Setup menu screen).
bool netIsWifiConnected();

// Result of the most recent server health check (HEALTH_URL in
// config.h). false until the first check has succeeded.
bool netIsServerOk();

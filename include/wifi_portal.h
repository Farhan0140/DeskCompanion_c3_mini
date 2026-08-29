#pragma once
// =======================================================================
// WIFI PORTAL MODULE — lets the user pick their WiFi from their phone
// instead of hardcoding SSID/password in secrets.h. Selecting "WiFi
// Setup" from the menu spins up the ESP32's own access point plus a
// small setup web page (served entirely from flash, no internet
// needed); the phone scans, taps a nearby network, enters the
// password, and the device saves it to NVS and reconnects as a
// normal WiFi client using those credentials from then on (including
// after a reset — see wifiCredsLoad(), called from netsync.cpp).
//
// Runs entirely on core 1, pumped from main.cpp's loop() only while
// active. netsync.cpp's background task (core 0) checks
// wifiPortalActive() and stays off the radio while the portal is up.
// =======================================================================
#include <Arduino.h>
#include <IPAddress.h>

// Loads previously-saved credentials into the given buffers. Returns
// false (buffers left untouched) if the device has never been
// configured via the portal yet.
bool wifiCredsLoad(char *ssid, size_t ssidLen, char *pass, size_t passLen);

void wifiPortalStart();   // begins the AP + web/DNS servers
void wifiPortalStop();    // tears the AP + servers back down
void wifiPortalUpdate();  // pump every loop() tick — no-ops while inactive
bool wifiPortalActive();

IPAddress   wifiPortalIP();      // the AP's own IP, valid only while active
const char* wifiPortalApSsid();  // the ESP32's own hotspot name, for the on-screen instructions

// True exactly once, right after the phone submits new credentials
// (already saved to NVS by then). The menu reads this once to stop
// the portal and kick off a reconnect (see netRequestReconnect() in
// netsync.h), then must call wifiPortalClearCredsReady().
bool wifiPortalCredsReady();
void wifiPortalClearCredsReady();

#pragma once
// =======================================================================
// NETSYNC MODULE — declarations only; see netsync.cpp.
//
// netInit() is the only thing the main loop needs to call: it starts
// a background FreeRTOS task (pinned to core 0) that owns WiFi/NTP/
// Firebase entirely from then on. There is no netUpdate() to call
// from loop() anymore — that's the point: networking (including the
// potentially slow Firebase HTTPS calls) never runs on the same core
// as button/IR polling, so it can't stall UI responsiveness.
// =======================================================================
#include <Arduino.h>

void netInit();

// Useful anywhere you want a real formatted timestamp (returns false
// if NTP hasn't synced yet or WiFi is down). Safe to call from the
// main loop even though NTP itself is serviced by the background task.
bool getLocalTimeStr(char *out, size_t outLen);

// Call whenever D.lightMode/D.fanMode changes locally (IR remote,
// menu, etc.) so the background net task pushes the new mode to
// Firebase's /deskbuddy/light_mode and /deskbuddy/fan_mode nodes on
// its very next tick — before its next periodic pull can read back
// the now-stale value it still has and stomp the local change. Safe
// to call from core 1 (main loop); just sets a flag the core-0 task
// polls.
void netMarkModeDirty();
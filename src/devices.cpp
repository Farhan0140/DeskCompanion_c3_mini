#include <Arduino.h>
#include "devices.h"
#include "config.h"
#include "settings.h"
#include "sensors.h"
#include "face.h"
#include "buzzer.h"
#include "globals.h"
#include "netsync.h"

DeviceState D;

static inline void relayWrite(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

// Relay modes (AUTO/ON/OFF) are persisted to NVS so a change made via
// the IR remote (or a menu, or Firebase) survives a reset/power cycle
// instead of always coming back up in AUTO. `prefs` is the shared NVS
// handle (see globals.h) also used by timer_task.cpp for the task
// list — begin() is safe to call again there since it's a no-op once
// already started on the same namespace.
void devicesInit() {
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  pinMode(PIN_RELAY_FAN, OUTPUT);
  relayWrite(PIN_RELAY_LIGHT, false);
  relayWrite(PIN_RELAY_FAN, false);

  prefs.begin("deskbuddy", false);
  D.lightMode = (DeviceMode)prefs.getUChar("light_mode", MODE_AUTO);
  D.fanMode   = (DeviceMode)prefs.getUChar("fan_mode", MODE_AUTO);
}

void setLightMode(DeviceMode m) {
  if (D.lightMode == m) return;
  D.lightMode = m;
  prefs.putUChar("light_mode", (uint8_t)m);
  netMarkModeDirty(); // push the new mode to Firebase before its next pull can read the stale one back
}

void setFanMode(DeviceMode m) {
  if (D.fanMode == m) return;
  D.fanMode = m;
  prefs.putUChar("fan_mode", (uint8_t)m);
  netMarkModeDirty();
}

void devicesUpdate(unsigned long now) {
  // ---- Debug: print the full decision chain once a second so it's
  // possible to see exactly where things break (bad wiring, wrong
  // RELAY_ACTIVE_LOW polarity, mode stuck on OFF, sensor never
  // reporting presence, etc). Safe to delete once everything works.
  static unsigned long lastDebug = 0;
  if (now - lastDebug >= 1000) {
    lastDebug = now;
    Serial.printf(
      "[DEBUG] dist=%.1fcm presence=%d | lightMode=%d lightOn=%d | fanMode=%d fanOn=%d AUTO_FAN_ENABLED=%d\n",
      S.distanceCm, S.presence,
      (int)D.lightMode, D.lightOn,
      (int)D.fanMode, D.fanOn, AUTO_FAN_ENABLED);
  }

  // ---- Light ----
  bool wantLight;
  switch (D.lightMode) {
    case MODE_ON:  wantLight = true; break;
    case MODE_OFF: wantLight = false; break;
    default: // AUTO
      wantLight = AUTO_LIGHT_ENABLED && S.presence && S.isDark;
      break;
  }
  if (wantLight != D.lightOn) {
    D.lightOn = wantLight;
    relayWrite(PIN_RELAY_LIGHT, D.lightOn);
    buzzerClick();
    faceShowEvent(D.lightOn ? FACE_EVENT_LIGHT_ON : FACE_EVENT_LIGHT_OFF);
  }

  // ---- Fan ----
  bool wantFan;
  switch (D.fanMode) {
    case MODE_ON:  wantFan = true; break;
    case MODE_OFF: wantFan = false; break;
    default: // AUTO
      wantFan = AUTO_FAN_ENABLED && S.presence;
      break;
  }
  if (wantFan != D.fanOn) {
    D.fanOn = wantFan;
    relayWrite(PIN_RELAY_FAN, D.fanOn);
    buzzerClick();
    faceShowEvent(D.fanOn ? FACE_EVENT_FAN_ON : FACE_EVENT_FAN_OFF);
  }
}
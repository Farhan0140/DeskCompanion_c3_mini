#include <Arduino.h>
#include "devices.h"
#include "config.h"
#include "settings.h"
#include "sensors.h"
#include "face.h"
#include "buzzer.h"

DeviceState D;

static inline void relayWrite(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void devicesInit() {
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  pinMode(PIN_RELAY_FAN, OUTPUT);
  relayWrite(PIN_RELAY_LIGHT, false);
  relayWrite(PIN_RELAY_FAN, false);
}

void setLightMode(DeviceMode m) { D.lightMode = m; }
void setFanMode(DeviceMode m)   { D.fanMode = m; }

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
  }
}
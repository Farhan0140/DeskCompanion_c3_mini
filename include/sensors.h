#pragma once
// =======================================================================
// SENSORS MODULE — Ultrasonic (HC-SR04) presence + LDR ambient light.
// Declarations only; see sensors.cpp for implementation.
// =======================================================================
#include <Arduino.h>

struct SensorState {
  float distanceCm = 999;
  float distanceMinCm = 999;       // minimum distance reading recorded
  float distanceMaxCm = 0;         // maximum distance reading recorded
  bool  presence = false;          // after hysteresis
  unsigned long lastUsPoll = 0;

  int   ldrRaw = 4095;
  bool  isDark = false;
  unsigned long lastLdrPoll = 0;
};

extern SensorState S;

void sensorsInit();
void sensorsUpdate(unsigned long now);

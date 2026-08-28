#pragma once
// =======================================================================
// DEVICES MODULE — Relay 1 = Light, Relay 2 = Fan.
// Declarations only; see devices.cpp for implementation.
// =======================================================================
#include <Arduino.h>

enum DeviceMode { MODE_OFF, MODE_ON, MODE_AUTO };

struct DeviceState {
  DeviceMode lightMode = MODE_AUTO;
  DeviceMode fanMode   = MODE_AUTO;
  bool lightOn = false;
  bool fanOn = false;
};

extern DeviceState D;

void devicesInit();
void devicesUpdate(unsigned long now);
void setLightMode(DeviceMode m);
void setFanMode(DeviceMode m);
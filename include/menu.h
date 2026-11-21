#pragma once
// =======================================================================
// MENU MODULE — declarations only; see menu.cpp.
// =======================================================================
#include <Arduino.h>
#include "config.h"

enum Screen {
  SCR_IDLE,
  SCR_ROOT,
  SCR_DEVICES, SCR_LIGHT, SCR_FAN,
  SCR_SENSORS,
  SCR_THRESH, SCR_THRESH_US, SCR_THRESH_LDR,
  SCR_WIFI,
};

struct MenuState {
  Screen screen = SCR_IDLE;
  int cursor = 0;
  uint8_t setField = 0; // 0=ON/dark threshold, 1=OFF/bright threshold (threshold edit screens)
};
extern MenuState M;

void menuInit();
void menuUpdate(unsigned long now);
void showBootScreen();
void renderFrame(unsigned long now);

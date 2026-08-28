#pragma once
// =======================================================================
// MENU MODULE — declarations only; see menu.cpp.
// =======================================================================
#include <Arduino.h>
#include "config.h"

enum Screen {
  SCR_IDLE,
  SCR_ROOT,
  SCR_TIMER, SCR_TIMER_SET,
  SCR_TASKS, SCR_TASK_LIST, SCR_TASK_ADD,
  SCR_DEVICES, SCR_LIGHT, SCR_FAN,
  SCR_SENSORS,
};

struct MenuState {
  Screen screen = SCR_IDLE;
  int cursor = 0;
  uint8_t setField = 0; // 0=hours,1=minutes,2=seconds (timer set screen)
};
extern MenuState M;

void menuInit();
void menuUpdate(unsigned long now);
void showBootScreen();
void renderFrame(unsigned long now);

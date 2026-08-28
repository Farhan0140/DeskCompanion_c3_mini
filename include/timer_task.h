#pragma once
// =======================================================================
// TIMER + TASK MODULE — declarations only; see timer_task.cpp.
// =======================================================================
#include <Arduino.h>
#include <Preferences.h>

// ---- Timer ----
struct TimerState {
  bool running = false;
  bool finished = false;
  unsigned long endsAtMs = 0;
  unsigned long remainingMs = 0; // used while paused/being set
  int setHours = 0, setMinutes = 0, setSeconds = 30;
};
extern TimerState T;

void timerStart();
void timerCancel();
unsigned long timerRemainingMs();

// ---- Tasks ----
#define MAX_TASKS 10
struct Task { char name[24]; bool done; };
extern Task g_tasks[MAX_TASKS];
extern uint8_t g_taskCount;

bool taskAdd(const char *name);
void taskToggleDone(uint8_t idx);

// Call once at boot with a pointer to the shared Preferences object.
void timerTaskInit(Preferences *p);
void timerTaskUpdate(unsigned long now);

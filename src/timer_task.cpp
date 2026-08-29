#include <Arduino.h>
#include "timer_task.h"
#include "config.h"
#include "face.h"
#include "buzzer.h"

TimerState T;

void timerStart() {
  unsigned long totalMs = ((unsigned long)T.setHours * 3600UL +
                            (unsigned long)T.setMinutes * 60UL +
                            (unsigned long)T.setSeconds) * 1000UL;
  if (totalMs == 0) { buzzerError(); return; }
  T.endsAtMs = millis() + totalMs;
  T.running = true;
  T.finished = false;
}

void timerCancel() {
  T.running = false;
  T.finished = false;
  buzzerStop();
}

unsigned long timerRemainingMs() {
  if (!T.running) return 0;
  long rem = (long)(T.endsAtMs - millis());
  return rem > 0 ? (unsigned long)rem : 0;
}

// ---- Tasks (max 10, persisted in NVS as a delimited string) ----
Task g_tasks[MAX_TASKS];
uint8_t g_taskCount = 0;
static Preferences *g_prefs = nullptr;

static void tasksSave() {
  if (!g_prefs) return;
  String blob = "";
  for (uint8_t i = 0; i < g_taskCount; i++) {
    blob += g_tasks[i].name;
    blob += "|";
    blob += (g_tasks[i].done ? "1" : "0");
    blob += ";";
  }
  g_prefs->putString("tasks", blob);
}

static void tasksLoad() {
  if (!g_prefs) return;
  String blob = g_prefs->getString("tasks", "");
  g_taskCount = 0;
  int start = 0;
  while (start < (int)blob.length() && g_taskCount < MAX_TASKS) {
    int semi = blob.indexOf(';', start);
    if (semi < 0) break;
    String entry = blob.substring(start, semi);
    int bar = entry.indexOf('|');
    if (bar > 0) {
      entry.substring(0, bar).toCharArray(g_tasks[g_taskCount].name, 24);
      g_tasks[g_taskCount].done = entry.substring(bar + 1) == "1";
      g_taskCount++;
    }
    start = semi + 1;
  }
}

bool taskAdd(const char *name) {
  if (g_taskCount >= MAX_TASKS) return false;
  strncpy(g_tasks[g_taskCount].name, name, 23);
  g_tasks[g_taskCount].name[23] = '\0';
  g_tasks[g_taskCount].done = false;
  g_taskCount++;
  tasksSave();
  return true;
}

void taskToggleDone(uint8_t idx) {
  if (idx >= g_taskCount) return;
  g_tasks[idx].done = !g_tasks[idx].done;
  tasksSave();
  if (g_tasks[idx].done) {
    faceSetExpression(EYE_HAPPY, MOUTH_HAPPY);
    buzzerTaskDone();
  }
}

void timerTaskInit(Preferences *p) {
  g_prefs = p;
  g_prefs->begin("deskbuddy", false);
  tasksLoad();
}

void timerTaskUpdate(unsigned long now) {
  (void)now;
  if (T.running && !T.finished) {
    if (timerRemainingMs() == 0) {
      T.finished = true;
      T.running = false;
      faceShowEvent(FACE_EVENT_TIMER_RING);
      buzzerTimerAlarm();
      Serial.println(F("[TIMER] Complete"));
    }
  }
}

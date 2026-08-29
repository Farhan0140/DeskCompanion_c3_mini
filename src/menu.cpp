#include <Arduino.h>
#include "menu.h"
#include "config.h"
#include "globals.h"
#include "face.h"
#include "sensors.h"
#include "devices.h"
#include "inputs.h"
#include "buzzer.h"
#include "timer_task.h"
#include "netsync.h"
#include "wifi_portal.h"

MenuState M;

static const char* ROOT_ITEMS[] = {"Timer", "Tasks", "Devices", "Sensors", "WiFi Setup"};
static const int ROOT_COUNT = 5;

// Set the moment SCR_WIFI first observes a live connection; menuUpdate()
// holds the "Connected!" screen briefly, then auto-returns to the root
// menu. 0 means "not connected yet" (also reset whenever the screen is
// freshly opened).
static unsigned long g_wifiConnectedAt = 0;
static constexpr unsigned long WIFI_CONNECTED_HOLD_MS = 1200;

void menuInit() { M.screen = SCR_IDLE; }

static void gotoIdle() {
  M.screen = SCR_IDLE;
  g_menuActive = false;
}

static void gotoRoot() {
  M.screen = SCR_ROOT;
  M.cursor = 0;
  g_menuActive = true;
  faceSetExpression(EYE_NORMAL, MOUTH_NEUTRAL);
  faceSetGaze(GAZE_CENTER);
}

// ---- Input handling per screen ----
static void handleRoot(InputEvent ev) {
  if (ev == EV_UP)   M.cursor = (M.cursor + ROOT_COUNT - 1) % ROOT_COUNT;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % ROOT_COUNT;
  if (ev == EV_BACK) gotoIdle();
  if (ev == EV_OK) {
    switch (M.cursor) {
      case 0: M.screen = SCR_TIMER; M.cursor = 0; break;
      case 1: M.screen = SCR_TASKS; M.cursor = 0; break;
      case 2: M.screen = SCR_DEVICES; M.cursor = 0; break;
      case 3: M.screen = SCR_SENSORS; break;
      case 4: M.screen = SCR_WIFI; wifiPortalStart(); g_wifiConnectedAt = 0; break;
    }
    buzzerOk();
  }
}

static void handleTimer(InputEvent ev) {
  const int N = 3; // Start, Set Time, Cancel
  if (ev == EV_UP)   M.cursor = (M.cursor + N - 1) % N;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % N;
  if (ev == EV_BACK) { M.screen = SCR_ROOT; M.cursor = 0; }
  if (ev == EV_OK) {
    if (M.cursor == 0) { timerStart(); buzzerOk(); gotoIdle(); }
    else if (M.cursor == 1) { M.screen = SCR_TIMER_SET; M.setField = 0; }
    else { timerCancel(); buzzerOk(); }
  }
}

static void handleTimerSet(InputEvent ev) {
  int *fields[3] = {&T.setHours, &T.setMinutes, &T.setSeconds};
  int maxVal[3] = {min(TIMER_MAX_MINUTES/60, 23), 59, 59};
  if (ev == EV_UP)   *fields[M.setField] = (*fields[M.setField] + 1) % (maxVal[M.setField] + 1);
  if (ev == EV_DOWN) *fields[M.setField] = (*fields[M.setField] + maxVal[M.setField]) % (maxVal[M.setField] + 1);
  if (ev == EV_OK)   M.setField = (M.setField + 1) % 3;
  if (ev == EV_BACK) M.screen = SCR_TIMER;
}

static void handleTasks(InputEvent ev) {
  const int N = 2; // Task List, Add Task
  if (ev == EV_UP)   M.cursor = (M.cursor + N - 1) % N;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % N;
  if (ev == EV_BACK) { M.screen = SCR_ROOT; M.cursor = 1; }
  if (ev == EV_OK) {
    if (M.cursor == 0) { M.screen = SCR_TASK_LIST; M.cursor = 0; }
    else { M.screen = SCR_TASK_ADD; }
  }
}

static void handleTaskList(InputEvent ev) {
  if (g_taskCount == 0) { if (ev == EV_BACK) M.screen = SCR_TASKS; return; }
  if (ev == EV_UP)   M.cursor = (M.cursor + g_taskCount - 1) % g_taskCount;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % g_taskCount;
  if (ev == EV_OK)   taskToggleDone((uint8_t)M.cursor);
  if (ev == EV_BACK) { M.screen = SCR_TASKS; M.cursor = 0; }
}

static const char* TASK_PRESETS[] = {"Study", "Project", "Exercise", "Reading", "Chores", "Custom"};
static const int TASK_PRESET_COUNT = 6;
static void handleTaskAdd(InputEvent ev) {
  if (ev == EV_UP)   M.cursor = (M.cursor + TASK_PRESET_COUNT - 1) % TASK_PRESET_COUNT;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % TASK_PRESET_COUNT;
  if (ev == EV_OK)   { taskAdd(TASK_PRESETS[M.cursor]); buzzerOk(); M.screen = SCR_TASKS; }
  if (ev == EV_BACK) M.screen = SCR_TASKS;
}

static void handleDevices(InputEvent ev) {
  const int N = 2;
  if (ev == EV_UP)   M.cursor = (M.cursor + N - 1) % N;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % N;
  if (ev == EV_BACK) { M.screen = SCR_ROOT; M.cursor = 2; }
  if (ev == EV_OK) { M.screen = (M.cursor == 0) ? SCR_LIGHT : SCR_FAN; M.cursor = 0; }
}

static void handleLight(InputEvent ev) {
  const int N = 3; // ON, OFF, AUTO
  if (ev == EV_UP)   M.cursor = (M.cursor + N - 1) % N;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % N;
  if (ev == EV_BACK) { M.screen = SCR_DEVICES; M.cursor = 0; }
  if (ev == EV_OK) {
    setLightMode(M.cursor == 0 ? MODE_ON : M.cursor == 1 ? MODE_OFF : MODE_AUTO);
    buzzerOk();
  }
}

static void handleFan(InputEvent ev) {
  const int N = 3; // ON, OFF, AUTO
  if (ev == EV_UP)   M.cursor = (M.cursor + N - 1) % N;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % N;
  if (ev == EV_BACK) { M.screen = SCR_DEVICES; M.cursor = 1; }
  if (ev == EV_OK) {
    setFanMode(M.cursor == 0 ? MODE_ON : M.cursor == 1 ? MODE_OFF : MODE_AUTO);
    buzzerOk();
  }
}

static void handleSensors(InputEvent ev) {
  if (ev == EV_BACK) { M.screen = SCR_ROOT; M.cursor = 3; }
}

void menuUpdate(unsigned long now) {
  InputEvent ev = g_lastEvent;

  // Any input while idle opens the menu instead of being consumed by a screen
  if (M.screen == SCR_IDLE) {
    if (ev != EV_NONE) gotoRoot();
    return;
  }

  // HOME button always jumps back to the face. If the setup AP was up,
  // tearing it down left WiFi disconnected — nudge netsync to retry
  // right away instead of waiting out its normal 15s retry timer.
  if (ev == EV_HOME) {
    if (wifiPortalActive()) { wifiPortalStop(); netRequestReconnect(); }
    gotoIdle();
    return;
  }

  // WiFi setup: the "input" here comes from the phone over HTTP, not
  // IR/buttons, so it's handled separately and exempted from the
  // normal idle timeout below (entering a password can easily take
  // longer than IDLE_TIMEOUT_MS with zero button presses).
  if (M.screen == SCR_WIFI) {
    if (wifiPortalCredsReady()) {
      wifiPortalClearCredsReady();
      wifiPortalStop();
      netRequestReconnect();
    }
    if (ev == EV_BACK) {
      wifiPortalStop();
      netRequestReconnect(); // same reasoning as EV_HOME above — reconnect promptly even if creds weren't changed
      M.screen = SCR_ROOT;
      M.cursor = 4;
      return;
    }

    // Once actually connected, hold the "Connected!" confirmation
    // briefly, then drop back into the root menu automatically instead
    // of waiting for a BACK press.
    if (!wifiPortalActive() && netIsWifiConnected()) {
      if (g_wifiConnectedAt == 0) g_wifiConnectedAt = now;
      else if (now - g_wifiConnectedAt > WIFI_CONNECTED_HOLD_MS) {
        M.screen = SCR_ROOT;
        M.cursor = 4;
      }
    } else {
      g_wifiConnectedAt = 0;
    }
    return;
  }

  if (now - g_lastInputAt > IDLE_TIMEOUT_MS) { gotoIdle(); return; }
  if (ev == EV_NONE) return;

  switch (M.screen) {
    case SCR_ROOT:      handleRoot(ev); break;
    case SCR_TIMER:      handleTimer(ev); break;
    case SCR_TIMER_SET:  handleTimerSet(ev); break;
    case SCR_TASKS:      handleTasks(ev); break;
    case SCR_TASK_LIST:  handleTaskList(ev); break;
    case SCR_TASK_ADD:   handleTaskAdd(ev); break;
    case SCR_DEVICES:    handleDevices(ev); break;
    case SCR_LIGHT:      handleLight(ev); break;
    case SCR_FAN:        handleFan(ev); break;
    case SCR_SENSORS:    handleSensors(ev); break;
    default: break;
  }
}

// ---- Rendering -------------------------------------------------------
static void drawListMenu(const char *title, const char **items, int count, int cursor) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  for (int i = 0; i < count; i++) {
    display.setCursor(6, 14 + i * 11);
    display.print(i == cursor ? "> " : "  ");
    display.println(items[i]);
  }
  display.display();
}

void showBootScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 25);
  display.println("DESK BUDDY");
  display.setCursor(10, 40);
  display.println("booting...");
  display.display();
}

static void renderMenuScreen() {
  char buf[24];
  switch (M.screen) {
    case SCR_ROOT:
      drawListMenu("DESK BUDDY", ROOT_ITEMS, ROOT_COUNT, M.cursor);
      break;
    case SCR_TIMER: {
      const char *items[] = {"Start", "Set Time", "Cancel"};
      display.clearDisplay();
      display.setCursor(0,0); display.println("TIMER");
      unsigned long rem = timerRemainingMs();
      int h = rem / 3600000; int m = (rem / 60000) % 60; int s = (rem / 1000) % 60;
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
      display.setCursor(0, 12); display.println(buf);
      for (int i = 0; i < 3; i++) {
        display.setCursor(6, 26 + i * 11);
        display.print(i == M.cursor ? "> " : "  ");
        display.println(items[i]);
      }
      display.display();
      break;
    }
    case SCR_TIMER_SET: {
      display.clearDisplay();
      display.setCursor(0,0); display.println("SET TIMER");
      const char* labels[3] = {"Hours", "Minutes", "Seconds"};
      int vals[3] = {T.setHours, T.setMinutes, T.setSeconds};
      for (int i = 0; i < 3; i++) {
        display.setCursor(0, 14 + i*11);
        display.print(i == M.setField ? "> " : "  ");
        snprintf(buf, sizeof(buf), "%-8s%02d", labels[i], vals[i]);
        display.println(buf);
      }
      display.setCursor(0, 52); display.println("OK: next  BACK: done");
      display.display();
      break;
    }
    case SCR_TASKS: {
      const char *items[] = {"Task List", "Add Task"};
      drawListMenu("TASKS", items, 2, M.cursor);
      break;
    }
    case SCR_TASK_LIST: {
      display.clearDisplay();
      display.setCursor(0,0); display.println("TASKS");
      if (g_taskCount == 0) {
        display.setCursor(0,16); display.println("(empty)");
      } else {
        for (int i = 0; i < g_taskCount; i++) {
          display.setCursor(6, 12 + i * 11);
          display.print(i == M.cursor ? "> " : "  ");
          display.print(g_tasks[i].done ? "[x] " : "[ ] ");
          display.println(g_tasks[i].name);
        }
      }
      display.display();
      break;
    }
    case SCR_TASK_ADD:
      drawListMenu("ADD TASK", TASK_PRESETS, TASK_PRESET_COUNT, M.cursor);
      break;
    case SCR_DEVICES: {
      const char *items[] = {"Light", "Fan"};
      drawListMenu("DEVICES", items, 2, M.cursor);
      break;
    }
    case SCR_LIGHT: {
      const char *items[] = {"ON", "OFF", "AUTO"};
      const char *modeName[] = {"OFF", "ON", "AUTO"}; // matches DeviceMode enum order
      display.clearDisplay();
      display.setCursor(0,0); display.println("LIGHT");
      display.setCursor(0,10);
      display.print("Relay : "); display.println(D.lightOn ? "ON" : "OFF");
      display.setCursor(0,20);
      display.print("Mode  : "); display.println(modeName[(int)D.lightMode]);
      for (int i = 0; i < 3; i++) {
        display.setCursor(6, 32 + i*11);
        display.print(i == M.cursor ? "> " : "  ");
        display.println(items[i]);
      }
      display.display();
      break;
    }
    case SCR_FAN: {
      const char *items[] = {"ON", "OFF", "AUTO"};
      const char *modeName[] = {"OFF", "ON", "AUTO"}; // matches DeviceMode enum order
      display.clearDisplay();
      display.setCursor(0,0); display.println("FAN");
      display.setCursor(0,10);
      display.print("Relay : "); display.println(D.fanOn ? "ON" : "OFF");
      display.setCursor(0,20);
      display.print("Mode  : "); display.println(modeName[(int)D.fanMode]);
      for (int i = 0; i < 3; i++) {
        display.setCursor(6, 32 + i*11);
        display.print(i == M.cursor ? "> " : "  ");
        display.println(items[i]);
      }
      display.display();
      break;
    }
    case SCR_SENSORS: {
      display.clearDisplay();
      display.setCursor(0,0); display.println("SENSORS");
      display.setCursor(0,14);
      snprintf(buf, sizeof(buf), "Distance: %.1f cm", S.distanceCm);
      display.println(buf);
      display.setCursor(0,26);
      snprintf(buf, sizeof(buf), "Light   : %d", S.ldrRaw);
      display.println(buf);
      display.setCursor(0,38);
      display.print("Status  : ");
      display.println(S.isDark ? "DARK" : "BRIGHT");
      display.setCursor(0,50);
      display.print("Presence: ");
      display.println(S.presence ? "YES" : "NO");
      display.display();
      break;
    }
    case SCR_WIFI: {
      display.clearDisplay();
      display.setCursor(0,0); display.println("WIFI SETUP");
      display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
      if (wifiPortalActive()) {
        display.setCursor(0,16); display.print("AP: "); display.println(wifiPortalApSsid());
        display.setCursor(0,28); display.print("IP: "); display.println(wifiPortalIP().toString());
        display.setCursor(0,42); display.println("Join that WiFi, then");
        display.setCursor(0,52); display.println("visit the IP above.");
      } else if (netIsWifiConnected()) {
        display.setCursor(0,26); display.println("Connected!");
        display.setCursor(0,40); display.println("BACK to return");
      } else {
        display.setCursor(0,26); display.println("Connecting...");
        display.setCursor(0,40); display.println("BACK to cancel");
      }
      display.display();
      break;
    }
    default: break;
  }
}

void renderFrame(unsigned long now) {
  static unsigned long lastRender = 0;
  if (now - lastRender < FRAME_INTERVAL_MS) return;
  lastRender = now;

  if (M.screen == SCR_IDLE) faceRenderIdle();
  else renderMenuScreen();
}
#include <Arduino.h>
#include "menu.h"
#include "config.h"
#include "globals.h"
#include "face.h"
#include "sensors.h"
#include "devices.h"
#include "inputs.h"
#include "buzzer.h"
#include "settings.h"
#include "netsync.h"
#include "wifi_portal.h"
#include "ble_control.h"

MenuState M;

static const char* ROOT_ITEMS[] = {"Devices", "Sensors", "Threshold Setup", "WiFi Setup"};
static const int ROOT_COUNT = 4;
// Root-menu cursor positions, used when backing out of a sub-screen.
enum { ROOT_DEVICES = 0, ROOT_SENSORS = 1, ROOT_THRESH = 2, ROOT_WIFI = 3 };

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
      case ROOT_DEVICES: M.screen = SCR_DEVICES; M.cursor = 0; break;
      case ROOT_SENSORS: M.screen = SCR_SENSORS; break;
      case ROOT_THRESH:  M.screen = SCR_THRESH; M.cursor = 0; break;
      case ROOT_WIFI:    M.screen = SCR_WIFI; wifiPortalStart(); g_wifiConnectedAt = 0; break;
    }
    buzzerOk();
  }
}

static void handleDevices(InputEvent ev) {
  const int N = 2;
  if (ev == EV_UP)   M.cursor = (M.cursor + N - 1) % N;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % N;
  if (ev == EV_BACK) { M.screen = SCR_ROOT; M.cursor = ROOT_DEVICES; }
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
  if (ev == EV_BACK) { M.screen = SCR_ROOT; M.cursor = ROOT_SENSORS; }
}

// ---- Threshold Setup ----
static void handleThresh(InputEvent ev) {
  const int N = 2; // Ultrasonic, LDR
  if (ev == EV_UP)   M.cursor = (M.cursor + N - 1) % N;
  if (ev == EV_DOWN) M.cursor = (M.cursor + 1) % N;
  if (ev == EV_BACK) { M.screen = SCR_ROOT; M.cursor = ROOT_THRESH; }
  if (ev == EV_OK)   { M.screen = (M.cursor == 0) ? SCR_THRESH_US : SCR_THRESH_LDR; M.setField = 0; }
}

// UP/DOWN adjusts the selected value by one step, OK switches between
// the two values, BACK returns. Every change is saved to NVS right away
// (settingsSaveThresholds()), so nothing is lost to the idle timeout.
// The hysteresis gap is enforced: ON must stay below OFF by >= 1 step.
static void handleThreshUs(InputEvent ev) {
  if (ev == EV_OK)   { M.setField ^= 1; return; }
  if (ev == EV_BACK) { M.screen = SCR_THRESH; M.cursor = 0; return; }
  if (ev != EV_UP && ev != EV_DOWN) return;
  int delta = (ev == EV_UP) ? DISTANCE_STEP_CM : -DISTANCE_STEP_CM;
  int on  = (int)lroundf(DISTANCE_ON_CM);
  int off = (int)lroundf(DISTANCE_OFF_CM);
  int &v  = (M.setField == 0) ? on : off;
  int lo  = (M.setField == 0) ? DISTANCE_MIN_CM : on + DISTANCE_STEP_CM;
  int hi  = (M.setField == 0) ? off - DISTANCE_STEP_CM : DISTANCE_MAX_CM;
  int nv  = v + delta;
  if (nv < lo || nv > hi) { buzzerError(); return; }
  v = nv;
  DISTANCE_ON_CM  = (float)on;
  DISTANCE_OFF_CM = (float)off;
  settingsSaveThresholds();
}

static void handleThreshLdr(InputEvent ev) {
  if (ev == EV_OK)   { M.setField ^= 1; return; }
  if (ev == EV_BACK) { M.screen = SCR_THRESH; M.cursor = 1; return; }
  if (ev != EV_UP && ev != EV_DOWN) return;
  int delta = (ev == EV_UP) ? LDR_STEP : -LDR_STEP;
  int &v  = (M.setField == 0) ? LDR_THRESHOLD_ON : LDR_THRESHOLD_OFF;
  int lo  = (M.setField == 0) ? LDR_MIN : LDR_THRESHOLD_ON + LDR_STEP;
  int hi  = (M.setField == 0) ? LDR_THRESHOLD_OFF - LDR_STEP : LDR_MAX;
  int nv  = v + delta;
  if (nv < lo || nv > hi) { buzzerError(); return; }
  v = nv;
  settingsSaveThresholds();
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
      M.cursor = ROOT_WIFI;
      return;
    }

    // Once actually connected, hold the "Connected!" confirmation
    // briefly, then drop back into the root menu automatically instead
    // of waiting for a BACK press.
    if (!wifiPortalActive() && netIsWifiConnected()) {
      if (g_wifiConnectedAt == 0) g_wifiConnectedAt = now;
      else if (now - g_wifiConnectedAt > WIFI_CONNECTED_HOLD_MS) {
        M.screen = SCR_ROOT;
        M.cursor = ROOT_WIFI;
      }
    } else {
      g_wifiConnectedAt = 0;
    }
    return;
  }

  if (now - g_lastInputAt > IDLE_TIMEOUT_MS) { gotoIdle(); return; }
  if (ev == EV_NONE) return;

  switch (M.screen) {
    case SCR_ROOT:       handleRoot(ev); break;
    case SCR_DEVICES:    handleDevices(ev); break;
    case SCR_LIGHT:      handleLight(ev); break;
    case SCR_FAN:        handleFan(ev); break;
    case SCR_SENSORS:    handleSensors(ev); break;
    case SCR_THRESH:     handleThresh(ev); break;
    case SCR_THRESH_US:  handleThreshUs(ev); break;
    case SCR_THRESH_LDR: handleThreshLdr(ev); break;
    default: break;
  }
}

// ---- Rendering -------------------------------------------------------
static void drawListMenu(const char *title, const char **items, int count, int cursor,
                         const char *badge = nullptr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title);
  if (badge) { // small right-aligned status text in the title bar
    display.setCursor(SCREEN_WIDTH - (int)strlen(badge) * 6, 0);
    display.print(badge);
  }
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
      drawListMenu("DESK BUDDY", ROOT_ITEMS, ROOT_COUNT, M.cursor, bleIsConnected() ? "BT" : nullptr);
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
    case SCR_THRESH: {
      const char *items[] = {"Ultrasonic", "LDR"};
      drawListMenu("THRESHOLD SETUP", items, 2, M.cursor);
      break;
    }
    case SCR_THRESH_US: {
      display.clearDisplay();
      display.setCursor(0,0); display.println("ULTRASONIC (cm)");
      display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
      display.setCursor(0,14);
      snprintf(buf, sizeof(buf), "%sON  <= %3d cm", M.setField == 0 ? "> " : "  ", (int)lroundf(DISTANCE_ON_CM));
      display.println(buf);
      display.setCursor(0,25);
      snprintf(buf, sizeof(buf), "%sOFF >= %3d cm", M.setField == 1 ? "> " : "  ", (int)lroundf(DISTANCE_OFF_CM));
      display.println(buf);
      display.setCursor(0,38);
      if (S.distanceCm >= 900.0f) snprintf(buf, sizeof(buf), "Now: ---  %s", S.presence ? "PRESENT" : "AWAY");
      else snprintf(buf, sizeof(buf), "Now: %.1f %s", S.distanceCm, S.presence ? "PRESENT" : "AWAY");
      display.println(buf);
      display.setCursor(0,54); display.println("UP/DN +-1  OK: next");
      display.display();
      break;
    }
    case SCR_THRESH_LDR: {
      display.clearDisplay();
      display.setCursor(0,0); display.println("LDR (0-4095)");
      display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
      display.setCursor(0,14);
      snprintf(buf, sizeof(buf), "%sDARK   <= %4d", M.setField == 0 ? "> " : "  ", LDR_THRESHOLD_ON);
      display.println(buf);
      display.setCursor(0,25);
      snprintf(buf, sizeof(buf), "%sBRIGHT >= %4d", M.setField == 1 ? "> " : "  ", LDR_THRESHOLD_OFF);
      display.println(buf);
      display.setCursor(0,38);
      snprintf(buf, sizeof(buf), "Now: %d %s", S.ldrRaw, S.isDark ? "DARK" : "BRIGHT");
      display.println(buf);
      display.setCursor(0,54); display.println("UP/DN +-100 OK: next");
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
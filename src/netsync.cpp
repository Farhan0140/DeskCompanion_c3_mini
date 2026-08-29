// =======================================================================
// NETSYNC MODULE — WiFi, NTP (online time), and Firebase Realtime
// Database sync over plain HTTPS REST calls (no external Firebase
// library needed — RTDB just speaks JSON over HTTP).
//
// Firebase RTDB REST cheat sheet:
//   PUT  {HOST}/path.json?auth=SECRET   body=JSON   -> overwrite
//   GET  {HOST}/path.json?auth=SECRET               -> read
//   Root used here: /deskbuddy/...
// =======================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "netsync.h"
#include "config.h"
#include "secrets.h"
#include "sensors.h"
#include "devices.h"
#include "timer_task.h"
#include "buzzer.h"
#include "face.h"

static WiFiClientSecure g_netClient;
static bool g_wifiOk = false;

static unsigned long g_lastPush = 0;
static unsigned long g_lastPull = 0;
static unsigned long g_lastNtpResync = 0;
static unsigned long g_lastWifiRetry = 0;

static void netTask(void *pvParameters);       // forward decl — defined at bottom of this file
static void pushFirebaseMode();                // forward decl — defined below, called from onWifiConnected() too

// Set from core 1 (devices.cpp) whenever a relay mode changes locally
// (IR remote, menu, ...). Cleared by netTask on core 0 once it has
// pushed the new mode to Firebase. A plain flag is enough here: worst
// case is one extra push, never a lost one (device.cpp only ever sets
// it, never clears it).
static volatile bool g_modeDirty = false;

void netMarkModeDirty() {
  g_modeDirty = true;
}

bool getLocalTimeStr(char *out, size_t outLen) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50)) return false;
  strftime(out, outLen, "%Y-%m-%d %H:%M:%S", &timeinfo);
  return true;
}

// Fully non-blocking from the caller's (main loop's) point of view:
// this only kicks off the WiFi association and spins up a background
// task, then returns immediately. The face/menu/sensors/relays start
// on the very next loop() iteration without waiting on the network.
//
// IMPORTANT: all networking (WiFi status polling, NTP resync, and —
// critically — the Firebase HTTPS PUT/GET calls, which can each take
// anywhere from ~100ms to a few seconds) now runs entirely inside
// netTask() on CORE 0, completely separate from the main loop() on
// core 1 that handles buttons/IR/menu/animation. That's what fixes
// buttons feeling unresponsive: a slow or stalled HTTP request can no
// longer block button polling, because they're not sharing a core.
void netInit() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  g_wifiOk = false;
  faceStartWifiConnecting(); // won't cut off the FACE_EVENT_BOOT hello just raised in setup()
  Serial.println(F("[WIFI] Connecting in background (face/menu/sensors/relays start immediately)"));

  xTaskCreatePinnedToCore(
    netTask,        // task function (defined below)
    "netTask",      // name, for debugging
    10240,          // stack size in bytes — TLS/HTTPS needs a generous stack
    nullptr,        // no parameters
    1,              // priority (low — this is background work)
    nullptr,        // no need to keep the task handle
    0               // pin to core 0 (Arduino loop() runs on core 1)
  );
}

// Called once, exactly when the connection transitions to CONNECTED —
// whether that happens a second after boot or five minutes later.
static void onWifiConnected() {
  g_wifiOk = true;
  Serial.print(F("[WIFI] Connected, IP: "));
  Serial.println(WiFi.localIP());
  g_netClient.setInsecure(); // skip cert validation (fine for RTDB REST + NTP-set time)

  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  Serial.println(F("[NTP] Requested time sync"));

  buzzerWifiConnected(); // small confirmation beep, once
  faceClearWifiConnecting();

  // Publish the locally-restored relay modes (devicesInit() loads
  // these from NVS at boot, see devices.cpp) to Firebase the instant
  // we come online, rather than pulling Firebase's copy over them.
  // The device's own state is authoritative — Firebase's light_mode/
  // fan_mode nodes are a mirror of it, updated here and on every
  // local change (see netMarkModeDirty()). Pulling here instead would
  // let a stale Firebase value (e.g. from before a reset) silently
  // undo whatever the IR remote last set. Resetting g_lastPull keeps
  // the regular pull cadence starting fresh from here.
  Serial.println(F("[FB] Publishing locally-restored relay state to Firebase..."));
  pushFirebaseMode();
  g_lastPull = millis();
}

// ---- Firebase helpers ----
static String firebaseUrl(const char *path) {
  String url = String(FIREBASE_HOST) + path + ".json";
  if (strlen(FIREBASE_AUTH) > 0) url += String("?auth=") + FIREBASE_AUTH;
  return url;
}

static bool firebasePut(const char *path, const String &json) {
  if (!g_wifiOk) return false;
  HTTPClient http;
  http.begin(g_netClient, firebaseUrl(path));
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(json);
  bool ok = (code >= 200 && code < 300);
  if (!ok) Serial.printf("[FB] PUT %s failed, code=%d\n", path, code);
  http.end();
  return ok;
}

static String firebaseGet(const char *path) {
  if (!g_wifiOk) return "";
  HTTPClient http;
  http.begin(g_netClient, firebaseUrl(path));
  int code = http.GET();
  String payload = "";
  if (code == 200) payload = http.getString();
  else Serial.printf("[FB] GET %s failed, code=%d\n", path, code);
  http.end();
  return payload;
}

// Pushes current status snapshot to /deskbuddy/status
//
// NOTE ON THREAD SAFETY: this reads S/D/T, which are also written by
// the main loop on the other core (sensorsUpdate/devicesUpdate/
// timerTaskUpdate). They're plain ints/bools/enums, so reads here
// can't crash or corrupt memory, but a value could in theory be read
// mid-update and be one cycle stale in the pushed JSON — harmless for
// a status snapshot pushed every 5s. If you extend this to something
// where that staleness would matter, wrap the read with a short
// portMUX_TYPE spinlock (taskENTER_CRITICAL/EXIT_CRITICAL).
static const char* getModeStr(DeviceMode mode) {
  switch (mode) {
    case MODE_ON:   return "ON";
    case MODE_OFF:  return "OFF";
    default:        return "AUTO";
  }
}

// Publishes D.lightMode/D.fanMode to Firebase's command/mirror nodes
// (/deskbuddy/light_mode, /deskbuddy/fan_mode) — the same nodes
// pullFirebaseCommands() reads back. Called right after a local mode
// change (via the g_modeDirty flag) and once on (re)connect, so those
// nodes always reflect whichever side changed most recently instead
// of the device and Firebase fighting over stale values.
static void pushFirebaseMode() {
  firebasePut("/deskbuddy/light_mode", "\"" + String(getModeStr(D.lightMode)) + "\"");
  firebasePut("/deskbuddy/fan_mode", "\"" + String(getModeStr(D.fanMode)) + "\"");
}

static DeviceMode parseDeviceMode(String val) {
  val.trim();
  val.toUpperCase();
  if (val.startsWith("\"")) val = val.substring(1, val.length() - 1);
  if (val == "1" || val == "ON" || val == "TRUE") return MODE_ON;
  if (val == "0" || val == "OFF" || val == "FALSE") return MODE_OFF;
  if (val == "2" || val == "AUTO") return MODE_AUTO;
  return MODE_AUTO;
}

static void pushFirebaseStatus() {
  char ts[32] = "unsynced";
  getLocalTimeStr(ts, sizeof(ts));

  String json = "{";
  json += "\"distance_cm\":" + String(S.distanceCm, 1) + ",";
  json += "\"distance_min_cm\":" + String(S.distanceMinCm, 1) + ",";
  json += "\"distance_max_cm\":" + String(S.distanceMaxCm, 1) + ",";
  json += "\"presence\":" + String(S.presence ? "true" : "false") + ",";
  json += "\"ldr_raw\":" + String(S.ldrRaw) + ",";
  json += "\"is_dark\":" + String(S.isDark ? "true" : "false") + ",";
  json += "\"light_on\":" + String(D.lightOn ? "true" : "false") + ",";
  json += "\"light_mode\":" + String((int)D.lightMode) + ",";
  json += "\"light_mode_str\":\"" + String(getModeStr(D.lightMode)) + "\",";
  json += "\"fan_on\":" + String(D.fanOn ? "true" : "false") + ",";
  json += "\"fan_mode\":" + String((int)D.fanMode) + ",";
  json += "\"fan_mode_str\":\"" + String(getModeStr(D.fanMode)) + "\",";
  json += "\"timer_running\":" + String(T.running ? "true" : "false") + ",";
  json += "\"timer_remaining_ms\":" + String(timerRemainingMs()) + ",";
  json += "\"task_count\":" + String(g_taskCount) + ",";
  json += "\"last_update\":\"" + String(ts) + "\"";
  json += "}";

  firebasePut("/deskbuddy/status", json);
}

// Very small hand-rolled JSON value extractor for our own flat command
// object — avoids pulling in ArduinoJson just for a few fields.
static String jsonExtract(const String &json, const char *key) {
  String pat = String("\"") + key + "\":";
  int idx = json.indexOf(pat);
  if (idx < 0) return "";
  int valStart = idx + pat.length();
  int valEnd = valStart;
  bool inQuotes = (json[valStart] == '"');
  if (inQuotes) {
    valEnd = json.indexOf('"', valStart + 1);
    return json.substring(valStart + 1, valEnd);
  } else {
    while (valEnd < (int)json.length() &&
           json[valEnd] != ',' && json[valEnd] != '}') valEnd++;
    return json.substring(valStart, valEnd);
  }
}

// Pulls remote commands from Firebase RTDB.
// Supports both /deskbuddy/command JSON object and direct control nodes (/deskbuddy/light_mode, /deskbuddy/fan_mode).
static void pullFirebaseCommands() {
  // 1. Direct node control: /deskbuddy/light_mode and /deskbuddy/fan_mode
  String directLight = firebaseGet("/deskbuddy/light_mode");
  if (directLight.length() > 0 && directLight != "null") {
    setLightMode(parseDeviceMode(directLight));
  }

  String directFan = firebaseGet("/deskbuddy/fan_mode");
  if (directFan.length() > 0 && directFan != "null") {
    setFanMode(parseDeviceMode(directFan));
  }

  // 2. Command object control: /deskbuddy/command
  String json = firebaseGet("/deskbuddy/command");
  if (json.length() < 3 || json == "null") return; // nothing pending

  String lm = jsonExtract(json, "light_mode");
  if (lm.length() == 0) lm = jsonExtract(json, "light");
  if (lm.length()) setLightMode(parseDeviceMode(lm));

  String fm = jsonExtract(json, "fan_mode");
  if (fm.length() == 0) fm = jsonExtract(json, "fan");
  if (fm.length()) setFanMode(parseDeviceMode(fm));

  String addTask = jsonExtract(json, "add_task");
  if (addTask.length()) taskAdd(addTask.c_str());

  String setTimerMin = jsonExtract(json, "set_timer_minutes");
  if (setTimerMin.length()) {
    T.setHours = 0; T.setMinutes = setTimerMin.toInt(); T.setSeconds = 0;
    timerStart();
  }

  firebasePut("/deskbuddy/command", "{}");
}

static void netUpdate(unsigned long now) {
  if (!g_wifiOk) {
    if (WiFi.status() == WL_CONNECTED) {
      onWifiConnected();
    } else if (now - g_lastWifiRetry > 15000) {
      // Not connected yet — nudge it again every 15s. This does NOT
      // block: WiFi.begin() just (re)starts the association attempt
      // and returns immediately; we keep polling status() here.
      g_lastWifiRetry = now;
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      Serial.println(F("[WIFI] Still not connected, retrying..."));
    }
    return; // nothing else to do until we're actually online
  }

  // Detect an unexpected disconnect (router reboot, out of range, etc.)
  // and drop back into the reconnect loop above.
  if (WiFi.status() != WL_CONNECTED) {
    g_wifiOk = false;
    faceStartWifiConnecting();
    Serial.println(F("[WIFI] Connection lost — retrying in background"));
    return;
  }

  // Handle a local mode change (IR remote, menu) before anything else
  // this tick, and in particular before the periodic pull below: if
  // both were due on the same tick, pulling first would fetch the
  // still-stale Firebase value and stomp the change we're about to
  // push. Pushing first means a same-tick pull just reads back what
  // we pushed — a harmless no-op.
  if (g_modeDirty) {
    g_modeDirty = false;
    pushFirebaseMode();
  }

  if (now - g_lastNtpResync > NTP_RESYNC_INTERVAL_MS) {
    g_lastNtpResync = now;
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  }

  if (now - g_lastPush > FIREBASE_PUSH_INTERVAL_MS) {
    g_lastPush = now;
    pushFirebaseStatus();
  }

  if (now - g_lastPull > FIREBASE_PULL_INTERVAL_MS) {
    g_lastPull = now;
    pullFirebaseCommands();
  }
}

// Background task body: just calls netUpdate() on its own schedule,
// forever, entirely on core 0. A 50ms tick is plenty responsive for
// WiFi/NTP/Firebase timing (which itself only fires every 2.5-15s)
// while keeping this task's own CPU usage negligible.
static void netTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    netUpdate(millis());
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
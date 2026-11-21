// =======================================================================
// NETSYNC MODULE — WiFi, NTP (online time), and a periodic server health
// check (HTTPS GET HEALTH_URL every HEALTH_INTERVAL_MS, result shown on
// the OLED face). Everything runs in a background FreeRTOS task so a
// slow network call never stalls the main loop.
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
#include "buzzer.h"
#include "face.h"
#include "wifi_portal.h"

static WiFiClientSecure g_netClient;
static bool g_wifiOk = false;

static unsigned long g_lastNtpResync = 0;
static unsigned long g_lastWifiRetry = 0;

// Server health check scheduling (see netUpdate()).
static unsigned long g_nextHealthAt = 0;
static bool g_healthRetried = false;   // already used the one quick retry after a failure
static volatile bool g_serverOk = false;

// The credentials actually in use — loaded from NVS (saved by the
// WiFi setup portal) if present, otherwise falling back to the
// hardcoded defaults in secrets.h. See loadWifiCreds()/netInit().
static char g_ssid[33] = "";
static char g_pass[65] = "";

static void netTask(void *pvParameters);       // forward decl — defined at bottom of this file

// Set from core 1 once the WiFi setup portal has saved fresh
// credentials. A plain flag is enough here: worst case is one extra
// reconnect, never a lost one.
static volatile bool g_credsDirty = false;

void netRequestReconnect() {
  g_credsDirty = true;
}

bool netIsWifiConnected() {
  return g_wifiOk;
}

bool netIsServerOk() {
  return g_serverOk;
}

static void loadWifiCreds() {
  if (!wifiCredsLoad(g_ssid, sizeof(g_ssid), g_pass, sizeof(g_pass))) {
    strncpy(g_ssid, WIFI_SSID, sizeof(g_ssid) - 1);
    strncpy(g_pass, WIFI_PASSWORD, sizeof(g_pass) - 1);
  }
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
// IMPORTANT: all networking (WiFi status polling, NTP resync, and the
// HTTPS health check, which can take seconds against a cold-starting
// server) runs entirely inside netTask(), separate from the main
// loop() that handles buttons/IR/menu/animation/BLE.
void netInit() {
  loadWifiCreds();
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid, g_pass);
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
    0               // pin to core 0 (the only core on the ESP32-C3)
  );
}

// Called once, exactly when the connection transitions to CONNECTED —
// whether that happens a second after boot or five minutes later.
static void onWifiConnected() {
  g_wifiOk = true;
  Serial.print(F("[WIFI] Connected, IP: "));
  Serial.println(WiFi.localIP());
  g_netClient.setInsecure(); // skip cert validation (health check only reads a status)

  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  Serial.println(F("[NTP] Requested time sync"));

  buzzerWifiConnected(); // small confirmation beep, once
  faceClearWifiConnecting();

  // First health check shortly after (re)connecting, then every
  // HEALTH_INTERVAL_MS — see netUpdate().
  g_nextHealthAt = millis() + HEALTH_FIRST_DELAY_MS;
  g_healthRetried = false;
}

// ---- Server health check ----
// OK = HTTP 200 and the top-level "status" field is "ok", e.g.
//   {"status":"ok","time":"...","uptime_seconds":292,"checks":{...}}
// (The top-level key comes first in the response, so the first
// "status" match is the overall one, not a nested check's.)
static bool checkServerHealth() {
  HTTPClient http;
  http.setConnectTimeout(HEALTH_HTTP_TIMEOUT_MS);
  http.setTimeout(HEALTH_HTTP_TIMEOUT_MS);
  if (!http.begin(g_netClient, HEALTH_URL)) {
    Serial.println(F("[HEALTH] begin() failed"));
    return false;
  }
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String body = http.getString();
    int k = body.indexOf("\"status\"");
    if (k >= 0) {
      int colon = body.indexOf(':', k);
      int q1 = body.indexOf('"', colon + 1);
      int q2 = body.indexOf('"', q1 + 1);
      if (colon > 0 && q1 > 0 && q2 > q1) {
        String v = body.substring(q1 + 1, q2);
        v.toLowerCase();
        ok = (v == "ok");
      }
    } else {
      ok = true; // 200 with no status field — treat as healthy
    }
    Serial.printf("[HEALTH] %s -> 200, %s (free heap %u)\n", HEALTH_URL, ok ? "OK" : "NOT OK",
                  (unsigned)ESP.getFreeHeap());
  } else {
    Serial.printf("[HEALTH] %s -> failed, code=%d\n", HEALTH_URL, code);
  }
  http.end();
  return ok;
}

static void netUpdate(unsigned long now) {
  // The WiFi setup portal owns the radio (AP mode) while it's open —
  // stay out of its way entirely until it hands control back.
  if (wifiPortalActive()) return;

  // The portal just saved fresh credentials — reload them and retry
  // right away instead of waiting out the normal 15s retry timer on
  // whatever (possibly wrong/blank) credentials were in use before.
  if (g_credsDirty) {
    g_credsDirty = false;
    loadWifiCreds();
    WiFi.disconnect();
    g_wifiOk = false;
    WiFi.begin(g_ssid, g_pass);
    g_lastWifiRetry = now;
    faceStartWifiConnecting();
    Serial.println(F("[WIFI] New credentials saved, reconnecting..."));
  }

  if (!g_wifiOk) {
    if (WiFi.status() == WL_CONNECTED) {
      onWifiConnected();
    } else if (now - g_lastWifiRetry > 15000) {
      // Not connected yet — nudge it again every 15s. This does NOT
      // block: WiFi.begin() just (re)starts the association attempt
      // and returns immediately; we keep polling status() here.
      g_lastWifiRetry = now;
      WiFi.disconnect();
      WiFi.begin(g_ssid, g_pass);
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

  if (now - g_lastNtpResync > NTP_RESYNC_INTERVAL_MS) {
    g_lastNtpResync = now;
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  }

  // Health check: every HEALTH_INTERVAL_MS (10 min). After a failure,
  // one quick retry after HEALTH_RETRY_MS — free-tier hosts like Render
  // often fail the first request while they wake up — then back to the
  // normal 10-minute cadence.
  if ((long)(now - g_nextHealthAt) >= 0) {
    bool ok = checkServerHealth();
    g_serverOk = ok;
    faceShowEvent(ok ? FACE_EVENT_SERVER_OK : FACE_EVENT_SERVER_DOWN);
    unsigned long done = millis(); // the request itself may have taken a while
    if (ok) {
      g_healthRetried = false;
      g_nextHealthAt = done + HEALTH_INTERVAL_MS;
    } else if (!g_healthRetried) {
      g_healthRetried = true;
      g_nextHealthAt = done + HEALTH_RETRY_MS;
    } else {
      g_healthRetried = false;
      g_nextHealthAt = done + HEALTH_INTERVAL_MS;
    }
  }
}

// Background task body: just calls netUpdate() on its own schedule,
// forever. A 50ms tick is plenty responsive for WiFi/NTP/health timing
// while keeping this task's own CPU usage negligible.
static void netTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    netUpdate(millis());
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/*
  =====================================================================
  DESK BUDDY — ESP32 Desk Companion
  =====================================================================
  Full firmware implementing:
    - OLED face (smooth eased eye/mouth animation, idle personality)
    - Menu system (buttons + IR remote)
    - Ultrasonic presence sensing (hysteresis)
    - LDR ambient light sensing (analog)
    - Relay control (Light / Fan) with AUTO / ON / OFF modes
    - Buzzer feedback
    - Threshold Setup menu (ultrasonic + LDR thresholds, saved in NVS)
    - BLE counter button for the Soul Shield app (ported from the
      ESP32 Dev Module build — same BLE identity/protocol)
    - Server health check every 10 min (HTTPS), shown on the face
    - Online time via NTP

  Built for PlatformIO — see platformio.ini for board/library config.
  =====================================================================
*/
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "secrets.h"
#include "settings.h"
#include "globals.h"

#include "face.h"
#include "sensors.h"
#include "devices.h"
#include "inputs.h"
#include "buzzer.h"
#include "menu.h"
#include "netsync.h"
#include "wifi_portal.h"
#include "ble_control.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n================================"));
  Serial.println(F("DESK COMPANION — booting"));
  Serial.println(F("================================"));

  // OLED
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[ERR] SSD1306 not found"));
  }
  display.clearDisplay();
  display.display();

  // Peripherals
  buzzerInit();
  devicesInit();              // also opens the shared NVS handle (prefs)
  settingsLoad();             // saved ultrasonic/LDR thresholds (needs prefs open)
  sensorsInit();
  inputsInit();
  faceInit();
  menuInit();

  // BLE counter button — started before WiFi so the BLE stack gets its
  // memory before TLS buffers fragment the heap. Advertises immediately;
  // the Soul Shield app connects whenever it's in range.
  bleInit();

  // Boot personality — "hi" + handshake icon, then WiFi connecting
  // takes over the face (see netInit() below) once it starts.
  faceShowEvent(FACE_EVENT_BOOT);
  buzzerBoot();
  showBootScreen();

  // Networking (WiFi + NTP + server health check) runs entirely in its
  // own background task from here on — see netsync.cpp. loop()
  // below never touches the network directly, so a slow/stalled HTTP
  // call can't delay button/IR polling.
  netInit();

  Serial.printf("[BOOT] Ready. Free heap: %u bytes\n\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  unsigned long now = millis();

  inputsUpdate(now);          // buttons + IR -> raises events
  sensorsUpdate(now);         // ultrasonic + LDR polling with hysteresis
  devicesUpdate(now);         // AUTO relay logic based on sensor state
  bleUpdate(now);             // BLE counter button + app replies (non-blocking)
  menuUpdate(now);            // consumes input events, updates UI state
  faceUpdate(now);            // eases eyes/mouth toward target every frame
  buzzerUpdate(now);          // non-blocking tone sequencer
  wifiPortalUpdate(); // pumps the WiFi setup web/DNS servers — no-op unless that screen is open

  renderFrame(now);           // draws either menu or idle face, ~30-60 FPS
}
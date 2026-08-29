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
    - Timer subsystem
    - Task list (persisted with Preferences/NVS)
    - Firebase Realtime Database sync (REST API over HTTPS)
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
#include "timer_task.h"
#include "menu.h"
#include "netsync.h"

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
  devicesInit();
  sensorsInit();
  inputsInit();
  timerTaskInit(&prefs);
  faceInit();
  menuInit();

  // Boot personality — "hi" + handshake icon, then WiFi connecting
  // takes over the face (see netInit() below) once it starts.
  faceShowEvent(FACE_EVENT_BOOT);
  buzzerBoot();
  showBootScreen();

  // Networking (WiFi + NTP + Firebase) runs entirely in its own
  // background task on core 0 from here on — see netsync.cpp. loop()
  // below never touches the network directly, so a slow/stalled HTTP
  // call can't delay button/IR polling.
  netInit();

  Serial.println(F("[BOOT] Ready.\n"));
}

void loop() {
  unsigned long now = millis();

  inputsUpdate(now);          // buttons + IR -> raises events
  sensorsUpdate(now);         // ultrasonic + LDR polling with hysteresis
  devicesUpdate(now);         // AUTO relay logic based on sensor state
  timerTaskUpdate(now);       // timer countdown + alarm trigger
  menuUpdate(now);            // consumes input events, updates UI state
  faceUpdate(now);            // eases eyes/mouth toward target every frame
  buzzerUpdate(now);          // non-blocking tone sequencer

  renderFrame(now);           // draws either menu or idle face, ~30-60 FPS
}
 #pragma once
// =======================================================================
// USER CONFIGURATION — compile-time constants and pin definitions only.
// This header is included by nearly every .cpp file, so it deliberately
// contains ONLY things that are safe to duplicate per translation unit
// (constexpr/const values and #defines) — no mutable variables. Mutable
// runtime-tunable settings (things you might later expose in a Settings
// menu) live in settings.h/.cpp instead, as proper extern globals.
// =======================================================================
#include <stdint.h>

// ---- OLED ----
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64

// ---- Ultrasonic hysteresis (presence detection) ----
// DISTANCE_ON_CM / DISTANCE_OFF_CM are now runtime settings (settings.h),
// editable from the "Threshold Setup" menu and saved to NVS. These are
// just their factory defaults + the limits/step the menu enforces.
static constexpr float DISTANCE_ON_CM_DEFAULT  = 6.0f;   // trigger ON  when <= this
static constexpr float DISTANCE_OFF_CM_DEFAULT = 8.0f;   // trigger OFF when >= this
static constexpr int   DISTANCE_STEP_CM = 1;
static constexpr int   DISTANCE_MIN_CM  = 1;
static constexpr int   DISTANCE_MAX_CM  = 200;
static constexpr unsigned long US_INTERVAL_MS = 500;  // sensor poll interval (300ms allows echoes to dissipate completely)


// ---- LDR ----
static constexpr unsigned long LDR_INTERVAL_MS = 200;   // how often a raw sample is taken
static constexpr uint8_t LDR_SAMPLE_COUNT = 15;          // moving-average window (~3s at 200ms/sample)
// LDR_THRESHOLD_ON / LDR_THRESHOLD_OFF live in settings.h (runtime,
// NVS-backed). Factory defaults + menu step/limits:
static constexpr int LDR_THRESHOLD_ON_DEFAULT  = 1400;
static constexpr int LDR_THRESHOLD_OFF_DEFAULT = 1600;
static constexpr int LDR_STEP    = 100;
static constexpr int LDR_MIN     = 0;
static constexpr int LDR_MAX     = 4000;

// ---- Behaviour ----
static constexpr unsigned long IDLE_TIMEOUT_MS   = 30000; // -> idle face after this

// ---- Frame / animation timing ----
static constexpr unsigned long FRAME_INTERVAL_MS = 20;   // ~50 FPS render/ease tick

// ---- Debounce ----
static constexpr unsigned long DEBOUNCE_MS     = 35;
static constexpr unsigned long REPEAT_DELAY_MS = 400;   // hold-to-repeat on UP/DOWN
static constexpr unsigned long REPEAT_RATE_MS  = 120;
static constexpr unsigned long IR_DEBOUNCE_MS  = 250;   // min interval between identical IR commands


// ---- WiFi setup portal (see wifi_portal.cpp) ----
// The ESP32's own hotspot name while the "WiFi Setup" menu screen is
// open — open network, no password, so a phone can join it in one tap.
#define WIFI_AP_SSID "DeskBuddy-Setup"

// ---- Networking cadence ----
static constexpr unsigned long NTP_RESYNC_INTERVAL_MS     = 3600000UL; // 1 hour

// ---- Server health check (see netsync.cpp) ----
#define HEALTH_URL "https://soul-shield-api.onrender.com/health"
static constexpr unsigned long HEALTH_INTERVAL_MS   = 600000UL; // every 10 minutes
static constexpr unsigned long HEALTH_RETRY_MS      = 30000UL;  // one quick retry after a failure (Render cold start)
static constexpr unsigned long HEALTH_FIRST_DELAY_MS = 3000UL;  // first check this long after WiFi connects
static constexpr uint16_t      HEALTH_HTTP_TIMEOUT_MS = 20000;  // cold-starting servers can be slow

// ---- BLE counter button (ported from Framework-ESP32-DevModule) ----
// Identity MUST match lib/bluetooth/counter-ble.ts in the Soul Shield app.
#define BLE_DEVICE_NAME  "SoulShield-ESP32"
#define BLE_SERVICE_UUID "9f83f3d0-8f5f-4a3f-9a1d-2c6a8f4e5b01"
#define BLE_CHAR_TX_UUID "a1b2c3d4-5e6f-4a7b-8c9d-0e1f2a3b4c5d"  // ESP32 -> phone (notify)
#define BLE_CHAR_RX_UUID "b2c3d4e5-6f7a-4b8c-9d0e-1f2a3b4c5d6e"  // phone -> ESP32 (write)
static constexpr unsigned long BLE_ACK_TIMEOUT_MS = 1500; // wait this long for the app's answer
static constexpr unsigned long BLE_DEBOUNCE_MS    = 50;   // counter-button debounce

// =======================================================================
// PIN CONFIGURATION  (ESP32-C3 Super Mini)
// =======================================================================
#define PIN_OLED_SDA     4
#define PIN_OLED_SCL     5

#define PIN_TRIG         20
#define PIN_ECHO         21

#define PIN_LDR          0   // ADC1_CH0 (GPIO0 on ESP32-C3)

#define PIN_IR           3

#define PIN_RELAY_LIGHT  6
#define PIN_RELAY_FAN    7
// Most relay modules are ACTIVE-LOW. Flip this if yours is active-high.
#define RELAY_ACTIVE_LOW true

#define PIN_BUZZER       10

// BLE counter button: GPIO1 <-> GND (internal pull-up, no resistor).
// (The Dev Module version used GPIO4, which is OLED SDA on this board.)
#define PIN_BLE_BUTTON   1


// =======================================================================
// IR REMOTE CODE MAP — replace with the codes printed by your remote.
// Boot the sketch, open Serial Monitor, press each button on your
// remote once; IRremote will print the raw hex code to copy here.
// =======================================================================
#define IR_UP      0x9F600707
#define IR_DOWN    0x9E610707
#define IR_OK      0x97680707
#define IR_BACK    0xD22D0707
#define IR_LIGHT   0xFD020707   // POWER -> toggle light
#define IR_FAN_ON  0xFB040707   // VOL+  -> fan on
#define IR_FAN_OFF 0xFA050707   // VOL-  -> fan off
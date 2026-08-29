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
static constexpr float DISTANCE_ON_CM   = 6.0f;   // trigger ON  when <= this
static constexpr float DISTANCE_OFF_CM  = 8.0f;   // trigger OFF when >= this
static constexpr unsigned long US_INTERVAL_MS = 500;  // sensor poll interval (300ms allows echoes to dissipate completely)


// ---- LDR ----
static constexpr unsigned long LDR_INTERVAL_MS = 200;   // how often a raw sample is taken
static constexpr uint8_t LDR_SAMPLE_COUNT = 15;          // moving-average window (~3s at 200ms/sample)

// ---- Behaviour ----
static constexpr unsigned long IDLE_TIMEOUT_MS   = 30000; // -> idle face after this
static constexpr int TIMER_MAX_MINUTES = 180;

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

// ---- Networking sync cadence ----
static constexpr unsigned long FIREBASE_PUSH_INTERVAL_MS = 5000;   // push sensor/status
static constexpr unsigned long FIREBASE_PULL_INTERVAL_MS = 2500;   // poll remote commands
static constexpr unsigned long NTP_RESYNC_INTERVAL_MS     = 3600000UL; // 1 hour

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
#include <Arduino.h>
#include "settings.h"
#include "config.h"
#include "globals.h"

// Ultrasonic presence hysteresis band (cm). Runtime-editable from the
// "Threshold Setup" menu; defaults come from config.h.
float DISTANCE_ON_CM    = DISTANCE_ON_CM_DEFAULT;
float DISTANCE_OFF_CM   = DISTANCE_OFF_CM_DEFAULT;

// A gap between these two (rather than one shared threshold) is the
// hysteresis band described in sensors.cpp — widen it if you still see
// occasional flicker right at dusk/dawn light levels, narrow it if the
// light feels sluggish to react. Values are on the averaged 0-4095 ADC
// scale, not a single raw reading.
int  LDR_THRESHOLD_ON   = LDR_THRESHOLD_ON_DEFAULT;    // <= this (averaged) => dark
int  LDR_THRESHOLD_OFF  = LDR_THRESHOLD_OFF_DEFAULT;   // >= this (averaged) => bright
bool AUTO_LIGHT_ENABLED = true;
bool AUTO_FAN_ENABLED   = true;
bool BUZZER_ENABLED     = true;
int  BUZZER_VOLUME      = 100;   // 0-100, tune to taste; see buzzer.cpp

// NVS keys (namespace "deskbuddy", shared `prefs` handle — see globals.h).
static const char *K_DIST_ON  = "dist_on";
static const char *K_DIST_OFF = "dist_off";
static const char *K_LDR_ON   = "ldr_on";
static const char *K_LDR_OFF  = "ldr_off";

void settingsLoad() {
  DISTANCE_ON_CM    = prefs.getFloat(K_DIST_ON,  DISTANCE_ON_CM_DEFAULT);
  DISTANCE_OFF_CM   = prefs.getFloat(K_DIST_OFF, DISTANCE_OFF_CM_DEFAULT);
  LDR_THRESHOLD_ON  = prefs.getInt(K_LDR_ON,     LDR_THRESHOLD_ON_DEFAULT);
  LDR_THRESHOLD_OFF = prefs.getInt(K_LDR_OFF,    LDR_THRESHOLD_OFF_DEFAULT);

  // Defend against a corrupt / inverted pair: hysteresis needs ON < OFF.
  if (DISTANCE_ON_CM >= DISTANCE_OFF_CM) {
    DISTANCE_ON_CM  = DISTANCE_ON_CM_DEFAULT;
    DISTANCE_OFF_CM = DISTANCE_OFF_CM_DEFAULT;
  }
  if (LDR_THRESHOLD_ON >= LDR_THRESHOLD_OFF) {
    LDR_THRESHOLD_ON  = LDR_THRESHOLD_ON_DEFAULT;
    LDR_THRESHOLD_OFF = LDR_THRESHOLD_OFF_DEFAULT;
  }

  Serial.printf("[SETTINGS] US on<=%.0fcm off>=%.0fcm | LDR dark<=%d bright>=%d\n",
                DISTANCE_ON_CM, DISTANCE_OFF_CM, LDR_THRESHOLD_ON, LDR_THRESHOLD_OFF);
}

void settingsSaveThresholds() {
  if (prefs.getFloat(K_DIST_ON,  -1) != DISTANCE_ON_CM)    prefs.putFloat(K_DIST_ON,  DISTANCE_ON_CM);
  if (prefs.getFloat(K_DIST_OFF, -1) != DISTANCE_OFF_CM)   prefs.putFloat(K_DIST_OFF, DISTANCE_OFF_CM);
  if (prefs.getInt(K_LDR_ON,     -1) != LDR_THRESHOLD_ON)  prefs.putInt(K_LDR_ON,     LDR_THRESHOLD_ON);
  if (prefs.getInt(K_LDR_OFF,    -1) != LDR_THRESHOLD_OFF) prefs.putInt(K_LDR_OFF,    LDR_THRESHOLD_OFF);
}

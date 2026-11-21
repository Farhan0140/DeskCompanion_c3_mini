#pragma once
// =======================================================================
// SETTINGS — mutable runtime-tunable values. These are true globals
// shared across translation units, so they're declared `extern` here
// and DEFINED EXACTLY ONCE in settings.cpp. (Unlike config.h's
// compile-time constants, these can be changed at runtime — the four
// thresholds below are edited from the "Threshold Setup" menu and
// persisted to NVS, see settingsLoad()/settingsSaveThresholds().)
// =======================================================================

extern float DISTANCE_ON_CM;     // ultrasonic: presence ON  when distance <= this
extern float DISTANCE_OFF_CM;    // ultrasonic: presence OFF when distance >= this
extern int  LDR_THRESHOLD_ON;    // averaged raw value <= this => dark  (AUTO light turns ON)
extern int  LDR_THRESHOLD_OFF;   // averaged raw value >= this => bright (AUTO light turns OFF)
extern bool AUTO_LIGHT_ENABLED;
extern bool AUTO_FAN_ENABLED;
extern bool BUZZER_ENABLED;
extern int  BUZZER_VOLUME;       // 0 (silent) .. 100 (loudest) — see buzzer.cpp

// Loads the four thresholds from NVS (falling back to the config.h
// defaults if never saved). Call once at boot, after prefs.begin()
// (devicesInit() does that).
void settingsLoad();

// Persists the four thresholds to NVS (only keys whose value changed
// are actually written).
void settingsSaveThresholds();

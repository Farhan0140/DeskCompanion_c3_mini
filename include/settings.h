#pragma once
// =======================================================================
// SETTINGS — mutable runtime-tunable values. These are true globals
// shared across translation units, so they're declared `extern` here
// and DEFINED EXACTLY ONCE in settings.cpp. (Unlike config.h's
// compile-time constants, these could later be changed at runtime,
// e.g. from a Settings menu screen or a Firebase command.)
// =======================================================================

extern int  LDR_THRESHOLD_ON;    // averaged raw value <= this => dark  (AUTO light turns ON)
extern int  LDR_THRESHOLD_OFF;   // averaged raw value >= this => bright (AUTO light turns OFF)
extern bool AUTO_LIGHT_ENABLED;
extern bool AUTO_FAN_ENABLED;
extern bool BUZZER_ENABLED;
extern int  BUZZER_VOLUME;       // 0 (silent) .. 100 (loudest) — see buzzer.cpp
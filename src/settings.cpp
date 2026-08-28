#include "settings.h"

// A gap between these two (rather than one shared threshold) is the
// hysteresis band described in sensors.cpp — widen it if you still see
// occasional flicker right at dusk/dawn light levels, narrow it if the
// light feels sluggish to react. Values are on the averaged 0-4095 ADC
// scale, not a single raw reading.
int  LDR_THRESHOLD_ON   = 1400;   // <= this (averaged) => dark
int  LDR_THRESHOLD_OFF  = 1600;   // >= this (averaged) => bright
bool AUTO_LIGHT_ENABLED = true;
bool AUTO_FAN_ENABLED   = true;
bool BUZZER_ENABLED     = true;
int  BUZZER_VOLUME      = 60;   // 0-100, tune to taste; see buzzer.cpp
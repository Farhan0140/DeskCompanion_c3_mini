#pragma once
// =======================================================================
// GLOBALS — hardware objects shared across multiple modules (the OLED
// display and the NVS Preferences handle). Declared extern here,
// defined exactly once in globals.cpp.
// =======================================================================
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include "config.h"

extern Adafruit_SSD1306 display;
extern Preferences prefs;

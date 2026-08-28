#include <Arduino.h>
#include "buzzer.h"
#include "config.h"
#include "settings.h"

struct ToneStep { int freq; unsigned long dur; };

#define MAX_STEPS 12
static ToneStep g_seq[MAX_STEPS];
static uint8_t g_seqLen = 0;
static uint8_t g_seqIdx = 0;
static unsigned long g_stepStart = 0;
static bool g_seqPlaying = false;
static bool g_seqLoop = false;

// =======================================================================
// VOLUME CONTROL
// tone()/noTone() drive a fixed-amplitude square wave — there's no
// volume knob in that API. Real volume control on a passive piezo
// buzzer means controlling the PWM *duty cycle* directly via the
// ESP32's LEDC peripheral: 50% duty = loudest, duty near 0% = quiet.
// BUZZER_VOLUME (0-100, in settings.h/.cpp) maps onto that duty range.
//
// Arduino-ESP32 core 3.x uses a pin-based LEDC API (ledcAttach /
// ledcWrite(pin, ...)); core 2.x used a channel-based API (ledcSetup /
// ledcAttachPin / ledcWrite(channel, ...)). This is written to build
// against either, via ESP_ARDUINO_VERSION_MAJOR.
// =======================================================================
static constexpr int LEDC_RESOLUTION_BITS = 8; // duty range 0-255

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  static inline void ledcAttachBuzzer() { ledcAttach(PIN_BUZZER, 2000, LEDC_RESOLUTION_BITS); }
  static inline void ledcToneWrite(int freq) { ledcWriteTone(PIN_BUZZER, freq); }
  static inline void ledcDutyWrite(uint8_t duty) { ledcWrite(PIN_BUZZER, duty); }
#else
  #define LEDC_BUZZER_CHANNEL 0
  static inline void ledcAttachBuzzer() {
    ledcSetup(LEDC_BUZZER_CHANNEL, 2000, LEDC_RESOLUTION_BITS);
    ledcAttachPin(PIN_BUZZER, LEDC_BUZZER_CHANNEL);
  }
  static inline void ledcToneWrite(int freq) { ledcWriteTone(LEDC_BUZZER_CHANNEL, freq); }
  static inline void ledcDutyWrite(uint8_t duty) { ledcWrite(LEDC_BUZZER_CHANNEL, duty); }
#endif

// Cap at ~50% duty (127/255): duty further from 50% starts getting
// quieter again (symmetric harmonic content), so mapping 0-100%
// volume onto 0-50% duty gives simple, monotonic loudness control.
static inline uint8_t dutyFromVolume() {
  int vol = constrain(BUZZER_VOLUME, 0, 100);
  return (uint8_t)map(vol, 0, 100, 0, 127);
}

static void playTone(int freq) {
  if (freq <= 0) { ledcDutyWrite(0); return; }
  ledcToneWrite(freq);
  ledcDutyWrite(dutyFromVolume());
}

void buzzerInit() {
  ledcAttachBuzzer();
  ledcDutyWrite(0);
}

void buzzerSetVolume(uint8_t percent) {
  BUZZER_VOLUME = constrain((int)percent, 0, 100);
  // If a note is playing right now, re-apply immediately so a live
  // volume change (e.g. from a future Settings menu) is audible at once.
  if (g_seqPlaying && g_seq[g_seqIdx].freq > 0) ledcDutyWrite(dutyFromVolume());
}

static void buzzerPlay(const ToneStep *steps, uint8_t len, bool loop = false) {
  if (!BUZZER_ENABLED) return;
  len = min(len, (uint8_t)MAX_STEPS);
  for (uint8_t i = 0; i < len; i++) g_seq[i] = steps[i];
  g_seqLen = len;
  g_seqIdx = 0;
  g_seqPlaying = true;
  g_seqLoop = loop;
  g_stepStart = millis();
  playTone(g_seq[0].freq);
}

void buzzerStop() {
  g_seqPlaying = false;
  ledcDutyWrite(0);
}

void buzzerUpdate(unsigned long now) {
  if (!g_seqPlaying) return;
  if (now - g_stepStart >= g_seq[g_seqIdx].dur) {
    g_seqIdx++;
    if (g_seqIdx >= g_seqLen) {
      if (g_seqLoop) g_seqIdx = 0;
      else { buzzerStop(); return; }
    }
    g_stepStart = now;
    playTone(g_seq[g_seqIdx].freq);
  }
}

// ---- Preset feedback sounds ----
void buzzerClick()   { ToneStep s[] = {{2000, 40}};                       buzzerPlay(s, 1); }
void buzzerOk()      { ToneStep s[] = {{2200, 40}, {0, 20}, {2600, 60}};  buzzerPlay(s, 3); }
void buzzerError()   { ToneStep s[] = {{500, 100}, {0, 30}, {300, 150}};  buzzerPlay(s, 3); }
void buzzerBoot()    { ToneStep s[] = {{1500, 60}, {1900, 60}, {2400, 90}}; buzzerPlay(s, 3); }
void buzzerTaskDone(){ ToneStep s[] = {{1800,60},{2200,60},{2600,90}};    buzzerPlay(s, 3); }
void buzzerTimerAlarm() {
  ToneStep s[] = {{2500, 200}, {0, 100}, {2500, 200}, {0, 100}};
  buzzerPlay(s, 4, true); // loops until buzzerStop() is called (e.g. on OK)
}
// Short, distinct "connected" chirp — plays once when WiFi finishes
// associating in the background, whenever that happens.
void buzzerWifiConnected() { ToneStep s[] = {{2800, 50}, {0, 30}, {3400, 70}}; buzzerPlay(s, 3); }
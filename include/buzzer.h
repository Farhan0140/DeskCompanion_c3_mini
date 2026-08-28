#pragma once
// =======================================================================
// BUZZER MODULE — non-blocking tone sequences. Declarations only;
// see buzzer.cpp for implementation. The sequencer's internal step
// state is private to buzzer.cpp (nothing else needs it).
// =======================================================================
#include <Arduino.h>

void buzzerInit();
void buzzerUpdate(unsigned long now);
void buzzerStop();
void buzzerSetVolume(uint8_t percent); // 0 (silent) .. 100 (loudest)

// Preset feedback sounds
void buzzerClick();
void buzzerOk();
void buzzerError();
void buzzerBoot();
void buzzerTaskDone();
void buzzerTimerAlarm();
void buzzerWifiConnected();
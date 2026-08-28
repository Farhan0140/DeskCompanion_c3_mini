#pragma once
// =======================================================================
// INPUTS MODULE — Buttons (debounced, hold-to-repeat) + IR remote.
// Declarations only; see inputs.cpp for implementation.
// =======================================================================
#include <Arduino.h>

enum InputEvent { EV_NONE, EV_UP, EV_DOWN, EV_OK, EV_BACK, EV_HOME };

// Result of the most recent inputsUpdate() call, and when any input
// last happened (used for the idle timeout).
extern InputEvent g_lastEvent;
extern unsigned long g_lastInputAt;

void inputsInit();
void inputsUpdate(unsigned long now);

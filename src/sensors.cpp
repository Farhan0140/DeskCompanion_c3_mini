#include <Arduino.h>
#include "sensors.h"
#include "config.h"
#include "settings.h"

SensorState S;

// =======================================================================
// LDR SMOOTHING: moving average + hysteresis
// Raw analogRead() on an LDR is noisy — reading it directly and
// comparing to one threshold (the old approach) makes the relay
// chatter every time a reading jitters across the line. Two techniques
// stacked together fix that:
//   1. Moving average over LDR_SAMPLE_COUNT samples (config.h) — a
//      fixed-size ring buffer with a running sum, so it's O(1) per
//      reading no matter how large the window is.
//   2. Hysteresis band (LDR_THRESHOLD_ON / LDR_THRESHOLD_OFF, with a
//      gap between them) on top of the averaged value — the same
//      pattern already used for the ultrasonic presence trigger, so
//      even a real light level sitting right at the edge can't cause
//      rapid on/off toggling.
// =======================================================================
static int g_ldrSamples[LDR_SAMPLE_COUNT];
static uint8_t g_ldrSampleIdx = 0;
static long g_ldrSampleSum = 0;

void sensorsInit() {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_LDR, INPUT);

  S.distanceMinCm = 999.0f;
  S.distanceMaxCm = 0.0f;

  // Prime the moving-average buffer with a real reading so it doesn't
  // start averaged against zeros (which would read as "very dark"
  // for the first LDR_SAMPLE_COUNT update cycles after boot).
  int first = analogRead(PIN_LDR);
  for (uint8_t i = 0; i < LDR_SAMPLE_COUNT; i++) g_ldrSamples[i] = first;
  g_ldrSampleSum = (long)first * LDR_SAMPLE_COUNT;
  S.ldrRaw = first;
}

static float readDistanceCm() {
  noInterrupts();
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duration = pulseIn(PIN_ECHO, HIGH, 15000UL); // 15ms timeout max (~2.5m range)
  interrupts();

  if (duration == 0) return 999.0f; // No object / out of range

  return (float)duration * 0.0343f / 2.0f;
}

// Feeds one new raw ADC reading into the ring buffer and returns the
// updated moving average. Constant-time regardless of window size.
static int ldrPushSample(int raw) {
  g_ldrSampleSum -= g_ldrSamples[g_ldrSampleIdx];
  g_ldrSamples[g_ldrSampleIdx] = raw;
  g_ldrSampleSum += raw;
  g_ldrSampleIdx = (g_ldrSampleIdx + 1) % LDR_SAMPLE_COUNT;
  return (int)(g_ldrSampleSum / LDR_SAMPLE_COUNT);
}

static uint8_t g_missCount = 0;

void sensorsUpdate(unsigned long now) {
  // Polled every US_INTERVAL_MS (300ms in config.h) so sound echoes die down completely
  if (now - S.lastUsPoll >= US_INTERVAL_MS) {
    S.lastUsPoll = now;
    float dist = readDistanceCm();

    if (dist < 900.0f) {
      // Valid distance reading — clear miss counter & track min/max
      g_missCount = 0;
      if (dist < S.distanceMinCm) S.distanceMinCm = dist;
      if (dist > S.distanceMaxCm) S.distanceMaxCm = dist;

      if (S.distanceCm >= 900.0f) {
        S.distanceCm = dist;
      } else {
        S.distanceCm = (S.distanceCm * 0.6f) + (dist * 0.4f);
      }
    } else {
      // Missed echo / transient timeout — increment miss counter
      g_missCount++;
      // Require 3 consecutive misses (~900ms of continuous no-echo) before setting distance to 999
      if (g_missCount >= 3) {
        S.distanceCm = 999.0f;
      }
      // If missCount < 3, S.distanceCm remains at its previous valid reading, preventing relay chatter
    }

    if (!S.presence && S.distanceCm <= DISTANCE_ON_CM)  S.presence = true;
    if (S.presence  && S.distanceCm >= DISTANCE_OFF_CM) S.presence = false;
  }

  if (now - S.lastLdrPoll >= LDR_INTERVAL_MS) {
    S.lastLdrPoll = now;
    int raw = analogRead(PIN_LDR);          // 0-4095 on ESP32 12-bit ADC
    S.ldrRaw = ldrPushSample(raw);           // smoothed (moving-average) value

    // Hysteresis on top of the averaged reading. Assumes a lower raw
    // value means darker (typical LDR voltage-divider wiring) — if
    // your module reads the opposite way, swap ON/OFF and their
    // comparison operators here.
    if (!S.isDark && S.ldrRaw <= LDR_THRESHOLD_ON)  S.isDark = true;
    if (S.isDark  && S.ldrRaw >= LDR_THRESHOLD_OFF) S.isDark = false;
  }
}
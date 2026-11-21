// =======================================================================
// FACE MODULE — implementation.
// Smooth, non-blocking eye/mouth renderer. Instead of swapping between
// fixed ASCII-art frames, every visual parameter (pupil offset, eyelid
// openness, eye size, mouth curve) is a float that EASES toward a
// target value once per frame. That's what makes it read as "alive"
// instead of a slideshow.
// =======================================================================
#include <Arduino.h>
#include <math.h>
#include "face.h"
#include "config.h"
#include "globals.h"
#include "ble_control.h"

FaceAnim F;
bool g_menuActive = false;

// Easing speed: fraction of remaining distance closed per ~20ms tick.
// Higher = snappier, lower = dreamier. Different channels get
// different speeds so motion doesn't look uniformly robotic.
static constexpr float EASE_POS   = 0.18f;
static constexpr float EASE_SIZE  = 0.12f;
static constexpr float EASE_LID   = 0.35f;   // blinks/winks should be quick
static constexpr float EASE_TILT  = 0.10f;
static constexpr float EASE_MOUTH = 0.15f;

static inline float ease(float cur, float target, float k) {
  return cur + (target - cur) * k;
}

void faceSetGaze(GazeDir dir) {
  switch (dir) {
    case GAZE_LEFT:   F.pupilX_t = -10; F.pupilY_t = 0;  break;
    case GAZE_RIGHT:  F.pupilX_t = 10;  F.pupilY_t = 0;  break;
    case GAZE_UP:     F.pupilX_t = 0;   F.pupilY_t = -8; break;
    case GAZE_DOWN:   F.pupilX_t = 0;   F.pupilY_t = 8;  break;
    default:           F.pupilX_t = 0;  F.pupilY_t = 0;  break;
  }
}

void faceBlink() {
  F.blinking = true;
  F.blinkStart = millis();
}

void faceWink(bool leftEye) {
  F.winking = true;
  F.winkLeftEye = leftEye;
  F.winkStart = millis();
}

void faceSetExpression(EyeExpression e, MouthExpression m) {
  F.expr = e; F.mouthExpr = m;

  // Reset per-expression targets; each case sets what differs.
  F.eyeW_t = 42; F.eyeH_t = 42;
  F.squashL_t = 1.0f; F.squashR_t = 1.0f;
  F.lTilt_t = 0; F.rTilt_t = 0;
  F.browRaise_t = 0;
  F.mouthWidth_t = 30; F.mouthOpen_t = 0.15f; F.mouthSmile_t = 0;

  switch (e) {
    case EYE_HAPPY:
      F.eyeH_t = 26; F.lTilt_t = 0; F.rTilt_t = 0; break;
    case EYE_SLEEPY:
      F.eyeH_t = 14; F.lTilt_t = -8; F.rTilt_t = 8; break;
    case EYE_SURPRISED:
      F.eyeW_t = 50; F.eyeH_t = 50; F.browRaise_t = 6; break;
    case EYE_ANGRY:
      F.eyeH_t = 34; F.lTilt_t = 18; F.rTilt_t = -18; break;
    case EYE_CONFUSED:
      F.lTilt_t = 14; F.rTilt_t = -6; F.eyeH_t = 38; break;
    case EYE_SAD:
      F.lTilt_t = -14; F.rTilt_t = 14; F.eyeH_t = 34; F.browRaise_t = 3; break;
    case EYE_WOW:
      // Bigger and rounder than plain SURPRISED — full amazement.
      F.eyeW_t = 56; F.eyeH_t = 56; F.browRaise_t = 9; break;
    case EYE_CRYING:
      // Sad-shaped eyes; drawTears() below adds the actual tear drops.
      F.lTilt_t = -14; F.rTilt_t = 14; F.eyeH_t = 30; F.browRaise_t = 3; break;
    case EYE_HEADACHE:
      // Squinting + furrowed brow (same converging tilt as angry, but
      // milder), plus a lowered brow via negative browRaise; drawPainMarks()
      // below adds the little throbbing "spark" marks above the eyes.
      F.eyeH_t = 20; F.lTilt_t = 12; F.rTilt_t = -12; F.browRaise_t = -2; break;
    case EYE_LAUGHING:
      // Squeezed almost shut with a slight upward curve; the actual
      // shaking motion is added in drawEyes()/drawMouth() below.
      F.eyeH_t = 8; F.lTilt_t = 6; F.rTilt_t = -6; break;

    // ---- new expressions. For the custom-drawn ones (heart, star,
    // spiral, shades, arcs) eyeW/eyeH just set the eased drawing size;
    // see drawSpecialEyes() below.
    case EYE_LOVE:       F.eyeW_t = 34; F.eyeH_t = 34; break;
    case EYE_STAR:       F.eyeW_t = 38; F.eyeH_t = 38; break;
    case EYE_DIZZY:      F.eyeW_t = 30; F.eyeH_t = 30; break;
    case EYE_SUSPICIOUS: F.eyeH_t = 38; F.squashL_t = 0.35f; F.squashR_t = 0.7f; F.lTilt_t = 8; break;
    case EYE_COOL:       F.eyeW_t = 42; F.eyeH_t = 22; break; // lens size
    case EYE_SHY:        F.eyeW_t = 32; F.eyeH_t = 28; break; // + blush, drawn in drawEyes()
    case EYE_BORED:      F.eyeH_t = 40; F.squashL_t = 0.4f; F.squashR_t = 0.4f; break; // + heavy lid line
    case EYE_THINKING:   F.eyeH_t = 38; F.squashR_t = 0.8f; F.browRaise_t = 2; break; // + "..." dots
    case EYE_SCARED:     F.eyeW_t = 28; F.eyeH_t = 36; F.browRaise_t = 5; break; // + tremble
    case EYE_KAWAII:     F.eyeW_t = 30; F.eyeH_t = 30; break;
    case EYE_CONTENT:    F.eyeW_t = 30; F.eyeH_t = 30; break;
    default: break; // EYE_NORMAL
  }

  switch (m) {
    case MOUTH_HAPPY:     F.mouthSmile_t = 1.0f;  F.mouthOpen_t = 0.5f; F.mouthWidth_t = 34; break;
    case MOUTH_SMILE:     F.mouthSmile_t = 0.6f;  F.mouthOpen_t = 0.1f; break;
    case MOUTH_SURPRISED: F.mouthSmile_t = 0.0f;  F.mouthOpen_t = 0.9f; F.mouthWidth_t = 14; break;
    case MOUTH_SAD:       F.mouthSmile_t = -0.8f; F.mouthOpen_t = 0.3f; break;
    case MOUTH_TALK:      F.mouthSmile_t = 0.1f;  F.mouthOpen_t = 0.6f; break;
    case MOUTH_LAUGH:     F.mouthSmile_t = 0.3f;  F.mouthOpen_t = 1.0f; F.mouthWidth_t = 30; break;
    case MOUTH_CRY:       F.mouthSmile_t = -0.9f; F.mouthOpen_t = 0.35f; break;
    case MOUTH_GRIMACE:   F.mouthSmile_t = -0.3f; F.mouthOpen_t = 0.25f; F.mouthWidth_t = 22; break;
    case MOUTH_SMIRK:     F.mouthSmile_t = 0.8f;  F.mouthOpen_t = 0.0f; F.mouthWidth_t = 24; break;
    case MOUTH_WAVY:      F.mouthSmile_t = 0.0f;  F.mouthOpen_t = 0.0f; F.mouthWidth_t = 30; break;
    case MOUTH_FLAT:      F.mouthSmile_t = 0.0f;  F.mouthOpen_t = 0.0f; F.mouthWidth_t = 22; break;
    case MOUTH_TONGUE:    F.mouthSmile_t = 0.7f;  F.mouthOpen_t = 0.15f; F.mouthWidth_t = 28; break;
    default:               F.mouthSmile_t = 0.0f;  F.mouthOpen_t = 0.12f; break;
  }
}

void faceInit() {
  F.lastFrameMs = millis();
  F.nextBlinkAt = millis() + random(2000, 5000);
  F.idleStateChangeAt = millis() + random(2500, 5000);
  faceSetExpression(EYE_NORMAL, MOUTH_NEUTRAL);
}

// ---- Contextual events (WiFi connecting, fan/light, first boot,
// server health, BLE counter button) — each takes over expr/mouth/icon for a while, then
// hands control back to the idle personality loop below. Plain
// globals are enough here (same reasoning as D in devices.h): these
// are simple scalars, some are set from core 0 (netsync.cpp's
// onWifiConnected running in netTask) and read/cleared from core 1
// (faceUpdate in the main loop) — a stale read for one frame is
// harmless for a face icon.
static constexpr unsigned long FACE_EVENT_MS = 3000; // how long a transient event holds the face
static bool g_eventActive = false;
static bool g_wifiConnecting = false;
static FaceEvent g_activeEvent = FACE_EVENT_BOOT;
static unsigned long g_eventUntil = 0;
static const char *g_caption = nullptr; // small text label shown top-left during some events

// How long each transient event holds the face.
static unsigned long eventDurationMs(FaceEvent ev) {
  switch (ev) {
    case FACE_EVENT_BLE_COUNTED:  return 1200;  // fires on every press - keep it snappy
    case FACE_EVENT_BLE_TARGET:   return 4000;
    case FACE_EVENT_SERVER_OK:
    case FACE_EVENT_SERVER_DOWN:  return 5000;
    default:                      return FACE_EVENT_MS;
  }
}

static void applyEventVisual(FaceEvent ev) {
  g_caption = nullptr;
  switch (ev) {
    case FACE_EVENT_BOOT:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_HAPPY, MOUTH_HAPPY);
      F.icon = ICON_HANDSHAKE;
      break;
    case FACE_EVENT_WIFI_CONNECTING:
      faceSetGaze(GAZE_UP);
      faceSetExpression(EYE_CONFUSED, MOUTH_NEUTRAL);
      F.icon = ICON_WIFI;
      break;
    case FACE_EVENT_FAN_ON:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_SLEEPY, MOUTH_SMILE); // relaxed, half-lidded
      F.icon = ICON_AIRFLOW;
      break;
    case FACE_EVENT_FAN_OFF:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_NORMAL, MOUTH_GRIMACE); // awkward "phew"
      F.icon = ICON_SWEAT;
      break;
    case FACE_EVENT_LIGHT_ON:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_HAPPY, MOUTH_HAPPY);
      F.icon = ICON_NONE;
      break;
    case FACE_EVENT_LIGHT_OFF:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_SLEEPY, MOUTH_NEUTRAL);
      F.icon = ICON_MOON;
      break;

    // ---- server health check ----
    case FACE_EVENT_SERVER_OK:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_CONTENT, MOUTH_SMILE);
      F.icon = ICON_CHECK;
      g_caption = "SERVER OK";
      break;
    case FACE_EVENT_SERVER_DOWN:
      faceSetGaze(GAZE_DOWN);
      faceSetExpression(EYE_SAD, MOUTH_SAD);
      F.icon = ICON_CROSS;
      g_caption = "SERVER DOWN";
      break;

    // ---- BLE counter button ----
    case FACE_EVENT_BLE_CONNECTED:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_HAPPY, MOUTH_HAPPY);
      F.icon = ICON_BT;
      g_caption = "CONNECTED";
      break;
    case FACE_EVENT_BLE_DISCONNECTED:
      faceSetGaze(GAZE_DOWN);
      faceSetExpression(EYE_SAD, MOUTH_SAD);
      F.icon = ICON_BT_OFF;
      g_caption = "DISCONNECTED";
      break;
    case FACE_EVENT_BLE_COUNTED:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_HAPPY, MOUTH_SMILE);
      F.icon = ICON_BT;
      g_caption = "+1";
      break;
    case FACE_EVENT_BLE_TARGET:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_STAR, MOUTH_HAPPY);
      F.icon = ICON_BELL;
      g_caption = "DONE!";
      break;
    case FACE_EVENT_BLE_ALREADY_DONE:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_BORED, MOUTH_FLAT);
      F.icon = ICON_CHECK;
      g_caption = "ALREADY DONE";
      break;
    case FACE_EVENT_BLE_NO_TASK:
      faceSetGaze(GAZE_UP);
      faceSetExpression(EYE_CONFUSED, MOUTH_NEUTRAL);
      F.icon = ICON_BT;
      g_caption = "NO TASK";
      break;
    case FACE_EVENT_BLE_NOT_RECEIVED:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_SAD, MOUTH_GRIMACE);
      F.icon = ICON_CROSS;
      g_caption = "NOT SENT";
      break;
    case FACE_EVENT_BLE_OFFLINE:
      faceSetGaze(GAZE_CENTER);
      faceSetExpression(EYE_CONFUSED, MOUTH_SAD);
      F.icon = ICON_BT_OFF;
      g_caption = "NO PHONE";
      break;
  }
}

void faceShowEvent(FaceEvent ev) {
  if (ev == FACE_EVENT_WIFI_CONNECTING) g_wifiConnecting = true;
  g_activeEvent = ev;
  g_eventActive = true;
  g_eventUntil = millis() + eventDurationMs(ev); // ignored for WIFI_CONNECTING, see faceServiceEvent()
  applyEventVisual(ev);
}

// Like faceShowEvent(FACE_EVENT_WIFI_CONNECTING), but won't cut off a
// transient event that's already on screen (e.g. the FACE_EVENT_BOOT
// hello at power-on) — it just marks WiFi as connecting and lets
// faceServiceEvent() pick it up once that event's window ends. If
// nothing else is currently showing (e.g. a later mid-session
// reconnect), it takes over immediately, same as faceShowEvent().
void faceStartWifiConnecting() {
  g_wifiConnecting = true;
  if (g_eventActive) return; // something else has priority right now
  g_activeEvent = FACE_EVENT_WIFI_CONNECTING;
  g_eventActive = true;
  applyEventVisual(FACE_EVENT_WIFI_CONNECTING);
}

void faceClearWifiConnecting() {
  g_wifiConnecting = false;
  if (g_activeEvent == FACE_EVENT_WIFI_CONNECTING) {
    g_eventActive = false; // hand back to idle loop next tick
    F.icon = ICON_NONE;
    g_caption = nullptr;
  }
}

// Expires transient events on the wall clock (runs even while a menu
// is open, so an event doesn't linger stale once you back out) and
// re-shows FACE_EVENT_WIFI_CONNECTING if it's still in progress once
// whatever transient event was on top of it finishes.
static void faceServiceEvent(unsigned long now) {
  if (!g_eventActive) return;
  if (g_activeEvent == FACE_EVENT_WIFI_CONNECTING) return; // persistent — cleared explicitly
  if (now < g_eventUntil) return;

  if (g_wifiConnecting) {
    g_activeEvent = FACE_EVENT_WIFI_CONNECTING;
    applyEventVisual(FACE_EVENT_WIFI_CONNECTING);
  } else {
    g_eventActive = false;
    F.icon = ICON_NONE;
    g_caption = nullptr;
  }
}

static void faceRunIdlePersonality(unsigned long now) {
  if (g_menuActive || g_eventActive) return;

  // Picks a random gaze + expression when nothing else is driving the
  // face: the original 9 states (0-8, unchanged) plus the 10 newer
  // expressions (9-18), never the same state twice in a row. CRYING and HEADACHE are
  // intentionally NOT part of the spontaneous idle loop — a desk
  // companion randomly crying for no reason would be a strange UX.
  // Trigger those two (and a wink) yourself from wherever makes sense
  // in your project, e.g.:
  //   faceSetExpression(EYE_CRYING,   MOUTH_CRY);      // something went wrong
  //   faceSetExpression(EYE_HEADACHE, MOUTH_GRIMACE);  // repeated errors
  //   faceWink(true);                                   // a little easter egg
  if (now >= F.idleStateChangeAt) {
    static constexpr uint8_t IDLE_STATE_COUNT = 19;
    uint8_t next;
    do { next = (uint8_t)random(0, IDLE_STATE_COUNT); } while (next == F.idleStep);
    F.idleStep = next;
    switch (F.idleStep) {
      case 0: faceSetGaze(GAZE_CENTER);              faceSetExpression(EYE_NORMAL,    MOUTH_NEUTRAL); break;
      case 1: faceSetGaze((GazeDir)random(1, 5));     faceSetExpression(EYE_CONFUSED,  MOUTH_NEUTRAL); break; // curious glance
      case 2: faceSetGaze(GAZE_CENTER);               faceSetExpression(EYE_NORMAL,    MOUTH_SMILE);   break;
      case 3: faceSetGaze(GAZE_CENTER);               faceSetExpression(EYE_HAPPY,     MOUTH_HAPPY);   break;
      case 4: faceSetGaze((GazeDir)random(1, 5));     faceSetExpression(EYE_NORMAL,    MOUTH_NEUTRAL); break; // look around
      case 5: faceSetGaze(GAZE_CENTER);               faceSetExpression(EYE_SLEEPY,    MOUTH_SAD);     break; // droopy/yawn-ish
      case 6: faceSetGaze(GAZE_CENTER);               faceSetExpression(EYE_WOW,       MOUTH_SURPRISED); break; // "oh!"
      case 7: faceSetGaze(GAZE_CENTER);               faceSetExpression(EYE_NORMAL,    MOUTH_SMILE);
              faceWink(random(0, 2) == 0);                                                              break; // wink
      case 8: faceSetGaze(GAZE_CENTER);               faceSetExpression(EYE_LAUGHING,  MOUTH_LAUGH);   break; // quick chuckle
      // ---- new ----
      case 9:  faceSetGaze(GAZE_CENTER);              faceSetExpression(EYE_LOVE,       MOUTH_SMILE);  break; // heart eyes
      case 10: faceSetGaze(GAZE_CENTER);              faceSetExpression(EYE_STAR,       MOUTH_HAPPY);  break; // star-struck
      case 11: faceSetGaze(GAZE_CENTER);              faceSetExpression(EYE_DIZZY,      MOUTH_WAVY);   break; // dizzy
      case 12: faceSetGaze(random(0, 2) ? GAZE_LEFT : GAZE_RIGHT);
               faceSetExpression(EYE_SUSPICIOUS, MOUTH_SMIRK);                                          break; // side-eye
      case 13: faceSetGaze(GAZE_CENTER);              faceSetExpression(EYE_COOL,       MOUTH_SMIRK);  break; // sunglasses
      case 14: faceSetGaze(GAZE_DOWN);                faceSetExpression(EYE_SHY,        MOUTH_SMILE);  break; // blushing
      case 15: faceSetGaze(random(0, 2) ? GAZE_LEFT : GAZE_RIGHT);
               faceSetExpression(EYE_BORED,      MOUTH_FLAT);                                           break; // bored
      case 16: faceSetGaze(GAZE_UP); F.pupilX_t = 8;  faceSetExpression(EYE_THINKING,   MOUTH_SMIRK);  break; // hmm...
      case 17: faceSetGaze(GAZE_CENTER);              faceSetExpression(EYE_SCARED,     MOUTH_WAVY);   break; // scared
      case 18: faceSetGaze(GAZE_CENTER);              faceSetExpression(EYE_KAWAII,     MOUTH_TONGUE); break; // ^ ^ :P
    }
    F.idleStateChangeAt = now + random(2200, 4500);
  }
}

void faceUpdate(unsigned long now) {
  if (now - F.lastFrameMs < FRAME_INTERVAL_MS) return;
  float dtScale = (now - F.lastFrameMs) / (float)FRAME_INTERVAL_MS;
  F.lastFrameMs = now;

  faceServiceEvent(now);
  faceRunIdlePersonality(now);

  // ---- Two-eye blink scheduling (spontaneous, like real blink cadence) ----
  if (!F.blinking && now >= F.nextBlinkAt) {
    faceBlink();
    F.nextBlinkAt = now + random(2500, 6000);
    // occasional double-blink for personality
    if (random(0, 5) == 0) F.nextBlinkAt = now + random(180, 260);
  }
  float blinkTarget = 1.0f;
  if (F.blinking) {
    unsigned long t = now - F.blinkStart;
    // ease-in-out over ~260ms: close (0-110ms), hold(110-140ms), open(140-260ms)
    if (t < 110)      blinkTarget = 1.0f - (t / 110.0f);
    else if (t < 140)  blinkTarget = 0.0f;
    else if (t < 260)  blinkTarget = (t - 140) / 120.0f;
    else { blinkTarget = 1.0f; F.blinking = false; }
  }
  F.lidOpenL_t = blinkTarget;
  F.lidOpenR_t = blinkTarget;

  // ---- One-eye wink (overrides just its eye's target, on top of the
  // blink assignment above — a wink mid-blink just looks like a
  // slightly longer close on that side, which reads fine) ----
  if (F.winking) {
    unsigned long t = now - F.winkStart;
    float target;
    if (t < 90)       target = 1.0f - (t / 90.0f);
    else if (t < 130)  target = 0.0f;
    else if (t < 260)  target = (t - 130) / 130.0f;
    else { target = 1.0f; F.winking = false; }
    if (F.winkLeftEye) F.lidOpenL_t = target;
    else                F.lidOpenR_t = target;
  }

  // ---- Breathing / idle micro-motion: subtle sine-wave pulsing ----
  // Eyes and mouth share the same phase clock but use different wave
  // shapes/speeds/offsets so they don't move in obvious lock-step.
  F.breathPhase += 0.02f * dtScale;
  float breatheEye   = sinf(F.breathPhase) * 0.6f;
  float breatheMouth = cosf(F.breathPhase * 1.3f) * 0.06f;

  // ---- Tears: two independent looping phases (0..1), offset so the
  // drops don't fall in perfect lockstep. Advances continuously (cheap)
  // but is only drawn while EYE_CRYING is active.
  F.tearPhaseL = fmodf(F.tearPhaseL + 0.018f * dtScale, 1.0f);
  F.tearPhaseR = fmodf(F.tearPhaseR + 0.018f * dtScale, 1.0f);

  // ---- Ease every channel toward its target ----
  F.eyeW    = ease(F.eyeW,    F.eyeW_t + breatheEye, EASE_SIZE);
  F.eyeH    = ease(F.eyeH,    F.eyeH_t + breatheEye, EASE_SIZE);
  F.lidOpenL = ease(F.lidOpenL, F.lidOpenL_t, EASE_LID);
  F.lidOpenR = ease(F.lidOpenR, F.lidOpenR_t, EASE_LID);
  F.squashL  = ease(F.squashL,  F.squashL_t,  EASE_SIZE);
  F.squashR  = ease(F.squashR,  F.squashR_t,  EASE_SIZE);
  F.pupilX  = ease(F.pupilX,  F.pupilX_t,         EASE_POS);
  F.pupilY  = ease(F.pupilY,  F.pupilY_t,         EASE_POS);
  F.lTiltDeg = ease(F.lTiltDeg, F.lTilt_t,        EASE_TILT);
  F.rTiltDeg = ease(F.rTiltDeg, F.rTilt_t,        EASE_TILT);
  F.browRaise = ease(F.browRaise, F.browRaise_t,  EASE_TILT);
  F.mouthOpen  = ease(F.mouthOpen,  F.mouthOpen_t + breatheMouth, EASE_MOUTH);
  F.mouthWidth = ease(F.mouthWidth, F.mouthWidth_t, EASE_MOUTH);
  F.mouthSmile = ease(F.mouthSmile, F.mouthSmile_t, EASE_MOUTH);
}

// ---- Drawing helpers -------------------------------------------------
static inline void drawOneEye(int cx, int cy, float lid) {
  float h = max(2.0f, F.eyeH * lid);
  float w = F.eyeW;
  int x0 = cx - w / 2;
  int y0 = cy - h / 2;

  // Rounded-rect "screen" eye, radius scales with size for a soft look
  int radius = min(w, h) * 0.35f;
  display.fillRoundRect(x0, y0, (int)w, (int)h, radius, SSD1306_WHITE);
}

static inline void drawEyelidTilt(int cx, int cy, float tiltDeg, float h, float w) {
  // Draws a black triangular eyelid wedge over the top of the eye to
  // fake angry/sad/confused/headache tilt on a filled rounded rect
  // (cheap but reads clearly at 128x64).
  if (fabsf(tiltDeg) < 1.0f) return;
  int y0 = cy - h / 2;
  int riseAmt = (int)(tanf(radians(fabsf(tiltDeg))) * w);
  riseAmt = constrain(riseAmt, 0, (int)h);
  int16_t xs[3], ys[3];
  if (tiltDeg > 0) { // inner corner higher
    xs[0] = cx - w/2; ys[0] = y0 - 4;
    xs[1] = cx + w/2; ys[1] = y0 - 4;
    xs[2] = cx - w/2; ys[2] = y0 + riseAmt;
  } else {
    xs[0] = cx - w/2; ys[0] = y0 - 4;
    xs[1] = cx + w/2; ys[1] = y0 - 4;
    xs[2] = cx + w/2; ys[2] = y0 + riseAmt;
  }
  display.fillTriangle(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], SSD1306_BLACK);
}

// Small falling teardrop below one eye, looping continuously via a
// 0..1 phase. Only called while EYE_CRYING is active.
static inline void drawTear(int cx, int eyeBottomY, float phase) {
  constexpr int TRAVEL_PX = 20;
  int y = eyeBottomY + 2 + (int)(phase * TRAVEL_PX);
  if (y > SCREEN_HEIGHT - 1) return; // off the bottom edge this loop
  display.fillCircle(cx, y, 2, SSD1306_WHITE);
}

// A couple of small throbbing "spark" marks above the eyes, blinking
// on/off for a headache/pain feel. Only called while EYE_HEADACHE is active.
static inline void drawPainMarks(int midCx, int cy) {
  if ((millis() / 280) % 2 == 0) return; // throb: visible half the time
  int y0 = cy - 20;
  display.drawLine(midCx - 9, y0,     midCx - 4, y0 + 6, SSD1306_WHITE);
  display.drawLine(midCx - 4, y0 + 6, midCx - 9, y0 + 11, SSD1306_WHITE);
  display.drawLine(midCx + 9, y0,     midCx + 4, y0 + 6, SSD1306_WHITE);
  display.drawLine(midCx + 4, y0 + 6, midCx + 9, y0 + 11, SSD1306_WHITE);
}

// Quick side-to-side shake used for EYE_LAUGHING ("very loud laughter"),
// applied to both eyes and the mouth together so the whole face shakes.
static inline int laughShakeX() {
  if (F.expr != EYE_LAUGHING) return 0;
  return (int)(sinf(millis() * 0.03f) * 2.5f);
}

// ---- Custom-shaped eyes for the newer expressions -------------------
static void drawHeart(int cx, int cy, float size) {
  int r = max(2, (int)(size / 4));
  int top = cy - r / 2;
  display.fillCircle(cx - r, top, r, SSD1306_WHITE);
  display.fillCircle(cx + r, top, r, SSD1306_WHITE);
  display.fillTriangle(cx - 2 * r, top + 1, cx + 2 * r, top + 1, cx, top + 2 * r + r / 2 + 2, SSD1306_WHITE);
}

static void drawStar(int cx, int cy, float R, float rot) {
  float r = R * 0.45f;
  int16_t px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = -PI / 2 + rot + i * PI / 5;
    float rad = (i % 2 == 0) ? R : r;
    px[i] = cx + (int)(cosf(a) * rad);
    py[i] = cy + (int)(sinf(a) * rad);
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    display.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], SSD1306_WHITE);
  }
}

static void drawSpiral(int cx, int cy, float R, float spin) {
  const int N = 36;
  const float TURNS = 2.5f;
  int px = cx, py = cy;
  for (int i = 1; i <= N; i++) {
    float t = (float)i / N;
    float a = spin + t * TURNS * 2 * PI;
    int x = cx + (int)(cosf(a) * t * R);
    int y = cy + (int)(sinf(a) * t * R);
    display.drawLine(px, py, x, y, SSD1306_WHITE);
    display.drawLine(px + 1, py, x + 1, y, SSD1306_WHITE); // 2px stroke so it reads at 128x64
    px = x; py = y;
  }
}

// Half-circle arc eyes: up = "^ ^" (kawaii), down = "u u" (content).
static void drawArcEye(int cx, int cy, int R, bool up) {
  for (int r = R - 2; r <= R; r++) {
    if (up) display.drawCircleHelper(cx, cy + R / 2, r, 1 | 2, SSD1306_WHITE);
    else    display.drawCircleHelper(cx, cy - R / 2, r, 4 | 8, SSD1306_WHITE);
  }
}

static void drawShades(int leftCx, int rightCx, int cy) {
  int w = (int)F.eyeW, h = (int)F.eyeH;
  int top = cy - h / 2;
  const int cxs[2] = {leftCx, rightCx};
  for (int i = 0; i < 2; i++) {
    int cx = cxs[i];
    display.fillRoundRect(cx - w / 2, top, w, h, 7, SSD1306_WHITE);
    // glint
    display.drawLine(cx - w / 2 + 6, top + h - 5, cx - w / 2 + 13, top + 3, SSD1306_BLACK);
    display.drawLine(cx - w / 2 + 9, top + h - 5, cx - w / 2 + 16, top + 3, SSD1306_BLACK);
  }
  display.fillRect(leftCx + w / 2, top + 2, (rightCx - leftCx) - w, 3, SSD1306_WHITE); // bridge
  display.drawLine(leftCx - w / 2, top + 3, 0, top + 1, SSD1306_WHITE);                // arms
  display.drawLine(rightCx + w / 2, top + 3, SCREEN_WIDTH - 1, top + 1, SSD1306_WHITE);
}

// Draws the eyes for expressions that don't use the rounded-rect eye at
// all. Returns false for every other expression (incl. all the original
// ones), which then draw exactly as before.
static bool drawSpecialEyes(int leftCx, int rightCx, int cy) {
  unsigned long ms = millis();
  // A normal two-eye blink still reads on shaped eyes as a closed line.
  bool closedL = F.lidOpenL < 0.3f, closedR = F.lidOpenR < 0.3f;
  int w = (int)F.eyeW;
  switch (F.expr) {
    case EYE_LOVE: {
      float pulse = 1.0f + 0.08f * sinf(ms * 0.012f); // heartbeat
      if (closedL) display.drawFastHLine(leftCx - w / 2, cy, w, SSD1306_WHITE); else drawHeart(leftCx, cy, F.eyeW * pulse);
      if (closedR) display.drawFastHLine(rightCx - w / 2, cy, w, SSD1306_WHITE); else drawHeart(rightCx, cy, F.eyeW * pulse);
      return true;
    }
    case EYE_STAR: {
      float R = F.eyeW / 2 * (1.0f + 0.07f * sinf(ms * 0.015f)); // twinkle
      float rot = sinf(ms * 0.003f) * 0.25f;
      if (closedL) display.drawFastHLine(leftCx - w / 2, cy, w, SSD1306_WHITE); else drawStar(leftCx, cy, R, rot);
      if (closedR) display.drawFastHLine(rightCx - w / 2, cy, w, SSD1306_WHITE); else drawStar(rightCx, cy, R, -rot);
      return true;
    }
    case EYE_DIZZY: {
      float spin = ms * 0.008f;
      drawSpiral(leftCx, cy, F.eyeW / 2, spin);
      drawSpiral(rightCx, cy, F.eyeW / 2, -spin); // counter-rotating
      return true;
    }
    case EYE_COOL:
      drawShades(leftCx, rightCx, cy);
      return true;
    case EYE_KAWAII:
      drawArcEye(leftCx, cy, w / 2, true);
      drawArcEye(rightCx, cy, w / 2, true);
      return true;
    case EYE_CONTENT:
      drawArcEye(leftCx, cy, w / 2, false);
      drawArcEye(rightCx, cy, w / 2, false);
      return true;
    default:
      return false;
  }
}

static void drawEyes() {
  int baseY = 26;
  int shake = laughShakeX();
  int tremble = (F.expr == EYE_SCARED) ? (int)random(-1, 2) : 0;
  int leftCx  = SCREEN_WIDTH/2 - 26 + (int)F.pupilX + shake + tremble;
  int rightCx = SCREEN_WIDTH/2 + 26 + (int)F.pupilX + shake + tremble;
  int cy = baseY + (int)F.pupilY - (int)F.browRaise;

  if (drawSpecialEyes(leftCx, rightCx, cy)) return;

  // squash is 1.0 for all original expressions -> identical to before
  float lidL = F.lidOpenL * F.squashL;
  float lidR = F.lidOpenR * F.squashR;
  drawOneEye(leftCx, cy, lidL);
  drawOneEye(rightCx, cy, lidR);
  drawEyelidTilt(leftCx,  cy, F.lTiltDeg, F.eyeH * lidL, F.eyeW);
  drawEyelidTilt(rightCx, cy, F.rTiltDeg, F.eyeH * lidR, F.eyeW);

  if (F.expr == EYE_BORED) {
    // heavy flat upper lid line over each half-closed eye
    int w = (int)F.eyeW;
    int topL = cy - (int)(max(2.0f, F.eyeH * lidL) / 2) - 3;
    int topR = cy - (int)(max(2.0f, F.eyeH * lidR) / 2) - 3;
    display.fillRect(leftCx - w / 2 - 2,  topL, w + 4, 2, SSD1306_WHITE);
    display.fillRect(rightCx - w / 2 - 2, topR, w + 4, 2, SSD1306_WHITE);
  }
  if (F.expr == EYE_SHY) {
    // blush hatching on the outer cheeks
    for (int k = 0; k < 3; k++) {
      int xl = leftCx - 14 + k * 5, xr = rightCx + 4 + k * 5;
      display.drawLine(xl, 56, xl + 3, 51, SSD1306_WHITE);
      display.drawLine(xr, 56, xr + 3, 51, SSD1306_WHITE);
    }
  }
  if (F.expr == EYE_THINKING) {
    // "..." thought dots appearing one by one, top-left
    int n = (millis() / 350) % 4;
    if (n >= 1) display.fillCircle(4, 14, 1, SSD1306_WHITE);
    if (n >= 2) display.fillCircle(9, 9, 2, SSD1306_WHITE);
    if (n >= 3) display.fillCircle(16, 4, 3, SSD1306_WHITE);
  }

  if (F.expr == EYE_CRYING) {
    int eyeBottomY = cy + (int)(F.eyeH / 2);
    drawTear(leftCx,  eyeBottomY, F.tearPhaseL);
    drawTear(rightCx, eyeBottomY, F.tearPhaseR);
  }
  if (F.expr == EYE_HEADACHE) {
    drawPainMarks(SCREEN_WIDTH/2 + shake, cy);
  }
}

static void drawMouth() {
  int cy = 52;
  int shake = laughShakeX();
  int cx = SCREEN_WIDTH / 2 + shake;
  float w = max(10.0f, F.mouthWidth);
  float half = w / 2.0f;

  if (F.mouthExpr == MOUTH_GRIMACE) {
    // Gritted-teeth zigzag instead of the smooth curve — reads as
    // clenched/tense rather than a normal open/closed mouth shape.
    int zigH = 2 + (int)(F.mouthOpen * 5);
    const int STEPS = 6;
    int prevX = cx - (int)half, prevY = cy;
    for (int i = 1; i <= STEPS; i++) {
      int x = cx - (int)half + (int)((w * i) / STEPS);
      int y = cy + ((i % 2 == 0) ? zigH : -zigH);
      display.drawLine(prevX, prevY, x, y, SSD1306_WHITE);
      prevX = x; prevY = y;
    }
    return;
  }

  if (F.mouthExpr == MOUTH_WAVY) {
    // Wobbly travelling sine line - nervous/dizzy.
    const int SEG = 16;
    float ph = millis() * 0.012f;
    int prevX = 0, prevY = 0;
    for (int i = 0; i <= SEG; i++) {
      float t = (float)i / SEG;
      int x = cx + (int)((t - 0.5f) * w);
      int y = cy + (int)(sinf(t * 3 * PI + ph) * 2.5f);
      if (i > 0) display.drawLine(prevX, prevY, x, y, SSD1306_WHITE);
      prevX = x; prevY = y;
    }
    return;
  }

  // Ranges tuned to be clearly visible on a 64px-tall display: up to
  // ~7px corner lift/drop for smile/frown, ~10px open bulge.
  float smile   = constrain(F.mouthSmile, -1.0f, 1.0f);
  float smileBend = smile * 7.0f;              // +/- corner lift (smile/frown)
  float openPx  = F.mouthOpen * 11.0f;         // vertical "open" bulge

  // Draw as a poly-line curve made of short segments -> smooth curve
  // that can bend continuously from frown to smile and open/close.
  const int SEGMENTS = 14;
  int prevX = 0, prevY = 0;
  for (int i = 0; i <= SEGMENTS; i++) {
    float t = (float)i / SEGMENTS;          // 0..1 across mouth width
    float xOff = (t - 0.5f) * w;
    float nx = xOff / half;                 // -1..1 normalized position

    // Cup/arc shape: at center (nx=0) the curve contributes nothing;
    // at the corners (nx=+-1) it lifts (smile) or drops (frown).
    float arc = smileBend * (nx * nx);
    if (F.mouthExpr == MOUTH_SMIRK && nx < 0) arc *= 0.15f; // only the right corner lifts

    // Vertical bulge, strongest at mouth center, for open/talking shapes.
    float bulge = sinf(t * PI) * openPx;

    int x = cx + (int)xOff;
    int y = cy - (int)arc + (int)(bulge * 0.4f);
    if (i > 0) display.drawLine(prevX, prevY, x, y, SSD1306_WHITE);
    prevX = x; prevY = y;
  }

  if (F.mouthExpr == MOUTH_TONGUE) {
    display.fillCircle(cx + 3, cy + 3, 3, SSD1306_WHITE);
    display.drawFastVLine(cx + 3, cy + 2, 3, SSD1306_BLACK); // tongue groove
  }
}

// ---- Event icon badge --------------------------------------------
// Drawn as a small black-filled circle with a white outline first —
// that "punches a hole" through whatever's underneath (typically the
// right eye, since the badge sits in the top-right corner) so the
// glyph drawn on top of it stays legible regardless of eye size/shape.
static constexpr int ICON_CX = 116, ICON_CY = 11, ICON_R = 10;

static void drawIcon(FaceIcon icon) {
  if (icon == ICON_NONE) return;
  display.fillCircle(ICON_CX, ICON_CY, ICON_R, SSD1306_BLACK);
  display.drawCircle(ICON_CX, ICON_CY, ICON_R, SSD1306_WHITE);

  int cx = ICON_CX, cy = ICON_CY;
  switch (icon) {
    case ICON_WIFI: {
      // Ascending signal bars; how many are "lit" cycles for a
      // searching/pulsing feel while WiFi is (re)connecting.
      int lit = 1 + ((millis() / 300) % 3);
      if (lit >= 1) display.fillRect(cx - 6, cy + 3, 2, 3, SSD1306_WHITE);
      if (lit >= 2) display.fillRect(cx - 2, cy,     2, 6, SSD1306_WHITE);
      if (lit >= 3) display.fillRect(cx + 2, cy - 4, 2, 10, SSD1306_WHITE);
      break;
    }
    case ICON_BELL: {
      // Gentle side-to-side wiggle reads as "ringing".
      int shake = (int)(sinf(millis() * 0.025f) * 2.0f);
      display.fillRoundRect(cx - 4 + shake, cy - 6, 8, 8, 3, SSD1306_WHITE);
      display.fillRect(cx - 5 + shake, cy + 1, 10, 2, SSD1306_WHITE);
      display.fillCircle(cx + shake, cy + 5, 1, SSD1306_WHITE);
      break;
    }
    case ICON_HANDSHAKE:
      display.fillCircle(cx - 4, cy, 3, SSD1306_WHITE);
      display.fillCircle(cx + 4, cy, 3, SSD1306_WHITE);
      display.drawLine(cx - 4, cy, cx + 4, cy, SSD1306_WHITE);
      break;
    case ICON_AIRFLOW: {
      // Three short flowing wavy lines, phase-animated left to right.
      int phase = (millis() / 150) % 4;
      for (int i = 0; i < 3; i++) {
        int y = cy - 5 + i * 5;
        int x0 = cx - 6 + ((phase + i) % 4);
        display.drawLine(x0,     y, x0 + 4, y - 2, SSD1306_WHITE);
        display.drawLine(x0 + 4, y - 2, x0 + 8, y, SSD1306_WHITE);
      }
      break;
    }
    case ICON_SWEAT: // anime-style sweat drop: circle + point
      display.fillCircle(cx, cy + 3, 3, SSD1306_WHITE);
      display.fillTriangle(cx - 3, cy + 1, cx + 3, cy + 1, cx, cy - 5, SSD1306_WHITE);
      break;
    case ICON_MOON: // crescent: white disc with a black disc bitten out
      display.fillCircle(cx, cy, 6, SSD1306_WHITE);
      display.fillCircle(cx + 3, cy - 2, 5, SSD1306_BLACK);
      break;
    case ICON_BT:
    case ICON_BT_OFF: // Bluetooth rune (+ a slash when disconnected)
      display.drawLine(cx - 1, cy - 7, cx - 1, cy + 7, SSD1306_WHITE);
      display.drawLine(cx - 1, cy - 7, cx + 4, cy - 3, SSD1306_WHITE);
      display.drawLine(cx + 4, cy - 3, cx - 5, cy + 4, SSD1306_WHITE);
      display.drawLine(cx - 1, cy + 7, cx + 4, cy + 3, SSD1306_WHITE);
      display.drawLine(cx + 4, cy + 3, cx - 5, cy - 4, SSD1306_WHITE);
      if (icon == ICON_BT_OFF) {
        display.drawLine(cx - 7, cy - 7, cx + 7, cy + 7, SSD1306_WHITE);
        display.drawLine(cx - 6, cy - 7, cx + 7, cy + 6, SSD1306_WHITE);
      }
      break;
    case ICON_CHECK:
      for (int t = 0; t < 2; t++) {
        display.drawLine(cx - 6, cy + t, cx - 2, cy + 4 + t, SSD1306_WHITE);
        display.drawLine(cx - 2, cy + 4 + t, cx + 6, cy - 5 + t, SSD1306_WHITE);
      }
      break;
    case ICON_CROSS:
      for (int t = 0; t < 2; t++) {
        display.drawLine(cx - 5 + t, cy - 5, cx + 5 + t, cy + 5, SSD1306_WHITE);
        display.drawLine(cx - 5 + t, cy + 5, cx + 5 + t, cy - 5, SSD1306_WHITE);
      }
      break;
    default: break;
  }
}

// Small drifting "z Z z" in the opposite corner from the icon badge,
// only while the moon icon (light off / sleeping) is showing.
static void drawSleepZs() {
  const char *chars = "zZz";
  for (int i = 0; i < 3; i++) {
    int bob = (int)(sinf(millis() * 0.004f + i) * 2.0f);
    display.setCursor(2 + i * 6, 2 + i * 3 + bob);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.print(chars[i]);
  }
}

// Event caption ("SERVER OK", "+1", ...) in a black box, top-left.
static void drawCaption() {
  if (!g_caption) return;
  int len = strlen(g_caption);
  display.fillRect(0, 0, len * 6 + 3, 10, SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(1, 1);
  display.print(g_caption);
}

// Replaces the Dev Module's BLE status LED: a tiny Bluetooth rune in the
// bottom-left corner while a phone is connected, plus a check when the
// app's task is complete or a "?" when no task is selected. Nothing is
// drawn while searching/advertising.
static void drawBleStatus() {
  BleStatus st = bleStatus();
  if (st == BLE_STATUS_SEARCHING) return;
  display.drawLine(4, 54, 4, 63, SSD1306_WHITE);
  display.drawLine(4, 54, 7, 57, SSD1306_WHITE);
  display.drawLine(7, 57, 1, 61, SSD1306_WHITE);
  display.drawLine(4, 63, 7, 60, SSD1306_WHITE);
  display.drawLine(7, 60, 1, 56, SSD1306_WHITE);
  if (st == BLE_STATUS_DONE) {
    display.drawLine(10, 59, 12, 61, SSD1306_WHITE);
    display.drawLine(12, 61, 16, 56, SSD1306_WHITE);
  } else if (st == BLE_STATUS_NO_TASK) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 56);
    display.print('?');
  }
}

void faceRenderIdle() {
  display.clearDisplay();
  drawEyes();
  drawMouth();
  drawIcon(F.icon);
  if (F.icon == ICON_MOON) drawSleepZs();
  drawCaption();
  drawBleStatus();
  display.display();
}
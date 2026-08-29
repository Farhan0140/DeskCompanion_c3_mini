#pragma once
// =======================================================================
// FACE MODULE — declarations only. See face.cpp for the smooth eased
// eye/mouth animation engine implementation.
// =======================================================================
#include <Arduino.h>

enum EyeExpression {
  EYE_NORMAL, EYE_HAPPY, EYE_SLEEPY, EYE_SURPRISED,
  EYE_ANGRY, EYE_CONFUSED, EYE_SAD,
  EYE_WOW, EYE_CRYING, EYE_HEADACHE, EYE_LAUGHING
};

enum MouthExpression {
  MOUTH_NEUTRAL, MOUTH_HAPPY, MOUTH_SMILE, MOUTH_SURPRISED,
  MOUTH_SAD, MOUTH_TALK,
  MOUTH_LAUGH, MOUTH_CRY, MOUTH_GRIMACE
};

enum GazeDir { GAZE_CENTER, GAZE_LEFT, GAZE_RIGHT, GAZE_UP, GAZE_DOWN };

// Small badge icon drawn in the corner alongside an expression, to give
// context-specific events (WiFi connecting, timer ringing, ...) a
// distinct look beyond just the eyes/mouth. ICON_NONE draws nothing.
enum FaceIcon { ICON_NONE, ICON_WIFI, ICON_BELL, ICON_HANDSHAKE, ICON_AIRFLOW, ICON_SWEAT, ICON_MOON };

// Contextual events other modules can raise to take over the face for
// a moment (or, for WIFI_CONNECTING, until explicitly cleared). See
// faceShowEvent()/faceClearWifiConnecting() below.
enum FaceEvent {
  FACE_EVENT_BOOT,            // first boot greeting
  FACE_EVENT_WIFI_CONNECTING, // held for as long as WiFi is (re)connecting
  FACE_EVENT_TIMER_RING,      // timer alarm just fired
  FACE_EVENT_FAN_ON,
  FACE_EVENT_FAN_OFF,
  FACE_EVENT_LIGHT_ON,
  FACE_EVENT_LIGHT_OFF
};

// ---- current (eased/animated) values ----
struct FaceAnim {
  // Eye geometry (shared L/R, mirrored around center)
  float eyeW = 42, eyeH = 42;         // current size
  float eyeW_t = 42, eyeH_t = 42;     // target size

  // Eyelid openness is now PER EYE (0=closed .. 1=open) so a wink can
  // close just one side while a normal blink still closes both — both
  // are driven toward these targets by faceUpdate() each frame.
  float lidOpenL = 1.0f, lidOpenL_t = 1.0f;
  float lidOpenR = 1.0f, lidOpenR_t = 1.0f;

  float pupilX = 0, pupilY = 0;       // current gaze offset (px)
  float pupilX_t = 0, pupilY_t = 0;   // target gaze offset

  float lTiltDeg = 0, rTiltDeg = 0;   // per-eye eyelid tilt (angry/sad/confused/headache)
  float lTilt_t = 0, rTilt_t = 0;

  float browRaise = 0, browRaise_t = 0; // surprised/wow eyebrow lift (negative = furrowed)

  float mouthOpen = 0.15f, mouthOpen_t = 0.15f; // curve amplitude
  float mouthWidth = 30, mouthWidth_t = 30;
  float mouthSmile = 0, mouthSmile_t = 0;       // -1 sad .. +1 happy curvature

  // Breathing / idle micro-motion
  float breathPhase = 0;

  // Two-eye blink control
  unsigned long nextBlinkAt = 0;
  bool blinking = false;
  unsigned long blinkStart = 0;

  // One-eye wink control (independent of the two-eye blink above)
  bool winking = false;
  bool winkLeftEye = true;
  unsigned long winkStart = 0;

  // Crying: each eye's tear falls on its own loop, offset so they
  // don't drip in perfect lockstep.
  float tearPhaseL = 0.0f, tearPhaseR = 0.5f;

  // Idle personality state machine
  unsigned long idleStateChangeAt = 0;
  uint8_t idleStep = 0;

  EyeExpression expr = EYE_NORMAL;
  MouthExpression mouthExpr = MOUTH_NEUTRAL;

  // Corner badge icon for the currently-active contextual event, if any.
  FaceIcon icon = ICON_NONE;

  unsigned long lastFrameMs = 0;
};

// Shared face animation state — defined once in face.cpp.
extern FaceAnim F;

// Suppresses the idle personality wander while a menu is open.
// menu.cpp sets this true/false; face.cpp reads it.
extern bool g_menuActive;

void faceInit();
void faceUpdate(unsigned long now);
void faceSetGaze(GazeDir dir);
void faceBlink();
void faceWink(bool leftEye = true); // one-eye blink; auto-releases after ~0.3s
void faceSetExpression(EyeExpression e, MouthExpression m);
void faceRenderIdle();

// Takes over the face for a contextual event: sets the matching
// expression + corner icon immediately. FACE_EVENT_WIFI_CONNECTING
// stays up (surviving retries) until faceClearWifiConnecting() is
// called; every other event auto-reverts to the normal idle
// personality loop a few seconds after being raised (or resumes
// FACE_EVENT_WIFI_CONNECTING first, if that's still in progress).
// Safe to call from either core — see face.cpp for details.
void faceShowEvent(FaceEvent ev);
void faceStartWifiConnecting(); // like faceShowEvent(FACE_EVENT_WIFI_CONNECTING), but won't cut off a transient event already showing (e.g. the boot hello)
void faceClearWifiConnecting();
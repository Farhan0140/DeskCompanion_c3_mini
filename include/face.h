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
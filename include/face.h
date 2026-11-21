#pragma once
// =======================================================================
// FACE MODULE — declarations only. See face.cpp for the smooth eased
// eye/mouth animation engine implementation.
// =======================================================================
#include <Arduino.h>

enum EyeExpression {
  EYE_NORMAL, EYE_HAPPY, EYE_SLEEPY, EYE_SURPRISED,
  EYE_ANGRY, EYE_CONFUSED, EYE_SAD,
  EYE_WOW, EYE_CRYING, EYE_HEADACHE, EYE_LAUGHING,
  // ---- 10 new expressions, mixed randomly into the idle loop ----
  EYE_LOVE,        // heart eyes
  EYE_STAR,        // star-struck (also used for BLE "target reached")
  EYE_DIZZY,       // spinning spiral eyes
  EYE_SUSPICIOUS,  // one eye narrowed, side-eye
  EYE_COOL,        // sunglasses
  EYE_SHY,         // looking down + blushing cheeks
  EYE_BORED,       // flat half-closed lids
  EYE_THINKING,    // looking up + animated "..." thought dots
  EYE_SCARED,      // small trembling eyes
  EYE_KAWAII,      // ^ ^ closed happy eyes
  // ---- event-only ----
  EYE_CONTENT      // relieved closed eyes — the "server OK" face
};

enum MouthExpression {
  MOUTH_NEUTRAL, MOUTH_HAPPY, MOUTH_SMILE, MOUTH_SURPRISED,
  MOUTH_SAD, MOUTH_TALK,
  MOUTH_LAUGH, MOUTH_CRY, MOUTH_GRIMACE,
  MOUTH_SMIRK,   // one-sided smile
  MOUTH_WAVY,    // wobbly line (dizzy/scared)
  MOUTH_FLAT,    // straight line (bored)
  MOUTH_TONGUE   // smile with tongue out
};

enum GazeDir { GAZE_CENTER, GAZE_LEFT, GAZE_RIGHT, GAZE_UP, GAZE_DOWN };

// Small badge icon drawn in the corner alongside an expression, to give
// context-specific events (WiFi connecting, BLE, server status, ...) a
// distinct look beyond just the eyes/mouth. ICON_NONE draws nothing.
enum FaceIcon { ICON_NONE, ICON_WIFI, ICON_BELL, ICON_HANDSHAKE, ICON_AIRFLOW, ICON_SWEAT, ICON_MOON,
                ICON_BT, ICON_BT_OFF, ICON_CHECK, ICON_CROSS };

// Contextual events other modules can raise to take over the face for
// a moment (or, for WIFI_CONNECTING, until explicitly cleared). See
// faceShowEvent()/faceClearWifiConnecting() below.
enum FaceEvent {
  FACE_EVENT_BOOT,            // first boot greeting
  FACE_EVENT_WIFI_CONNECTING, // held for as long as WiFi is (re)connecting
  FACE_EVENT_FAN_ON,
  FACE_EVENT_FAN_OFF,
  FACE_EVENT_LIGHT_ON,
  FACE_EVENT_LIGHT_OFF,
  // Server health check (netsync.cpp, every 10 min)
  FACE_EVENT_SERVER_OK,
  FACE_EVENT_SERVER_DOWN,
  // BLE counter button (ble_control.cpp)
  FACE_EVENT_BLE_CONNECTED,
  FACE_EVENT_BLE_DISCONNECTED,
  FACE_EVENT_BLE_COUNTED,
  FACE_EVENT_BLE_TARGET,
  FACE_EVENT_BLE_ALREADY_DONE,
  FACE_EVENT_BLE_NO_TASK,
  FACE_EVENT_BLE_NOT_RECEIVED,
  FACE_EVENT_BLE_OFFLINE
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

  // Per-eye height squash (1 = full). Unlike the lids above this is set
  // by the expression, not the blink — e.g. SUSPICIOUS narrows one eye.
  // Always 1 for the original expressions.
  float squashL = 1.0f, squashL_t = 1.0f;
  float squashR = 1.0f, squashR_t = 1.0f;

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
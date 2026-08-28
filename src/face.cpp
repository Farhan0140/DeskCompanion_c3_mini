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
    default:               F.mouthSmile_t = 0.0f;  F.mouthOpen_t = 0.12f; break;
  }
}

void faceInit() {
  F.lastFrameMs = millis();
  F.nextBlinkAt = millis() + random(2000, 5000);
  F.idleStateChangeAt = millis() + random(2500, 5000);
  faceSetExpression(EYE_NORMAL, MOUTH_NEUTRAL);
}

static void faceRunIdlePersonality(unsigned long now) {
  if (g_menuActive) return;

  // Cycles gaze + expression when nothing else is driving the face.
  // The lighthearted new expressions (WOW, a wink, a quick LAUGHING
  // chuckle) are mixed in here for variety. CRYING and HEADACHE are
  // intentionally NOT part of the spontaneous idle loop — a desk
  // companion randomly crying for no reason would be a strange UX.
  // Trigger those two (and a wink) yourself from wherever makes sense
  // in your project, e.g.:
  //   faceSetExpression(EYE_CRYING,   MOUTH_CRY);      // something went wrong
  //   faceSetExpression(EYE_HEADACHE, MOUTH_GRIMACE);  // repeated errors
  //   faceWink(true);                                   // a little easter egg
  if (now >= F.idleStateChangeAt) {
    F.idleStep = (F.idleStep + 1) % 9;
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
    }
    F.idleStateChangeAt = now + random(2200, 4500);
  }
}

void faceUpdate(unsigned long now) {
  if (now - F.lastFrameMs < FRAME_INTERVAL_MS) return;
  float dtScale = (now - F.lastFrameMs) / (float)FRAME_INTERVAL_MS;
  F.lastFrameMs = now;

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

static void drawEyes() {
  int baseY = 26;
  int shake = laughShakeX();
  int leftCx  = SCREEN_WIDTH/2 - 26 + (int)F.pupilX + shake;
  int rightCx = SCREEN_WIDTH/2 + 26 + (int)F.pupilX + shake;
  int cy = baseY + (int)F.pupilY - (int)F.browRaise;

  drawOneEye(leftCx, cy, F.lidOpenL);
  drawOneEye(rightCx, cy, F.lidOpenR);
  drawEyelidTilt(leftCx,  cy, F.lTiltDeg, F.eyeH * F.lidOpenL, F.eyeW);
  drawEyelidTilt(rightCx, cy, F.rTiltDeg, F.eyeH * F.lidOpenR, F.eyeW);

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

    // Vertical bulge, strongest at mouth center, for open/talking shapes.
    float bulge = sinf(t * PI) * openPx;

    int x = cx + (int)xOff;
    int y = cy - (int)arc + (int)(bulge * 0.4f);
    if (i > 0) display.drawLine(prevX, prevY, x, y, SSD1306_WHITE);
    prevX = x; prevY = y;
  }
}

void faceRenderIdle() {
  display.clearDisplay();
  drawEyes();
  drawMouth();
  display.display();
}
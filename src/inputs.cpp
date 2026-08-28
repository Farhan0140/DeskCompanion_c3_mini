#include <Arduino.h>
#include <IRremote.hpp>
#include "inputs.h"
#include "config.h"
#include "devices.h"
#include "buzzer.h"

InputEvent g_lastEvent = EV_NONE;
unsigned long g_lastInputAt = 0;

void inputsInit() {
  IrReceiver.begin(PIN_IR, DISABLE_LED_FEEDBACK);
}

void inputsUpdate(unsigned long now) {
  g_lastEvent = EV_NONE;

  if (IrReceiver.decode()) {
    uint32_t code = IrReceiver.decodedIRData.decodedRawData;
    bool isRepeat = (IrReceiver.decodedIRData.flags & (IRDATA_FLAGS_IS_REPEAT | IRDATA_FLAGS_IS_AUTO_REPEAT)) != 0;

    static unsigned long lastIRTime = 0;
    static uint32_t lastIRCode = 0;

    // Filter repeat signals, invalid codes, and rapid duplicate frames within IR_DEBOUNCE_MS window
    if (!isRepeat && code != 0) {
      if (code != lastIRCode || (now - lastIRTime >= IR_DEBOUNCE_MS)) {
        lastIRTime = now;
        lastIRCode = code;

        Serial.printf("[IR] 0x%08X\n", code);

        switch (code) {
          case IR_UP:   g_lastEvent = EV_UP; break;
          case IR_DOWN: g_lastEvent = EV_DOWN; break;
          case IR_OK:   g_lastEvent = EV_OK; break;
          case IR_BACK: g_lastEvent = EV_BACK; break;
          case IR_LIGHT:
            setLightMode(D.lightOn ? MODE_OFF : MODE_ON);
            break;
          case IR_FAN_ON:  setFanMode(MODE_ON); break;
          case IR_FAN_OFF: setFanMode(MODE_OFF); break;
          default: break;
        }
      }
    }
    IrReceiver.resume();
  }

  if (g_lastEvent != EV_NONE) {
    g_lastInputAt = now;
    buzzerClick();
  }
}


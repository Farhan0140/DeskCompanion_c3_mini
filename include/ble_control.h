#pragma once
// =======================================================================
// BLE CONTROL MODULE — the Soul Shield physical counter button, ported
// from Framework-ESP32-DevModule (same BLE identity / protocol, so the
// Soul Shield app pairs with this board exactly like the Dev Module).
//
// One debounced press on PIN_BLE_BUTTON = one one-byte "1" notification
// on BLE_CHAR_TX_UUID = +1 on the app's active Bluetooth-controlled
// counter task. The app answers on BLE_CHAR_RX_UUID (see ble_control.cpp
// handleMessage()). Feedback is shown on the buzzer + OLED face (this
// board has no status LED).
//
// Runs from the main loop like every other module: bleInit() once in
// setup(), bleUpdate() every loop() tick. The BLE stack's own callbacks
// only set flags / fill a ring buffer — all real work happens in
// bleUpdate() on the loop task.
// =======================================================================
#include <Arduino.h>

void bleInit();
void bleUpdate(unsigned long now);
bool bleIsConnected();

// What the Dev Module's idle LED used to show — drawn instead as a small
// Bluetooth mark in the bottom-left corner of the idle face (face.cpp).
enum BleStatus {
  BLE_STATUS_SEARCHING,  // no phone connected (advertising)
  BLE_STATUS_COUNTING,   // connected, task still counting (or state not known yet)
  BLE_STATUS_DONE,       // connected, task complete — presses no longer count
  BLE_STATUS_NO_TASK     // connected, no task selected in the app
};
BleStatus bleStatus();

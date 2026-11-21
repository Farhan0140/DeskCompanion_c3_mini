// =======================================================================
// BLE CONTROL MODULE — implementation.
//
// Logic ported from Framework-ESP32-DevModule/src/main.cpp (unchanged:
// BLE server + TX notify / RX write characteristics, debounced falling-
// edge button, reply/ack bookkeeping with ACK timeout, auto re-advertise
// while disconnected). Differences, all forced by this board:
//   - Button pin GPIO1 (GPIO4 is the OLED's SDA here) — see config.h.
//   - No status LED: the LED+buzzer pattern player is replaced by the
//     existing buzzer module (same tones, see buzzer.cpp) + an OLED face
//     event, so BLE never fights buzzer.cpp over the LEDC channel.
//   - No delay(5) at the end — this runs inside the shared main loop.
//
// FEEDBACK (buzzer + face):
//   Phone connected ............ rising beep-beep,   happy face + BT icon
//   Phone disconnected ......... falling beep-beep,  sad face + BT-off icon
//   Press counted (app ack) .... short high tick,    "+1" smile
//   TARGET REACHED ............. victory melody,     star-struck "DONE!"
//   Press after completion ..... 2 low buzzes,       "DONE" (not counted)
//   No task selected in app .... 2 medium beeps,     confused "NO TASK"
//   Update NOT received ........ 3 low beeps,        sad "NOT SENT"
//   Press while disconnected ... 1 long low tone,    BT-off "NO PHONE"
// =======================================================================
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "ble_control.h"
#include "config.h"
#include "buzzer.h"
#include "face.h"

// ===========================================================================
// Feedback (replaces the Dev Module's PLAY(PAT_xxx) LED/buzzer patterns)
// ===========================================================================

enum BleFeedback {
  FB_CONNECTED, FB_DISCONNECTED, FB_COUNTED, FB_TARGET_REACHED,
  FB_ALREADY_COMPLETE, FB_NO_TASK, FB_NOT_RECEIVED, FB_OFFLINE_PRESS
};

static void play(BleFeedback fb) {
  switch (fb) {
    case FB_CONNECTED:        buzzerBleConnected();       faceShowEvent(FACE_EVENT_BLE_CONNECTED);    break;
    case FB_DISCONNECTED:     buzzerBleDisconnected();    faceShowEvent(FACE_EVENT_BLE_DISCONNECTED); break;
    case FB_COUNTED:          buzzerBleCounted();         faceShowEvent(FACE_EVENT_BLE_COUNTED);      break;
    case FB_TARGET_REACHED:   buzzerBleTargetReached();   faceShowEvent(FACE_EVENT_BLE_TARGET);       break;
    case FB_ALREADY_COMPLETE: buzzerBleAlreadyComplete(); faceShowEvent(FACE_EVENT_BLE_ALREADY_DONE); break;
    case FB_NO_TASK:          buzzerBleNoTask();          faceShowEvent(FACE_EVENT_BLE_NO_TASK);      break;
    case FB_NOT_RECEIVED:     buzzerBleNotReceived();     faceShowEvent(FACE_EVENT_BLE_NOT_RECEIVED); break;
    case FB_OFFLINE_PRESS:    buzzerBleOfflinePress();    faceShowEvent(FACE_EVENT_BLE_OFFLINE);      break;
  }
}

// ===========================================================================
// State
// ===========================================================================

enum TaskState { TASK_UNKNOWN, TASK_OPEN, TASK_DONE, TASK_NONE };

static BLEServer* server = nullptr;
static BLECharacteristic* txCharacteristic = nullptr;
static BLE2902* txCccd = nullptr;  // tells us whether the app subscribed to presses
static volatile bool deviceConnected = false;
static bool wasConnected = false;
static TaskState taskState = TASK_UNKNOWN;

static unsigned long lastDebounceTime = 0;
static int lastReading = HIGH;
static int lastStableState = HIGH;

// Presses sent and still waiting for the app's answer.
static uint8_t awaitingReplies = 0;
static unsigned long replyDeadline = 0;

static unsigned long nextAdvertiseAt = 0;

// Messages from the app arrive on the BLE task; hand them to the main
// loop through a small single-producer / single-consumer ring buffer.
#define RX_BUF_SIZE 16
static volatile uint8_t rxHead = 0;
static volatile uint8_t rxTail = 0;
static char rxBuf[RX_BUF_SIZE];

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { deviceConnected = true; }
  void onDisconnect(BLEServer*) override { deviceConnected = false; }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    for (char c : value) {
      uint8_t next = (rxHead + 1) % RX_BUF_SIZE;
      if (next == rxTail) break;  // full — drop (never happens at human speed)
      rxBuf[rxHead] = c;
      rxHead = next;
    }
  }
};

bool bleIsConnected() { return deviceConnected; }

// Mirrors the Dev Module's idleLed() decision table.
BleStatus bleStatus() {
  if (!deviceConnected) return BLE_STATUS_SEARCHING;
  switch (taskState) {
    case TASK_DONE: return BLE_STATUS_DONE;
    case TASK_NONE: return BLE_STATUS_NO_TASK;
    default:        return BLE_STATUS_COUNTING;
  }
}

// ===========================================================================
// App messages (MUST match ESP_REPLY / ESP_STATE in the app's counter-ble.ts)
// ===========================================================================

static void replyArrived() {
  if (awaitingReplies > 0) awaitingReplies--;
}

static void handleMessage(char c) {
  switch (c) {
    // --- replies to a press ---
    case 'A':  // counted
      replyArrived();
      taskState = TASK_OPEN;
      play(FB_COUNTED);
      Serial.println("[BLE] App: +1 counted");
      break;
    case 'D':  // this press reached the target
      replyArrived();
      taskState = TASK_DONE;
      play(FB_TARGET_REACHED);
      Serial.println("[BLE] App: TARGET REACHED — task complete, further presses won't count");
      break;
    case 'X':  // already complete, press ignored
      replyArrived();
      taskState = TASK_DONE;
      play(FB_ALREADY_COMPLETE);
      Serial.println("[BLE] App: task already complete — press not counted");
      break;
    case 'N':  // no task selected
      replyArrived();
      taskState = TASK_NONE;
      play(FB_NO_TASK);
      Serial.println("[BLE] App: no task selected for Bluetooth control");
      break;
    case 'E':  // the app couldn't save it
      replyArrived();
      play(FB_NOT_RECEIVED);
      Serial.println("[BLE] App: press could not be saved");
      break;
    // --- state sync (no sound) ---
    case 'U':
      taskState = TASK_OPEN;
      Serial.println("[BLE] App: active task is counting");
      break;
    case 'C':
      taskState = TASK_DONE;
      Serial.println("[BLE] App: active task is complete");
      break;
    case '0':
      taskState = TASK_NONE;
      Serial.println("[BLE] App: no active task");
      break;
    default:
      Serial.print("[BLE] App: unknown message ");
      Serial.println((int)c);
  }
}

// ===========================================================================

static void onButtonPressed() {
  if (!deviceConnected) {
    play(FB_OFFLINE_PRESS);
    Serial.println("[BLE] Button press ignored (phone not connected)");
    return;
  }
  if (txCccd && !txCccd->getNotifications()) {
    // Connected but the app isn't listening — the press would go nowhere.
    play(FB_NOT_RECEIVED);
    Serial.println("[BLE] Button press NOT sent (app not subscribed yet)");
    return;
  }
  uint8_t increment = 1;
  txCharacteristic->setValue(&increment, 1);
  txCharacteristic->notify();
  awaitingReplies++;
  replyDeadline = millis() + BLE_ACK_TIMEOUT_MS;
  Serial.println("[BLE] Button press -> sent, waiting for the app");
}

void bleInit() {
  pinMode(PIN_BLE_BUTTON, INPUT_PULLUP);

  BLEDevice::init(BLE_DEVICE_NAME);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(BLE_SERVICE_UUID);

  // TX: the phone subscribes here; one one-byte "1" notification per press.
  txCharacteristic = service->createCharacteristic(
      BLE_CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txCccd = new BLE2902();
  txCharacteristic->addDescriptor(txCccd);

  // RX: the app's answers + task state.
  BLECharacteristic* rxCharacteristic = service->createCharacteristic(
      BLE_CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  rxCharacteristic->setCallbacks(new RxCallbacks());

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println(F("[BLE] " BLE_DEVICE_NAME " ready — advertising, waiting for the app."));
}

void bleUpdate(unsigned long now) {
  // --- connection changes ---
  bool connected = deviceConnected;
  if (connected != wasConnected) {
    wasConnected = connected;
    awaitingReplies = 0;
    if (connected) {
      taskState = TASK_UNKNOWN;  // the app sends the real state right away
      play(FB_CONNECTED);
      Serial.println("[BLE] Phone connected.");
    } else {
      play(FB_DISCONNECTED);
      Serial.println("[BLE] Phone disconnected — advertising again.");
      nextAdvertiseAt = now + 500;  // let the stack settle first
    }
  }

  // --- messages from the app ---
  while (rxTail != rxHead) {
    char c = rxBuf[rxTail];
    rxTail = (rxTail + 1) % RX_BUF_SIZE;
    handleMessage(c);
  }

  // --- button (debounced, FALLING edge = pressed) ---
  int reading = digitalRead(PIN_BLE_BUTTON);
  if (reading != lastReading) {
    lastDebounceTime = now;
    lastReading = reading;
  }
  if ((now - lastDebounceTime) > BLE_DEBOUNCE_MS && reading != lastStableState) {
    lastStableState = reading;
    if (lastStableState == LOW) onButtonPressed();
  }

  // --- the app never answered ---
  if (awaitingReplies > 0 && (long)(now - replyDeadline) >= 0) {
    awaitingReplies = 0;
    play(FB_NOT_RECEIVED);
    Serial.println("[BLE] No answer from the app — update NOT received");
  }

  // --- keep advertising while no phone is connected (auto-reconnect) ---
  if (!connected && (long)(now - nextAdvertiseAt) >= 0) {
    BLEDevice::startAdvertising();
    nextAdvertiseAt = now + 2000;
  }
}

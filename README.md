# Desk Buddy

**A cheerful little ESP32-C3 desk companion** — an animated OLED face that reacts to what's happening around it, automated light/fan control, presence + ambient-light sensing with on-device threshold tuning, IR remote control, a Bluetooth (BLE) counter button for the Soul Shield app, and a periodic server health check, all wrapped in a from-your-phone WiFi setup flow so nothing is hardcoded.

Built on an ESP32-C3 Super Mini with PlatformIO / Arduino framework.

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Pinout](#pinout)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
- [WiFi Setup](#wifi-setup)
- [Menu & Controls](#menu--controls)
- [Threshold Setup](#threshold-setup)
- [BLE Counter Button](#ble-counter-button)
- [Server Health Check](#server-health-check)
- [Persistence](#persistence)
- [License](#license)

---

## Features

**Animated face**
- A smooth, eased (not frame-swapped) OLED face — blinking, breathing, gaze, winks — with an idle "personality" loop that wanders between expressions on its own.
- 29 idle expressions picked at random: the original set plus heart eyes, star-struck, dizzy spirals, suspicious side-eye, cool sunglasses, shy blush, bored, thinking "...", scared tremble, kawaii "^ ^ :P", shocked O_O, squinting stare, mocking tongue-out taunt, evil grin, roaring laughter, sarcastic eye-roll, disgusted "bleh", terrified, puzzled "?" and a big yawn.
- Contextual expressions that take over the face for real events: a boot greeting, a "connecting…" face with a WiFi icon while WiFi (re)connects, a relaxed face + airflow icon when the fan turns on, a sweat-drop icon when it turns off, a joyful face when the light turns on, a sleepy face + moon/"Zzz" icon when it turns off, a content "SERVER OK" / sad "SERVER DOWN" face after each health check, and BLE events (connected, +1 counted, target reached, ...).

**Automated relay control**
- Two relays (light + fan), each independently switchable between **AUTO**, **ON**, and **OFF**.
- AUTO mode drives the relays from live sensor state: the light turns on when it's dark *and* someone's present; the fan turns on whenever presence is detected.
- Relay modes persist in NVS flash storage and survive a reset — no more everything silently reverting to AUTO on power-up.

**Sensing**
- HC-SR04 ultrasonic distance sensing with hysteresis for stable presence detection (no relay chatter at the trigger boundary).
- LDR ambient-light sensing with a moving average and its own hysteresis band for a stable day/dark decision.
- All four thresholds (`DISTANCE_ON_CM`, `DISTANCE_OFF_CM`, `LDR_THRESHOLD_ON`, `LDR_THRESHOLD_OFF`) are adjustable from the menu with the IR remote and saved to flash — no re-upload needed.

**IR remote control**
- Menu navigation (up/down/OK/back) plus direct one-button light toggle and fan on/off, entirely non-blocking so button/IR polling never stalls behind a network call.

**On-device menu (SSD1306 OLED)**
- Per-relay mode control, a live sensor readout screen, threshold setup, and WiFi setup — navigable with the IR remote.

**WiFi provisioning portal**
- No hardcoded WiFi credentials required. Selecting **WiFi Setup** from the menu spins up the device's own access point and shows its IP on screen; connecting from a phone opens a page that scans nearby networks, lets you tap one and enter the password, and the device saves it and reconnects as a normal WiFi client — remembered across reboots.

**BLE counter button (Soul Shield app)**
- Ported from the ESP32 Dev Module firmware with the identical BLE identity, so the Soul Shield app pairs with this board exactly the same way. One press = +1 on the app's active counter task; the app's answers are shown on the buzzer and OLED face.

**Server health check**
- Every 10 minutes a background task calls `https://soul-shield-api.onrender.com/health` and shows the result on the face.

**Non-blocking buzzer**
- A small tone sequencer for click/confirm/error/boot/WiFi-connected/BLE feedback sounds, driven entirely from the main loop tick — never a blocking `delay()`.

---

## Hardware

| Component | Notes |
|---|---|
| ESP32-C3 Super Mini | Main controller (single RISC-V core, dual-core-*task* split via FreeRTOS across... well, it's one core — see [Architecture](#architecture)) |
| SSD1306 OLED, 128×64, I²C | Face + menu display |
| HC-SR04 ultrasonic sensor | Presence detection |
| LDR (photoresistor) + resistor | Ambient light sensing (analog) |
| IR receiver (e.g. TSOP38238) | Remote control input |
| Push button | BLE counter button (to GND, internal pull-up) |
| 2-channel relay module | Light + fan switching |
| Passive/active buzzer | Audible feedback |
| Any NEC-ish IR remote | Control (codes are user-mapped, see below) |

> **Note:** the ESP32-C3 has a single physical CPU core; "core 0 / core 1" below refers to the two FreeRTOS scheduling contexts the Arduino-ESP32 core exposes (`xTaskCreatePinnedToCore`), which is what actually matters here — see [Architecture](#architecture).

## Pinout

| Signal | GPIO |
|---|---|
| OLED SDA | 4 |
| OLED SCL | 5 |
| Ultrasonic TRIG | 20 |
| Ultrasonic ECHO | 21 |
| LDR (analog) | 0 |
| IR receiver | 3 |
| Relay — Light | 6 |
| Relay — Fan | 7 |
| Buzzer | 10 |
| BLE counter button | 1 (other leg to GND) |

Relay polarity (active-low vs active-high) is configurable via `RELAY_ACTIVE_LOW` in `include/config.h`.

## Architecture

Desk Buddy splits work across two FreeRTOS execution contexts so a slow network call can never make the buttons feel unresponsive:

- **Core 1 (`loop()`)** — buttons/IR polling, sensors, relay logic, BLE button + app replies, menu, face animation/rendering, buzzer. Runs every ~20ms.
- **Core 0 (background task, `netsync.cpp`)** — WiFi connection management, NTP time sync, and the HTTPS server health check. Communicates with core 1 only through small plain-value flags/state (WiFi-connected status, face events), never blocking calls.
- **BLE stack (Bluedroid tasks)** — its callbacks only set a connected flag and fill a small ring buffer; `bleUpdate()` in the main loop does the real work. WiFi and BLE share the radio via ESP-IDF's software coexistence (WiFi modem sleep stays enabled, as coexistence requires).

The WiFi setup portal (AP + web/DNS servers) runs on core 1, pumped from the main loop only while its menu screen is open; the background networking task detects this and steps off the radio entirely until the portal hands control back.

## Project Structure

```
include/          Header files (one per module — declarations only)
src/              Implementation files
  main.cpp        setup()/loop() wiring
  face.cpp        Eased face animation engine + contextual events
  menu.cpp        Menu state machine + OLED screen rendering
  inputs.cpp      IR remote decoding -> input events
  ble_control.cpp BLE counter button for the Soul Shield app
  sensors.cpp     Ultrasonic + LDR polling with hysteresis
  devices.cpp     Relay logic (AUTO/ON/OFF) + NVS persistence
  buzzer.cpp      Non-blocking tone sequencer
  netsync.cpp     WiFi/NTP/server-health background task
  wifi_portal.cpp WiFi provisioning AP + setup web page
  settings.cpp    Runtime-tunable globals (thresholds, toggles) + NVS load/save
  globals.cpp     Shared hardware objects (display, NVS handle)
platformio.ini    Build configuration
```

## Getting Started

### Requirements

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- An ESP32-C3 Super Mini board

### Setup

1. Clone the repository.
2. Copy/create `include/secrets.h` (kept out of git) — WiFi credentials are **optional** here, since they can be configured entirely from the device's menu (see [WiFi Setup](#wifi-setup)):

   ```cpp
   #pragma once
   #define WIFI_SSID       ""   // optional fallback — leave blank and use the on-device WiFi Setup instead
   #define WIFI_PASSWORD   ""

   #define NTP_SERVER_1    "pool.ntp.org"
   #define NTP_SERVER_2    "time.google.com"
   #define GMT_OFFSET_SEC  (6 * 3600)  // your UTC offset
   #define DST_OFFSET_SEC  0
   ```

3. Map your IR remote's button codes in `include/config.h` (`IR_UP`, `IR_DOWN`, `IR_OK`, `IR_BACK`, `IR_LIGHT`, `IR_FAN_ON`, `IR_FAN_OFF`) — boot the device, open the Serial Monitor, press each button once, and copy the printed hex code.
4. Build and upload:

   ```sh
   pio run --target upload
   pio device monitor
   ```

## WiFi Setup

No need to hardcode a network. From the on-device menu:

1. Navigate to **WiFi Setup** and press OK.
2. The device starts its own open access point (`DeskBuddy-Setup` by default) and shows its IP address on screen.
3. Connect your phone to that access point, then browse to the shown IP.
4. Pick a network from the live scan, enter its password, and hit **Connect**.
5. The device saves the credentials to flash, shuts down its own access point, and joins your network — the menu returns to the root screen automatically once connected.

Credentials persist across reboots; if none have ever been saved, the device falls back to the (optional) hardcoded values in `secrets.h`.

## Menu & Controls

| Root menu item | What it does |
|---|---|
| **Devices** | Set Light/Fan mode: AUTO / ON / OFF |
| **Sensors** | Live distance, light level, and presence readout |
| **Threshold Setup** | Tune the ultrasonic and LDR thresholds (see below) |
| **WiFi Setup** | Provision WiFi from your phone (see above) |

A small **BT** badge in the root menu's title bar shows when the Soul Shield app is connected.

Navigation: **UP/DOWN** to move, **OK** to select, **BACK** to go up a level. The menu auto-returns to the idle face after a period of inactivity. The IR remote's dedicated light/fan buttons toggle those relays directly without opening the menu.

## Threshold Setup

**Threshold Setup** has two screens, both showing the live sensor value so you can tune against it:

| Screen | Values | Step per UP/DOWN | Default |
|---|---|---|---|
| **Ultrasonic** | `ON <=` (presence on) / `OFF >=` (presence off), cm | 1 cm | 6 / 8 |
| **LDR** | `DARK <=` / `BRIGHT >=`, averaged 0–4095 ADC | 100 | 1400 / 1600 |

**OK** switches between the two values, **UP/DOWN** changes the selected one, **BACK** returns. Every change takes effect immediately and is saved to flash at once. The ON value is always kept below the OFF value (the hysteresis gap); an error beep means you hit that limit or the range end.

## BLE Counter Button

Same BLE profile as the ESP32 Dev Module firmware:

| Item | Value |
|---|---|
| Device name | `SoulShield-ESP32` |
| Service UUID | `9f83f3d0-8f5f-4a3f-9a1d-2c6a8f4e5b01` |
| TX (ESP32 → phone, notify) | `a1b2c3d4-5e6f-4a7b-8c9d-0e1f2a3b4c5d` |
| RX (phone → ESP32, write) | `b2c3d4e5-6f7a-4b8c-9d0e-1f2a3b4c5d6e` |

Wire a push button between **GPIO1** and **GND**. One debounced press sends `"1"` → +1 on the app's active counter task. There's no status LED on this build; feedback is the same buzzer tones as the Dev Module plus the OLED face:

| Event | Buzzer | Face |
|---|---|---|
| Phone connected | rising beep-beep | happy + BT icon, "CONNECTED" |
| Phone disconnected | falling beep-beep | sad + BT-off icon, "DISCONNECTED" |
| Press counted | short high tick | smile, "+1" |
| Target reached | victory melody | star eyes + bell, "DONE!" |
| Press after completion | 2 low buzzes | bored, "ALREADY DONE" |
| No task selected in app | 2 medium beeps | confused, "NO TASK" |
| Update not received (no reply in 1.5 s / not subscribed / save failed) | 3 low beeps | sad, "NOT SENT" |
| Press while no phone is connected | 1 long low tone | BT-off icon, "NO PHONE" |

While a phone is connected, a tiny Bluetooth mark sits in the bottom-left corner of the idle face (with a ✓ when the task is complete, or `?` when no task is selected). This replaces the Dev Module's idle LED patterns.

## Server Health Check

Once WiFi is up, the background task calls `GET https://soul-shield-api.onrender.com/health` (3 s after connecting, then every 10 minutes). HTTP 200 with `"status":"ok"` shows a content face with a ✓ and "SERVER OK" for 5 s. Anything else shows a sad face with a ✗ and "SERVER DOWN", then retries once after 30 s, since free-tier hosts often fail the first request while waking up. The interval, retry and timeout values are in `include/config.h`.

## Persistence

Everything below survives a reset, stored in the ESP32's NVS flash (`Preferences`, namespace `deskbuddy`):

- Relay modes (light/fan)
- Ultrasonic + LDR thresholds
- Saved WiFi credentials

## License

No license file is currently included. If you plan to share or open-source this project, consider adding one (MIT is a common choice for hobby/embedded projects like this).

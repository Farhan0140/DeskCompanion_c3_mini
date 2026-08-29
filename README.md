# Desk Buddy

**A cheerful little ESP32-C3 desk companion** — an animated OLED face that reacts to what's happening around it, automated light/fan control, presence + ambient-light sensing, a timer and task list, IR remote control, and live sync to Firebase, all wrapped in a from-your-phone WiFi setup flow so nothing is hardcoded.

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
- [Firebase Sync](#firebase-sync)
- [Persistence](#persistence)
- [License](#license)

---

## Features

**Animated face**
- A smooth, eased (not frame-swapped) OLED face — blinking, breathing, gaze, winks — with an idle "personality" loop that wanders between expressions on its own.
- Contextual expressions that take over the face for real events: a boot greeting, a "connecting…" face with a WiFi icon while WiFi (re)connects, a startled face + bell icon when the timer rings, a relaxed face + airflow icon when the fan turns on, a sweat-drop icon when it turns off, a joyful face when the light turns on, and a sleepy face + moon/"Zzz" icon when it turns off.

**Automated relay control**
- Two relays (light + fan), each independently switchable between **AUTO**, **ON**, and **OFF**.
- AUTO mode drives the relays from live sensor state: the light turns on when it's dark *and* someone's present; the fan turns on whenever presence is detected.
- Relay modes persist in NVS flash storage and survive a reset — no more everything silently reverting to AUTO on power-up.

**Sensing**
- HC-SR04 ultrasonic distance sensing with hysteresis for stable presence detection (no relay chatter at the trigger boundary).
- LDR ambient-light sensing with a moving average and its own hysteresis band for a stable day/dark decision.

**IR remote control**
- Menu navigation (up/down/OK/back) plus direct one-button light toggle and fan on/off, entirely non-blocking so button/IR polling never stalls behind a network call.

**On-device menu (SSD1306 OLED)**
- Timer, task list, per-relay mode control, a live sensor readout screen, and WiFi setup — navigable with the IR remote.

**WiFi provisioning portal**
- No hardcoded WiFi credentials required. Selecting **WiFi Setup** from the menu spins up the device's own access point and shows its IP on screen; connecting from a phone opens a page that scans nearby networks, lets you tap one and enter the password, and the device saves it and reconnects as a normal WiFi client — remembered across reboots.

**Firebase Realtime Database sync**
- Background task (pinned to its own core) pushes live status and pulls remote commands over plain HTTPS REST — no extra SDK. Relay mode changes from the IR remote and from Firebase stay in sync in both directions.

**Timer & task list**
- A countdown timer with an audible alarm, and a small persistent task list (add/complete), both usable from the menu or remotely via Firebase.

**Non-blocking buzzer**
- A small tone sequencer for click/confirm/error/boot/task-done/timer-alarm/WiFi-connected feedback sounds, driven entirely from the main loop tick — never a blocking `delay()`.

---

## Hardware

| Component | Notes |
|---|---|
| ESP32-C3 Super Mini | Main controller (single RISC-V core, dual-core-*task* split via FreeRTOS across... well, it's one core — see [Architecture](#architecture)) |
| SSD1306 OLED, 128×64, I²C | Face + menu display |
| HC-SR04 ultrasonic sensor | Presence detection |
| LDR (photoresistor) + resistor | Ambient light sensing (analog) |
| IR receiver (e.g. TSOP38238) | Remote control input |
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

Relay polarity (active-low vs active-high) is configurable via `RELAY_ACTIVE_LOW` in `include/config.h`.

## Architecture

Desk Buddy splits work across two FreeRTOS execution contexts so a slow network call can never make the buttons feel unresponsive:

- **Core 1 (`loop()`)** — buttons/IR polling, sensors, relay logic, menu, face animation/rendering, buzzer. Runs every ~20ms.
- **Core 0 (background task, `netsync.cpp`)** — WiFi connection management, NTP time sync, and all Firebase HTTPS REST calls. Communicates with core 1 only through small plain-value flags/state (relay mode dirty flags, WiFi-connected status), never blocking calls.

The WiFi setup portal (AP + web/DNS servers) runs on core 1, pumped from the main loop only while its menu screen is open; the background networking task detects this and steps off the radio entirely until the portal hands control back.

## Project Structure

```
include/          Header files (one per module — declarations only)
src/              Implementation files
  main.cpp        setup()/loop() wiring
  face.cpp        Eased face animation engine + contextual events
  menu.cpp        Menu state machine + OLED screen rendering
  inputs.cpp      IR remote decoding -> input events
  sensors.cpp     Ultrasonic + LDR polling with hysteresis
  devices.cpp     Relay logic (AUTO/ON/OFF) + NVS persistence
  timer_task.cpp  Countdown timer + persistent task list
  buzzer.cpp      Non-blocking tone sequencer
  netsync.cpp     WiFi/NTP/Firebase background task
  wifi_portal.cpp WiFi provisioning AP + setup web page
  settings.cpp    Runtime-tunable globals (thresholds, toggles)
  globals.cpp     Shared hardware objects (display, NVS handle)
platformio.ini    Build configuration
```

## Getting Started

### Requirements

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- An ESP32-C3 Super Mini board
- A [Firebase Realtime Database](https://firebase.google.com/docs/database) (optional — the device runs fine offline; you just lose remote sync)

### Setup

1. Clone the repository.
2. Copy/create `include/secrets.h` (kept out of git) with at minimum a Firebase host — WiFi credentials are **optional** here now, since they can be configured entirely from the device's menu (see [WiFi Setup](#wifi-setup)):

   ```cpp
   #pragma once
   #define WIFI_SSID       ""   // optional fallback — leave blank and use the on-device WiFi Setup instead
   #define WIFI_PASSWORD   ""

   #define FIREBASE_HOST   "https://YOUR-PROJECT-default-rtdb.REGION.firebasedatabase.app"
   #define FIREBASE_AUTH   ""   // RTDB secret/token, or "" for public test-mode rules

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
| **Timer** | Start/set/cancel a countdown timer with an alarm |
| **Tasks** | View/complete tasks, add from presets |
| **Devices** | Set Light/Fan mode: AUTO / ON / OFF |
| **Sensors** | Live distance, light level, and presence readout |
| **WiFi Setup** | Provision WiFi from your phone (see above) |

Navigation: **UP/DOWN** to move, **OK** to select, **BACK** to go up a level. The menu auto-returns to the idle face after a period of inactivity. The IR remote's dedicated light/fan buttons toggle those relays directly without opening the menu.

## Firebase Sync

The device talks to a Firebase Realtime Database over plain HTTPS REST (no SDK) under `/deskbuddy/`:

| Path | Direction | Purpose |
|---|---|---|
| `/deskbuddy/status` | device → cloud, every 5s | Full telemetry snapshot: sensors, relay states/modes, timer, task count |
| `/deskbuddy/light_mode`, `/deskbuddy/fan_mode` | bidirectional | Mirrors the current relay mode; either side (device or an app writing to Firebase) can change it, and the change propagates within a few seconds |
| `/deskbuddy/command` | cloud → device, polled every 2.5s | One-shot remote commands: `light_mode`/`fan_mode`, `add_task`, `set_timer_minutes`; cleared once processed |

## Persistence

Everything below survives a reset, stored in the ESP32's NVS flash (`Preferences`, namespace `deskbuddy`):

- Relay modes (light/fan)
- Task list
- Saved WiFi credentials

## License

No license file is currently included. If you plan to share or open-source this project, consider adding one (MIT is a common choice for hobby/embedded projects like this).

# SafeRover

A small autonomous rover that demonstrates two active automotive safety systems —
**Autonomous Emergency Braking (AEB)** and **Lane Keeping Assist (LKA)** — on an ESP32.

> 🚧 **Status:** In active development. Phase 0 (toolchain + environment) complete;
> hardware integration in progress. See the [roadmap](#roadmap).

SafeRover runs two independent closed control loops (sense → decide → act) on top of a
real-time state machine.

---

## What it does

SafeRover drives on a small track and runs in three modes:

- **Standby** — stopped and ready.
- **Manual** — driven from a phone over Wi-Fi (a dashboard the ESP32 serves).
- **Autonomous** — drives itself, staying inside a marked lane and correcting its own steering.

Two safety layers run on top of *every* mode:

### 🛑 AEB — Autonomous Emergency Braking
A front ultrasonic sensor measures distance continuously and brakes in three escalating
stages, like a real car:

`WARN` (buzzer + brake lights) → `SLOW` (forced deceleration) → `STOP` (full brake)

…with **hysteresis** to prevent jitter at the thresholds.
**This layer overrides every command — it is active even in manual mode.** You cannot drive
the rover into a wall.

### 🛣️ LKA — Lane Keeping Assist
Two infrared sensors detect the lane edges. A **proportional (P) controller** gradually
corrects the steering and slows the rover down in curves.

Around the two safety systems: an on-board **OLED** status display, a **phone dashboard**
with live telemetry and a brake-event log, a physical **mode button**, and every braking
event pushed to the **cloud** (Adafruit IO) with an **NTP** timestamp — building a
remotely-viewable history.

---

## Hardware

| Role | Part |
|------|------|
| Controller | ESP32 DevKit V1 (30-pin) |
| Motor driver | L298N + 2× DC motors |
| Distance | HC-SR04 ultrasonic |
| Lane sensing | 2× TCRT5000 IR |
| Display | 0.96" SSD1306 OLED (I²C) |
| Indicators | brake LEDs, buzzer, status LED |
| Input | physical mode button |

**Steering — differential, no servo.** Turning is a *speed difference* between the two
wheels: the rover turns toward the slower side.

**Power.** Motors run from a 6×AA pack (~9 V); the ESP32 runs from a separate 5 V power
bank; the two supplies share a common ground. Separating them prevents brownout resets
when the motors spike current.

<details>
<summary><b>Planned pin map (wiring contract)</b></summary>

| Component | Signal | ESP32 pin | Notes |
|-----------|--------|-----------|-------|
| L298N | ENA / IN1 / IN2 | 32 / 33 / 25 | left motor (ENA = PWM) |
| L298N | IN3 / IN4 / ENB | 26 / 27 / 14 | right motor (ENB = PWM) |
| HC-SR04 | TRIG / ECHO | 5 / 18 | ECHO via a 1k/2k voltage divider |
| Line sensor L / R | AO / AO | 34 / 35 | analog, ADC1, input-only pins |
| OLED | SDA / SCL | 21 / 22 | I²C, address 0x3C |
| Buzzer | + | 4 | |
| Brake LEDs | anode | 16 | via 220 Ω each |
| Status LED | anode | 17 | via 220 Ω |
| Mode button | — | 23 | `INPUT_PULLUP` (pressed = LOW) |

Pins avoid the ESP32 boot-strapping pins and ADC2 (disabled when Wi-Fi is on), so the
analog line sensors sit on ADC1.

</details>

---

## Architecture

```
[ultrasonic] [2x line sensors] [button] [phone over Wi-Fi]
        \         |          |          /
                 [ ESP32 ]
   AEB safety layer · state machine · LKA · web server
        /         |          \              \
 [L298N + motors] [OLED]  [brake lights+buzzer]  [cloud: NTP + event log]
```

The firmware is split into modules, each built and tested in isolation:

| Module | Responsibility |
|--------|----------------|
| `sensors` | distance (median-filtered), calibrated line reads |
| `motors` | drive / turn / speed (PWM), differential steering, trim |
| `aeb` | WARN / SLOW / STOP stages + hysteresis |
| `lka` | P-controller steering correction |
| `state_machine` | modes + priority (safety overrides everything) |
| `web` + `display` | phone dashboard, commands, OLED |
| `cloud` | NTP time + Adafruit IO events |

**Design rule:** all driving goes through the safety layer — navigation and manual control
call `safeDrive(...)`, never the motors directly, so AEB is impossible to bypass.

---

## Tech stack

- **C++ (Arduino framework)** on the ESP32 — real-time control with a non-blocking
  `millis()` scheduler (no `delay()` in the main loop).
- **[pioarduino](https://github.com/pioarduino/platform-espressif32)** — a community
  PlatformIO fork that tracks the current ESP32 Arduino core.
- Built in **Antigravity** (VS Code-based) with **clangd** IntelliSense.

---

## Build & run

Requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/) (or the pioarduino
IDE extension). From the project root:

```bash
pio run               # compile
pio run -t upload     # flash to the board
pio device monitor    # serial monitor @ 115200 baud
```

The platform and board are pinned in [`platformio.ini`](platformio.ini).

---

## Roadmap

| Phase | Milestone | Status |
|:-----:|-----------|:------:|
| 0 | Environment — toolchain + Blink compiles | ✅ |
| 1 | GPIO basics — Serial, button, LED, PWM | ⬜ |
| 2 | Sensors on the bench — OLED, ultrasonic, line sensors | ⬜ |
| 3 | Vehicle moves — chassis, power, straight-line trim | ⬜ |
| 4 | Phone control — Wi-Fi dashboard + watchdog stop | ⬜ |
| 5 | AEB — 3-stage braking + hysteresis | ⬜ |
| 6 | LKA — track + P-controller tuning | ⬜ |
| 7 | Brain — state machine + non-blocking loop | ⬜ |
| 8 | IoT — live dashboard, NTP, cloud event log | ⬜ |
| 9 | Finish — enclosure, portfolio, dry run | ⬜ |

_Wiring photos and a demo video will be added here as the hardware comes together._

---

## Author

**Ariel Kuznets**

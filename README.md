# SafeRover

A small autonomous rover that demonstrates two active automotive safety systems —
**Autonomous Emergency Braking (AEB)** and **Lane Keeping Assist (LKA)** — on an ESP32.

> 🚧 **Status:** In active development. **The rover brakes for obstacles on its own.**
> AEB runs in three escalating stages with hysteresis, and it cannot be bypassed —
> hold the forward button at a wall from the phone and the rover refuses, while
> reverse and pivot turns stay free so it can always back out of what it stopped for.
> Before that: driving from a phone over the rover's own Wi-Fi with a watchdog that
> stops it when commands stop arriving, and every component wired and calibrated on
> the vehicle. Next is LKA — the second safety system. See the [roadmap](#roadmap).

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

A front ultrasonic sensor measures distance ten times a second and escalates through
three stages, like a real car:

| Stage | At | What happens |
|-------|---:|--------------|
| `WARN` | 90 cm | buzzer only — the speed is untouched |
| `SLOW` | 55 cm | forced ceiling on speed, buzzer and brake lights |
| `STOP` | 35 cm | the motors are shorted through the bridge and held, buzzer and brake lights |

**This layer overrides every command, including manual driving.** Hold the forward
button at a wall and the rover simply refuses. Braking is active rather than coasting,
because momentum would otherwise carry the rover into what it had just detected.

A stage is entered the moment its threshold is crossed but only left **8 cm further
out**, so a reading resting on a threshold cannot flicker across it. Escalating late is
a collision; de-escalating late is a fraction of a second of caution.

Two decisions in it are worth more than the thresholds:

- **Reverse and pivot turns are never limited.** The sensor faces forward, so an
  obstacle ahead only matters while the rover drives at it. A rover pinned against a
  wall with the only commands that would free it refused is a worse failure than the
  one being prevented.
- **A missing echo is never read as clear road.** It is not a distance at all, and what
  it means depends on the last real reading: lost at three metres is open space, lost
  at fifteen centimetres is something close, possibly inside the sensor's blind zone.
  The thresholds themselves were raised after the first runs on the floor — the
  reasoning is in the [journal](docs/journal.md).

### 🛣️ LKA — Lane Keeping Assist
The rover runs inside a white lane marked by two black stripes, with one infrared sensor
on each side. While it stays in the lane both sensors see white and nothing is corrected.
A sensor reads dark only once the rover has drifted far enough to reach a stripe, and that
is what a **proportional (P) controller** acts on to steer it back and to slow it in curves.

Around the two safety systems: an on-board **OLED** status display, a **phone dashboard**
with live telemetry and a brake-event log, a physical **mode button**, and every braking
event pushed to the **cloud** (Adafruit IO) with an **NTP** timestamp — building a
remotely-viewable history.

### 📱 Phone control — and a watchdog that does not trust silence

The rover creates **its own Wi-Fi network** rather than joining one, so a demonstration
never depends on infrastructure nobody in the room controls. A phone joins it and opens a
page served by the ESP32 itself: the sensor readings update live, and a cross of held
buttons plus a speed slider drive the vehicle.

**Only one device may connect at a time.** Two operators sending driving commands at once
is an unsafe state — one brakes while the other accelerates — and refusing the second
association keeps that out of the command code entirely.

The browser does not send *events*, it **repeats the current intent** several times a
second. A lost message costs nothing, because the next one carries the same thing. And the
rover measures the gap between them:

> **If no command arrives for half a second, the rover stops itself.**

That matters because nothing in the system decays on its own. The commanded values hold
whatever arrived last, and the PWM peripheral keeps producing that waveform with no help
from the CPU — so a rover whose operator walked out of range would drive until its battery
ran out. **Silence on a control channel is not consent to keep going.**

Letting go of a button is the ordinary way to stop and takes effect immediately. The
watchdog exists for the case where "stop" cannot be sent at all.

---

## Hardware

| Role | Part |
|------|------|
| Controller | ESP32 DevKit V1 (30-pin) |
| Chassis | CROB2 4WD, 4 motors, no caster |
| Motor driver | L298N + 4× DC motors (4WD, driven as 2 channels) |
| Distance | HC-SR04 ultrasonic |
| Lane sensing | 2× TCRT5000 IR |
| Display | 0.91" I²C OLED — SSD1306, 128×32, at `0x3C` |
| Indicators | brake LEDs, buzzer, status LED |
| Input | physical mode button |

**Steering — differential, no servo.** Turning is a *speed difference* between the two
sides: the rover turns toward the slower side. The two motors on each side are wired in
parallel into a single L298N channel — the driver has only two channels, and motors on
the same side always turn the same way at the same speed, so they need no separate
control. 4WD changes the wiring, not the code: `drive(left, right)` is unchanged.

Because the chassis is 4WD with no caster, all four wheels scrub sideways through a
turn. This is expected, and it will make tuning `Kp` in the LKA phase slower.

**Power.** Motors run from a 6×AA pack (~9 V); the ESP32 runs from a separate 5 V power
bank; the two supplies share a common ground. Separating them prevents brownout resets
when the motors spike current. The chassis's own 4×AA holder is not used.

**The display.** Confirmed in phase 2: a 128×32 SSD1306 answering at `0x3C`, found with
the bus scanner in [`tools/i2c_scanner/`](tools/i2c_scanner/) rather than assumed from a
datasheet. The first module failed — the ribbon bonding its driver to the glass came
loose, a factory heat-press joint that cannot be resoldered, and it would render only
while the module was squeezed by hand. A replacement of the same part works with the
firmware unchanged, which is what confirmed the fault was physical rather than software.

<details>
<summary><b>Planned pin map (wiring contract)</b></summary>

| Component | Signal | ESP32 pin | Notes |
|-----------|--------|-----------|-------|
| L298N | ENA / IN1 / IN2 | 32 / 33 / 25 | left side, 2 motors in parallel (ENA = PWM) |
| L298N | IN3 / IN4 / ENB | 26 / 27 / 14 | right side, 2 motors in parallel (ENB = PWM) |
| HC-SR04 | TRIG / ECHO | 5 / 18 | ECHO via a 1k/2k voltage divider |
| Line sensor L / R | AO / AO | 34 / 35 | analog, ADC1, input-only pins |
| OLED | SDA / SCL | 21 / 22 | I²C — confirm address with a scanner |
| Buzzer | I/O | 4 | active-low module — driven **high** to stay silent |
| Brake LEDs | anode | 19 | via 220 Ω each |
| Status LED | anode | 13 | via 220 Ω |
| Mode button | — | 23 | `INPUT_PULLUP` (pressed = LOW) |

Pins avoid the ESP32 boot-strapping pins and ADC2 (disabled when Wi-Fi is on), so the
analog line sensors sit on ADC1. They are also limited to the lines this 30-pin board
actually breaks out — the chip has more GPIO than the headers expose, and GPIO 16/17
are not among them.

**PWM channel allocation.** The ESP32's eight LEDC channels share four timers in pairs
(0-1, 2-3, 4-5, 6-7), and the frequency is a property of the timer rather than the
channel — two channels on one timer cannot run at different frequencies. Channels are
therefore assigned so that nothing shares a timer: **0** for the brake light, **2** for
ENA (left side) and **4** for ENB (right side).

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

`safeDrive()` was built in phase 4 as an empty seam, before it had any callers, and the
AEB moved into it in phase 5. That is the payoff: **not one line of the code that decides
where to go had to change** when the braking arrived. Adding the seam afterwards would
have meant finding and converting every caller, and a single missed call would be a
silent way around the brakes that no test would catch.

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

The platform, board and library versions are pinned in
[`platformio.ini`](platformio.ini).

The rover serves its own Wi-Fi access point, and its passphrase is deliberately not
in this repository — this repo is public. Create `src/secrets.h` before the first
build, or the compile fails on a missing include:

```cpp
#pragma once
constexpr char WIFI_AP_PASSWORD[] = "your-passphrase";  // WPA2: 8 characters minimum
```

The network name is `Saferover`, and it is in the source: an SSID is broadcast over
the air anyway, so hiding it would protect nothing and only make the network harder
to find.

There is a second environment holding a one-shot I²C bus scanner, used to prove the
display's wiring and address before any display library is loaded. It is kept out of
`src/` so it never ends up compiled into the rover, and it is built explicitly:

```bash
pio run -e i2c-scanner -t upload
```

---

## Roadmap

| Phase | Milestone | Status |
|:-----:|-----------|:------:|
| 0 | Environment — toolchain + Blink compiles | ✅ |
| 1 | GPIO basics — Serial, button, LED, PWM | ✅ |
| 2 | Sensors on the bench — OLED, ultrasonic, line sensors | ✅ |
| 3 | Vehicle moves — chassis, power, straight-line trim | ✅ |
| 4 | Phone control — Wi-Fi dashboard + watchdog stop | ✅ |
| 5 | AEB — 3-stage braking + hysteresis | ✅ |
| 6 | LKA — track + P-controller tuning | ⬜ |
| 7 | Brain — state machine + non-blocking loop | ⬜ |
| 8 | IoT — live dashboard, NTP, cloud event log | ⬜ |
| 9 | Finish — enclosure, portfolio, dry run | ⬜ |

### Progress photos

[`photos/`](photos/) holds a shot of the build at each stage — the breadboard as it grew,
the serial output captured while the line sensors were being calibrated, and the chassis
coming together.

![The finished rover: a two-deck acrylic chassis on four wheels, with the ultrasonic sensor and both line sensor boards at the front, the breadboards and ESP32 across the two decks, and the battery pack at the back](photos/phase3-fully-wired-vehicle.jpg)

A build log with the measurements and the faults hit along the way is in
[`docs/journal.md`](docs/journal.md).

_A demo video will follow._

---

## Author

**Ariel Kuznets**

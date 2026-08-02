# SafeRover — Work Journal

This journal is an internal working tool, not the formal project book. It is written
session by session, while the details are still fresh, and later feeds the official
project book — mainly the chapter on the development process and the chapter on
libraries and faults.

Every entry follows the same template, so an entry can be lifted into the project book
with little rewriting:

- **Goal** — what the session set out to achieve.
- **What was done** — what actually happened.
- **Problems & challenges** — every fault written as `Symptom → Diagnosis → Solution`.
- **Decisions & rationale** — what was chosen and *why*, including what was rejected.
- **Next up** — the exact next action.

Entries run oldest first. Sessions 1 and 2 were written retroactively on 2026-07-28;
from session 3 onward each entry is written the same day.

---

## Session 1 — 2026-06-24 — Development environment setup

### Goal
Get a working ESP32 toolchain running end to end, and prove it by compiling a Blink
program — before any hardware arrives.

### What was done
- Created the PlatformIO project for the ESP32 DOIT DevKit V1.
- Selected and installed the pioarduino platform.
- Configured clangd for IntelliSense.
- Wrote Blink and compiled it successfully — `firmware.bin` generated.
- Moved the project off OneDrive to an ASCII-only path.
- Initialized Git, wrote `.gitignore`, and pushed the first four commits to GitHub.

### Problems & challenges

**Fault 1 — the linker could not write its map file**

- **Symptom:** the build failed. The Xtensa linker (`ld.exe`) reported
  `cannot open map file`. Separately, builds intermittently failed on locked files.
- **Diagnosis:** the project was sitting under `OneDrive\מסמכים\…`, a path containing
  non-ASCII (Hebrew) characters, and `ld.exe` cannot handle non-English characters in
  paths. The file locking was a second, independent problem: OneDrive syncs files
  while the build is still writing its output.
- **Solution:** moved the project to `C:\Ariel\…` — an ASCII-only path outside any
  cloud-synced folder. Both symptoms disappeared. This is now a permanent constraint
  on the project.

**Fault 2 — the editor reported errors on code that compiled fine**

- **Symptom:** red underlines in the editor on code the compiler accepted without
  complaint.
- **Diagnosis:** IntelliSense and compilation are two separate paths. clangd is an
  independent LLVM parser that serves the IDE only; the real build runs
  `xtensa-esp32-elf-gcc`, a cross-compiler that runs on the PC and produces code for
  the Xtensa LX6. The compiler is passed Xtensa-specific flags (`-mlongcalls`,
  `-mfix-esp32-psram-cache-issue`, `-march=…`) that clangd does not recognize.
- **Solution:** added a `.clangd` file that strips those flags before clangd sees
  them. Standing conclusion for this project: **a red underline in the IDE is not a
  compile error. The only authority is the build result — SUCCESS or FAILED.**

### Decisions & rationale

- **pioarduino instead of the official PlatformIO platform.** The official
  `espressif32` platform stopped tracking the current Arduino-ESP32 core; pioarduino
  is the maintained community fork that follows it. `platformio.ini` therefore points
  `platform` at a pioarduino release URL rather than at `espressif32`. The official
  platform was tried first and rejected for this reason.
- **`.pio` is excluded from Git.** Build output is regenerated from the shared
  toolchain in `C:\Users\1979d\.platformio` (~5.9 GB — compiler, framework and
  flashing tools, shared across all projects). The repository holds source and
  configuration only, so a clone on another machine rebuilds rather than downloads.
- **ASCII-only path, outside OneDrive** — see Fault 1. Not a preference; a hard
  constraint that must not be regressed.

### Next up
Upload Blink to the physical board once hardware arrives, to verify the chain past
the compiler.

---

## Session 2 — 2026-07-07 — First upload to hardware

### Goal
Verify the toolchain past the compiler: get code from the editor onto the physical
ESP32 and watch it run.

### What was done
- Rebuilt Blink — `firmware.bin` generated.
- Flashed the board over USB for the first time.
- **The on-board LED blinked.** The full edit → build → flash → run chain is verified
  on real hardware.
- Phase 0 (environment) complete.

### Problems & challenges

**Fault — unclear which copy of the project was the live one**

- **Symptom:** more than one SafeRover folder existed on the machine, and it was not
  obvious which one the IDE was building. Time was lost editing in one place while
  building in another.
- **Diagnosis:** the move off OneDrive in session 1 copied the project rather than
  relocating it, leaving the original in place. Two trees with the same name coexisted
  and their configuration had since diverged.
- **Solution:** partial at this stage — work continued in the correct folder, but the
  duplicates were left in place. Fully resolved in session 3.

### Decisions & rationale
- **Verify on hardware before writing more code.** A program that only compiles proves
  the compiler works; it says nothing about the flashing tool, the USB-serial driver,
  or the board itself. Testing the whole chain with a trivial program means that any
  later failure points at the new code rather than at the setup.

### Next up
Clean up the duplicate project folders, then begin Phase 1.

---

## Session 3 — 2026-07-28 — Environment cleanup and journal setup

### Goal
Remove the duplicate project copies for good, and open this journal.

### What was done
- Mapped every SafeRover folder on the machine. Found three: the correct one, a stale
  copy inside OneDrive, and a third entry that turned out to be a link into that copy.
- Compared the trees file by file to confirm the stale copy held nothing unique, and
  confirmed the live repository was fully pushed to GitHub before deleting anything.
- Removed the link first, then the two real folders. One project folder remains.
- Created this journal and the `photos/` directory.

### Problems & challenges

**Fault — one of the "copies" was a link, not a folder**

- **Symptom:** three SafeRover directories appeared on the machine, seemingly
  identical.
- **Diagnosis:** `C:\PlatformIO\Projects\SafeRover` was a junction — a filesystem link
  pointing at the OneDrive copy, not separate data. Deleting it recursively risked
  following the link and destroying the target instead of just removing the pointer.
- **Solution:** removed the link itself without recursion, verified the target was
  still intact, and only then deleted the real folders. The stale copy was also found
  to still carry the old broken configuration (`platform = espressif32`), which
  independently confirmed which tree was the correct one.

### Decisions & rationale
- **Verify before deleting.** Both trees were compared file by file to prove nothing
  unique would be lost, and the repository's sync state with GitHub was checked as a
  fallback. Deletion is not reversible and the check cost minutes.
- **Keep the journal in the repository, written in English.** It is written per
  session rather than reconstructed at the end, so the Git history itself shows the
  documentation evolving alongside the code.

### Next up
Phase 1 — GPIO basics: serial output, read the mode button (`INPUT_PULLUP` on
GPIO 23), drive an LED, then PWM via LEDC.

---

## Session 4 — 2026-07-28 — First brake light, and a pin map that did not survive contact

### Goal
Wire the first brake light on a breadboard and verify it, as the first physical
component of Phase 1.

### What was done
- Built an LED circuit on a breadboard: a 220 Ω current-limiting resistor in series
  with the LED, fed from an ESP32 pin and returned to GND.
- Verified the circuit against a fixed 3V3 supply before connecting it to a
  controlled pin. The LED lit.
- Moved the feed wire from 3V3 to GPIO 19.
- Updated the pin map after discovering the planned pins do not exist on this board.
- Uploaded the first non-blocking blink code to the board. The brake light blinks on
  GPIO 19, and the serial output reports the state on every toggle. Circuit, pin and
  code are now verified end to end.

### Problems & challenges

**Fault — the pins in the planned map are not broken out on this board**

- **Symptom:** the planned pin map assigned GPIO 16 to the brake lights and GPIO 17
  to the status LED. On physical inspection of the board, neither pin is marked
  anywhere along its edge.
- **Diagnosis:** the ESP32 chip has more GPIO lines than the development board routes
  out to its headers. On this 30-pin DevKit V1, 16 and 17 are not among them. The
  pins actually available are: 13, 12, 14, 27, 26, 25, 33, 32, 35, 34, 15, 2, 4, 5,
  18, 19, 21, 22, 23. The pin map had been written from the chip's capabilities
  rather than from this board's headers.
- **Solution:** brake lights moved to GPIO 19, status LED to GPIO 13.

**Fault — the first replacement pin collided with an existing assignment**

- **Symptom:** GPIO 18 was the initial candidate for the status LED, but the pin map
  already assigns GPIO 18 to the HC-SR04 ECHO line.
- **Diagnosis:** cross-checking the remaining free pins against the whole contract —
  not just against the pins already wired — left only 13, 12, 15 and 2 unassigned.
  Three of those four carry boot-strapping duties: GPIO 2 drives the built-in LED and
  affects boot, GPIO 15 (MTDO) silences the boot log if held low, and GPIO 12 (MTDI)
  selects the flash voltage at power-on.
- **Solution:** GPIO 13, the only remaining pin with no boot-strapping role.

### Decisions & rationale

- **Verify against a fixed voltage before moving to a controlled pin.** If the LED
  lights on 3V3, then the circuit, the LED polarity and the resistor value are all off
  the suspect list. Any fault after that point can only be in the code or the pin —
  which turns one open-ended problem into a much smaller one.
- **GPIO 2 was considered and rejected** for the brake light. It is tied to the
  board's built-in LED, so the two would always light together, and it is a
  boot-strapping pin whose level at power-on affects how the board starts.
- **The pin map must be checked against the board, not the chip.** The remaining
  assignments were re-verified against the available list at the same time, so the
  rest of the contract is now known to be physically wireable.

### Next up
Status LED on GPIO 13, then the mode button on GPIO 23 using `INPUT_PULLUP`.

---

## Session 5 — 2026-07-29 — Status LED, mode button, and three wiring faults

### Goal
Wire the status LED and the mode button, completing the input/output components
of Phase 1.

### What was done
- Built a second LED circuit on the same breadboard for the status LED.
- Wired the mode button using the ESP32's internal pull-up, with no external
  resistor.
- Observed contact bounce in the serial terminal and added a 50 ms software
  debounce.

### Problems & challenges

**Fault 1 — the breadboard power rails are not continuous**

- **Symptom:** the status LED did not light at all and the button produced no
  events, while the brake light that already worked kept running normally.
- **Diagnosis:** the 830-point board has four edge rails, a pair on each side,
  and they are not joined to each other. The new components were connected to
  the rails on one side while the controller's GND went to the other side. The
  brake light was unaffected because it is fed directly from GPIO 19 rather than
  through a rail.
- **Solution:** moved every component onto the same GND rail the controller is
  connected to. The status LED is now fed straight from GPIO 13 instead of going
  through a power rail.

**Fault 2 — the tactile button was permanently closed**

- **Symptom:** the button read as permanently pressed from boot, and pressing it
  changed nothing. Swapping in a different button did not change the behaviour.
- **Isolation:** an isolation chain ruled out each layer in turn — the code, the
  pin, and GND. Jumping GPIO 23 to GND by hand produced correct PRESSED and
  RELEASED events, which cleared everything below the switch. Pulling only the
  button, without touching any wire, made the symptom disappear and pinned the
  fault on the component itself.
- **First diagnosis — later disproven:** the switch's leg spacing differs between
  its two axes, so the internally connected pair runs along the length of the
  board rather than across it. This is what was written down at the time, and the
  wiring was changed on the strength of it.
- **Re-check:** that description does not fit the layout that actually works. The
  working wiring has the two jumpers in two consecutive rows on the *same* side of
  the centre channel. For that to close anything, each internally connected pair
  has to run *across* the board — one pair per row, its legs emerging on both
  sides of the channel — and the switch closes between the two rows. The first
  explanation had the pairs running the other way, which contradicts this.
- **Actual cause:** the original layout put both jumpers on the same horizontal
  line, one on each side of the channel. Both were therefore landing on the same
  always-connected pair, tying GPIO 23 to GND permanently, independent of any
  press.
- **Solution:** two jumpers in two consecutive rows, so each faces a different
  pair and the switch closes between them. The button started working.
- **Confidence at the time:** that reading came from visual inspection of the
  working wiring and from ruling out the alternatives, not from direct
  measurement, so the entry was left open pending a multimeter check in
  resistance mode. The measurement has since been taken and it agreed with the
  reading; see the verification below.
- **Verification:** measured with an Omega DT830D in resistance mode on the 200 Ω
  range — the unit has no continuity mode with a buzzer. All power was
  disconnected and both control wires were pulled out to isolate the button, and
  four jumpers were pushed into the four junctions to bring the contact points
  off the board where the probes can reach them. A control reading came first, to
  calibrate what "connected" looks like: two jumpers in two holes of the same
  breadboard row — a guaranteed connection — read 1.5 Ω. That is the overhead of
  the measuring setup itself, the probes, the jumpers and the spring contacts,
  and it is the definition of "connected" for every reading below. The four
  junctions are named by which side of the centre channel they sit on: A1 and B1
  are the two rows to the left of it, one carrying GPIO 23 and the other GND;
  A2 and B2 are the rows facing them across the channel.

Readings with the button not pressed:

| Pair | Reading | Meaning |
|---|---|---|
| A1-B1 | OL | open |
| A1-A2 | 1.5 Ω | connected |
| B1-B2 | 1.5 Ω | connected |
| A1-B2 | OL | open |
| B1-A2 | OL | open |
| A2-B2 | OL | open |

The contact pairs run across the channel. A1-A2 are one permanently joined pair,
B1-B2 are the other, and at rest there is no path of any kind between them.
A1-A2 read exactly the control value, which means a continuous metal path with no
component in between.

This measures the original failure mechanism directly. The layout that failed put
GND and GPIO 23 on the same row number on opposite sides of the channel —
junctions A1 and A2 — which the table now shows are permanently joined. The two
control wires were shorted together through the button's internal metal,
bypassing the switch contacts entirely, so the pin read LOW at all times.

- **Not measured:** the same six pairs with the button held down. That round was
  skipped because it needs two probes and the button held at once. The switch
  element itself therefore rests on behavioural evidence — the Phase 1 firmware
  reads press events correctly, with edge detection and debounce — and not on a
  direct measurement.
- **Method note:** a fix that works is not evidence that the explanation for it is
  correct. Here the fix was right from the start while the reasoning behind it was
  not, and the gap only surfaced on a second look. The two were verified
  separately.

**Fault 3 — contact bounce**

- **Symptom:** a single press sometimes produced a pair of PRESSED/RELEASED
  events milliseconds apart.
- **Diagnosis:** normal physical behaviour of any mechanical switch. The contacts
  bounce for 1-20 ms before settling, and the non-blocking loop is fast enough to
  read every bounce as its own event.
- **Solution:** a `millis()`-based software debounce with a 50 ms threshold —
  above the typical bounce duration and below the 100-150 ms gap between even
  fast human presses.

### Decisions & rationale

- **Isolate before replacing.** No component was swapped until each layer had
  been ruled out separately: code, pin, GND, then the switch. Jumping the pin
  straight to GND acted as a "perfect button" and tested everything underneath
  the switch in one step.
- **An unverified assumption about a component's internals can produce a wrong
  diagnosis.** The first explanation for this fault was itself an untested
  assumption, and it survived until the entry was read back against the working
  board. The lesson recorded for the rest of the project: measure a switch's
  contact mapping before assuming how it sits on the board.
- **Confirm through the inverse symptom.** Removing the button and watching the
  symptom vanish was the decisive evidence, because it isolated the component
  without changing anything else in the circuit.

### Next up
PWM for brightness and speed control, toward integration with the L298N in the
next phase.

---

## Session 6 — 2026-07-29 — PWM, and the end of Phase 1

### Goal
Close Phase 1 by adding PWM control.

### What was done
- Moved the brake light from plain on/off switching to LEDC PWM, cycling through
  four duty steps.
- Verified it both by eye — four distinct brightness levels — and in the serial
  output, which reports every step.

### Problems & challenges
None. The change built and ran correctly the first time.

### Decisions & rationale

- **Implemented on an LED rather than a motor**, because the motors are not wired
  yet and the LED gives immediate visual confirmation that the mechanism works.
  The logic itself carries over unchanged to the ENA/ENB pins when the rover
  starts driving.
- **LEDC over `analogWrite`.** LEDC is a hardware peripheral: once a channel is
  configured it generates the waveform on its own, with no CPU involvement, so
  the main loop stays free for sensor reads.

### Next up
Phase 2 — sensors on the bench: OLED, ultrasonic, line sensors.

---

## Session 7 — 2026-07-31 — Line sensors on the bench

### Goal
Verify both line sensors on the bench and measure the reference points the LKA
will be built on.

### What was done
- Wired two TCRT5000 modules (HW-870) on GPIO 34 and 35, analog output only.
- Wrote read-only code — no thresholds — and used it to find the working height
  and the reference values by experiment.

Measurements, taken at a working height of 3.5 cm and averaged over 16 samples:

| Condition | Left | Right | Delta (L−R) |
|---|---|---|---|
| Both over white | ~60 | ~70 | ~−10 |
| Both over black | ~4028 | ~4006 | ~+20 |
| One over each colour | — | — | ~4030 |

Noise on a steady reading is about ±7. Note the direction: **a high value means a
dark surface**, which is the opposite of the intuitive reading.

### Problems & challenges

**Fault 1 — the wrong working height hid the signal**

- **Symptom:** at a height of about 8.5 cm the gap between white and black was
  only around 300 counts — under 8% of the range.
- **Diagnosis:** the TCRT5000 is a short-range part, and the strength of the
  reflected light falls off with the square of the distance. At that height almost
  nothing was coming back from the module's own LED, so the difference being
  measured was mostly reflected ambient light rather than the sensor's own signal.
  The readings were also found to depend on the room lighting, because the
  phototransistor responds to any infrared reaching it, not only to light from the
  module's LED.
- **Solution:** lowered the working height to 3.5 cm and fixed the sensor in the
  breadboard instead of holding it by hand. The gap rose to about 4030 counts at
  roughly ±7 of noise, and the dependence on room lighting became negligible except
  under strong direct light. Being close to the surface is what lets the LED's own
  signal swamp the ambient light.

**Fault 2 — slight coupling between the PWM and the analog reading**

- **Symptom:** the line sensor readings rise by a consistent 10 to 15 counts as the
  brake light's duty increases, across all four steps.
- **Diagnosis:** the effect tracks the duty exactly and is not random noise. It was
  not visible in the first measurement because the values there were high and the
  noise covered it. The likely mechanism is optical rather than electrical: the
  brake light sits close to the sensors on the same breadboard, so part of its
  light reaches the phototransistor directly and adds to what the sensor sees. This
  has not been confirmed by measurement — covering the LED and watching whether the
  effect disappears would settle it.
- **Solution:** none needed at this stage. 15 counts out of a 4030 range is under
  half a percent and does not affect detection. Two things carry forward from it.
  If the cause is optical, the brake light and the line sensors must not end up
  facing each other once they are mounted on the chassis. And the readings should be
  checked again with the motors running, since those add electrical noise of their
  own regardless of what causes this particular effect.

### Decisions & rationale

- **Analog output (AO) rather than the digital output (DO).** The LKA is built on
  a proportional controller that corrects in proportion to how far off the line the
  rover is. DO returns a binary answer after comparing against a threshold, which
  does not carry that magnitude.
- **Modules powered from 3.3 V rather than 5 V.** The AO output cannot exceed its
  own supply voltage, so running the modules at 3.3 V means the output can never
  exceed what an ESP32 pin tolerates. That removes the need for a voltage divider.
- **Both sensors on ADC1 (GPIO 34 and 35).** ADC2 is unusable while Wi-Fi is
  running because it shares hardware with the radio. Choosing ADC2 would have made
  the sensors stop working at the moment Wi-Fi was switched on — a fault that is
  hard to trace back to its cause. 34 and 35 are also input-only pins.
- **No per-sensor correction factor.** The two sensors read within 10 to 20 counts
  of each other over the same surface, which is under half a percent of the working
  range. Calibrating them individually would add complexity for no measurable gain.
- **The OLED was deferred.** Its pin header arrived unsoldered, and an unsoldered
  connection would have introduced intermittent contact faults. The part was put off
  until it can be soldered rather than risking that.

### Photos

| File | What it shows |
|---|---|
| `phase2-line-sensors-closeup.jpg` | Both TCRT5000 modules mounted on the breadboard alongside the brake light, status LED and mode button |
| `phase2-line-sensors-test-setup.jpg` | The bench arrangement used for the measurements. The tin serves as a stand — the white sheet and the black surface were leaned against it in front of the sensors |
| `phase2-serial-output-over-black.jpg` | Serial output with both sensors over a dark surface — the readings near 4030 and the delta near 20 |
| `phase2-workspace-overview.jpg` | The wider bench, including the parts not yet in use |

### Next up
The OLED once its header is soldered, then the HC-SR04 with a voltage divider,
which needs a multimeter to verify before it is connected to a pin.

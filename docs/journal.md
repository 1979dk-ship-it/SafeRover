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

---

## Session 8 — 2026-08-02 — HC-SR04: divider design, verification and characterisation

### Goal
Bring the ultrasonic sensor up on the bench without damaging a GPIO, and measure
what the sensor actually does before any AEB logic is designed against it.

### What was done
- Designed and built a 1k/2k voltage divider for the ECHO line, and verified it in
  three stages before it was ever connected to a pin.
- Wired the sensor: TRIG on GPIO 5 direct, ECHO on GPIO 18 through the divider,
  the module fed from VIN at 5.1 V, all grounds on a common node.
- Wrote the read code — trigger pulse, `pulseIn` with an explicit timeout, and the
  conversion to centimetres — and used it to characterise the sensor.

**Why the divider is needed at all.** The module runs on 5 V, so its ECHO pulse
comes out at 5 V. ESP32 pins are 3.3 V parts, and the protection diodes on the
input start conducting somewhere above about 3.6 V. The damage from that is
usually cumulative rather than immediate, which is what makes it dangerous: the
pin keeps working, and then starts returning wrong readings days later, by which
point the suspicion has already been aimed at the code.

**The idea the whole design rests on: a part's signal voltage is set by its supply
voltage.** A logic gate represents "on" by driving its output up to its own
supply, so a part living on 5 V cannot produce a 3.3 V signal. The problem is
therefore not the sensor's supply — it is the signal that supply produces. That is
why the fix does not touch the supply at all, and intercepts the signal on the way
instead.

**TRIG gets no divider.** The danger only exists in one direction. The ESP32 puts
out 3.3 V and the HC-SR04's detection threshold is around 2 V, so 3.3 V into a
5 V input reads as HIGH and works. 5 V into a 3.3 V input is the case that does
damage.

### The three-level verification

One principle ran through the whole session: measure something whose answer is
already known, to calibrate both the instrument and the way its output is read,
before measuring something whose answer is not known.

| Level | Measured | Expected | What it establishes |
|---|---|---|---|
| 1. Resistance, no power | top 960 Ω, bottom 1970 Ω, the two in series 2.98 kΩ | the parts add up to the whole | the circuit is complete and mechanically stable |
| 2. Static voltage | 5.1 V in, 3.42 V out | 5.1 * 1970 / 2930 = 3.43 V | it behaves the way the model predicts, not merely "works" |
| 3. At rest, sensor on supply only, no TRIG and no GPIO | sensor 5.1 V, divider out 0 V | ECHO idle low | the sensor has power *and* its output sits low |

Level 1's consistency test is the point of it: the two parts add to 2.93 kΩ
against 2.98 kΩ measured directly across the pair, which agree to within about 2%
— inside the meter's accuracy across two different ranges. A nominal resistor
carries a 5% tolerance, so every calculation above uses the measured values rather
than the nominal ones.

Level 3 needs both of its readings together. A 0 V output on its own does not
distinguish a healthy sensor whose ECHO line idles low from a sensor receiving no
power at all. Those two faults look identical in a single measurement.

**Caveat on level 3.** The sensor's supply was measured at the board's VIN pin
rather than at the sensor's own VCC leg, so the jumper between them was never
verified directly. That is the first thing to check if the sensor behaves oddly.

### Problems & challenges

**Fault 1 — resistance readings that did not add up**

- **Symptom:** the top section measured 960 Ω, the bottom 1450 Ω, and the pair in
  series 1846 Ω. The whole was smaller than the sum of its parts, which is not
  physically possible in a series circuit.
- **First diagnosis — wrong:** the fault was assumed to be mechanical. The resistor
  legs were long and twisted and could have shifted between measurements, so the
  circuit was pulled apart and rebuilt with short legs bent at right angles.
- **Actual cause:** the meter was still on the 200 Ω range, which cannot display
  anything above 200. The readings were not valid measurements at all.
- **The clue that was missed:** 1450 Ω is not a standard resistor value. A number
  that does not look like a component usually is not one.
- **Solution:** switched to the correct range and re-measured, giving the values in
  the table above.
- **Method note:** when numbers contradict each other, the first suspect is the
  instrument, not the circuit. Here the hardware was rebuilt before the validity of
  the measurement had been established — against the "isolate before replacing"
  principle already recorded in session 5.

**Fault 2 — the two resistors were installed the wrong way round**

- **Symptom:** the divider output measured 1.67 V instead of the expected 3.42 V.
- **Diagnosis:** 1.67 / 5.1 is almost exactly one third — precisely the inverse of
  the two thirds expected. The divider formula depends on the *bottom* resistor, so
  an output of one third means the bottom resistor is the 1k rather than the 2k.
- **Solution:** swapped the two, and explicitly re-measured the top resistor on its
  own before reconnecting power.
- **Why the earlier measurements did not catch it — the main point of this fault:**
  addition is commutative. Two resistors in series sum to the same value in either
  order, so the level 1 consistency test is blind to which one is which.
  **A resistance measurement proves the circuit is complete. Only a voltage
  measurement proves it is correct.** The two answer different questions and one
  does not substitute for the other.

**Fault 3 — TRIG on the wrong pin**

- **Symptom:** with everything wired and the code running, every sample returned
  `echo=0us`, for hundreds of consecutive lines, including with the sensor aimed
  squarely at a wall.
- **Diagnosis:** a total failure rather than a partial one points at wiring or
  protocol, not at environmental conditions. An angled surface or an absorbing
  material would have produced intermittent failures, not an unbroken run of zeros.
  The TRIG wire was connected to a pin other than the one the code drives.
- **Mechanism:** without TRIG the sensor is never told to measure. It is powered and
  healthy but never transmits, so the ECHO line stays low and `pulseIn` reaches its
  timeout on every call.
- **Solution:** moved TRIG to the pin in the wiring contract. The exact wrong pin
  was not written down at the time, so it is not recorded here.
- **Method note:** this is the same root cause as the pin-map fault in session 4 —
  a gap between what the code assumes and what is physically wired. There the
  source was a pin map written from the chip's datasheet rather than from the board.

**A misdiagnosis caught before it was implemented**

This one is recorded separately from the faults above because nothing was fixed —
the diagnosis was wrong and the fix was never applied.

- **What was seen:** once the sensor started returning distances, an early log held
  long runs of readings near 10,000 microseconds, converting to about 172 cm.
- **The hypothesis:** that 172 cm was the `pulseIn` timeout being reported as a
  valid distance. An instruction was drafted to reject any reading above a ceiling
  derived from the timeout.
- **What disproved it — two checks:** a second log contained valid readings passing
  freely through 172 cm and up to 237 cm; if 172 had been a ceiling, nothing could
  have crossed it. And aiming the sensor at open space printed the "no reading"
  marker as intended, confirming the existing timeout handling was already correct.
  The locked run was an accurate measurement of a wall that was genuinely there.
- **Where the reasoning went wrong:** the argument was that a real distance
  fluctuates, so a locked run is suspicious. In fact the consistency was evidence of
  measurement *quality*, and it was read as evidence of failure. The question that
  settled it — what was actually in front of the sensor — was never asked before the
  diagnosis was written.
- **What the near-miss would have cost:** rejecting readings above an upper
  threshold would have deleted real obstacles at long range. For an AEB system that
  is the worst direction to fail in: an obstacle that goes unreported.
- **Method note**, alongside the line already in session 5 that a fix which works is
  not evidence that the explanation for it is correct: a diagnosis that sounds
  convincing is not a diagnosis that has been tested. The check that settled this
  one took under a minute and had been available from the start.

### Sensor characterisation

Measured for use when the AEB thresholds are designed in phase 5.

- **Noise floor is very low.** On stable runs against a fixed target, consecutive
  readings moved within less than a tenth of a percent — for example 209.5 to
  209.8 cm.
- **Continuous motion is tracked faithfully.** Moving a target showed gradual
  progression from about 20 cm out to about 237 cm and back, with no jumps.

Physical limits that constrain the thresholds. These are properties of ultrasound,
not faults in the part:

| Limit | Effect |
|---|---|
| Below about 2 cm | Unreliable — the transmitter is still ringing when the echo returns |
| Surface angled more than about 15° | The wave reflects away from the receiver rather than back to it |
| Soft materials (cloth, foam) | Absorb ultrasound and may return no echo at all |
| Beam lobe about 15° | A narrow object at long range can be missed entirely |

**For AEB design:** the STOP threshold has to sit well above 2 cm — not only for
braking distance, but because below that the sensor is blind.

### Decisions & rationale

- **The divider intercepts the signal rather than lowering the supply.** The supply
  cannot be lowered here: the ultrasonic transmitter needs 5 V to produce a strong
  enough wave. So the 5 V stays and the signal is divided on its way to the pin.
- **The opposite decision was taken for the line sensors, from the same principle.**
  There the modules were deliberately fed 3.3 V rather than 5 V, because the AO
  output cannot exceed its own supply — which removed the need for a divider
  entirely. Same rule, two opposite choices, decided by what each part allows.
- **1k and 2k specifically.** The division ratio depends on the ratio between the
  resistors, not their absolute values, but the order of magnitude is not arbitrary.
  Too small and the current through the divider rises and loads the ECHO output.
  Too large and stray capacitance slows the edges of the pulse — which matters more
  here than in most dividers, because `pulseIn` measures the *width* of that pulse
  and the width is the distance. A smeared edge is a wrong distance. High impedance
  is also more susceptible to picked-up noise, which becomes a real consideration
  near motors and PWM in the phases ahead.
- **Verify at three levels before connecting, not after.** A destroyed GPIO cannot
  be undone, and this board has no spare pins left. The cost of the three
  measurements is a few minutes; the cost of skipping them is the board.
- **No median filter yet.** The project convention requires one on this sensor, and
  it will be added. It is deliberately absent at this stage so the raw noise can be
  measured first — the filter should be designed against real numbers rather than
  guessed at ahead of them.

### Open risk — loop timing

Recorded here for the first time, to be dealt with in phase 7.

`pulseIn` is a blocking call: while it waits for the pulse, the rest of the loop
stops. With the timeout it can hold for tens of milliseconds. The line sensors
have a smaller version of the same problem — their 16-sample averaging burst
blocks for roughly 2.4 ms per read.

The planned median filter makes the ultrasonic case worse, not better: it triples
the number of samples and adds a gap between them, so it multiplies the blocking
time. That trade is being accepted knowingly — protection against single missed
echoes on a moving, vibrating vehicle matters more than the timing budget does at
bench stage.

In phase 7, with every subsystem sharing one loop, this may have to become a
non-blocking interrupt-driven measurement, or the samples may have to be spread
across several loop passes.

### Next up
The OLED once its header is soldered — its controller chip and resolution are both
still unconfirmed.

---

## Session 9 — 2026-08-03 — Median filter on the distance reading

### Goal
Protect the distance reading from single bad samples, before anything that brakes
is allowed to trust it.

### What was done
- Moved the read from one sample per measurement to three consecutive samples,
  returning the middle one.
- Added the rule for what happens when samples fail, and verified the change.

Two checks were run afterwards. Readings against a fixed target stayed stable. And
as a regression check, aiming the sensor at open space still printed the "no
reading" marker, which confirms the filter did not disturb the timeout handling
that was already working.

### Problems & challenges
None. The change built and behaved correctly the first time.

### Decisions & rationale

- **A median rather than an average.** An average is pulled toward an outlier in
  proportion to how far out that outlier is, and it produces a number that no
  sample actually measured. A median ignores the extremes outright, so a single
  wild value simply disappears. The filter is aimed at one bad jump, not at
  continuous noise.

- **The honest reason it was added.** The noise measured on the bench was very low
  — under a tenth of a percent across stable runs against a fixed target. This
  filter is therefore *not* a response to measured noise. It is protection
  prepared in advance for conditions the bench cannot reproduce: vibration,
  surface angles that change while the rover is moving, and obstacles crossing in
  and out of the beam lobe. Single missed echoes are expected there. That is worth
  writing down plainly rather than presenting the filter as something a
  measurement demanded.

- **Two of the three samples must return an echo, or the result is "no reading".**
  When most of the samples fail, the likely truth is that the target genuinely
  returns no echo — an absorbing surface, too sharp an angle, or no obstacle at
  all. Reporting a distance on the strength of the one sample that survived would
  be a guess presented as a measurement. A safety system is better off declaring
  that it has no information than stating a wrong value confidently. This is the
  same principle recorded in session 8, in the misdiagnosis caught before it was
  implemented: a timeout means "no information", never "a large distance".

- **The timing cost, accepted knowingly.** `pulseIn` blocks. It waits in a loop
  for the pulse to start and then for it to end, and nothing else runs for that
  whole time. How long it waits depends on the distance, because the pulse *is*
  the measurement — a near obstacle gives a short pulse, a far one a long pulse.
  The worst case is when there is no echo at all: the pulse never starts and the
  function waits out the full timeout. The system spends the most time exactly
  when there is nothing to see.

  Tripling the samples and adding a gap between them multiplies that blocking time
  accordingly. By construction the worst case goes from about 25 ms to about
  85 ms — three timeouts plus two gaps — against a 100 ms measurement interval.
  These are derived from the constants, not measured. The trade was made
  deliberately: a wrong reading is more dangerous than a late one. A single spike
  in an AEB system means either braking for nothing in mid-drive, or an obstacle
  that vanishes for a moment while the system fails to stop.

- **Left blocking for now, on purpose.** The alternative is a non-blocking
  interrupt-driven measurement — recording timestamps when the pin changes state,
  so the processor keeps working between the start and the end of the pulse. That
  is not being done yet, because the simple implementation should be proven to
  work before it is optimised, and the optimisation should be driven by a
  measurement rather than by an estimate.

### Open risk carried into phase 7
In a unified loop, this blocking time adds to the blocking already noted for the
line sensor sampling burst. Both are recorded under the open timing risk in
session 8; this entry only makes the ultrasonic side larger.

### Next up
The OLED once its header is soldered — its controller chip and resolution are both
still unconfirmed.

---

## Session 10 — 2026-08-03 — OLED bring-up, and a display that cannot be repaired

### Goal
Bring the 0.91" I2C OLED up on the bench and get a test screen onto it.

### What was done
- Built an I2C bus scanner as a **separate PlatformIO environment** under
  `tools/i2c_scanner/`, kept out of `src/` so it never shares a `setup()` with
  the firmware and is never compiled into the vehicle. A plain `pio run` still
  builds only the rover firmware; the scanner is flashed on request with
  `pio run -e i2c-scanner -t upload`.
- Ran it: one device found, at address `0x3C`. Three runs, three hits.
- Added the display code to the firmware — `Adafruit_SSD1306` at 128x32, with the
  address the scanner reported rather than one taken from a datasheet.

**Why the test screen has a border.** It draws a rectangle on the outermost pixel
row and column of all four sides, deliberately. Text on its own still looks
plausible when the pixel buffer is the wrong shape, so a wrong resolution can pass
unnoticed. A frame that does not line up with the physical edges of the glass
gives it away immediately.

### Problems & challenges

**Fault 1 — the panel never rendered**

- **Symptom:** depending on the upload, either faint light at the edges of the
  glass or a single thin lit band close to the pin header. Never the test screen.

- **Ruled out — wrong resolution.** Changed the configured height from 32 to 64.
  This is not a cosmetic setting: it decides which COM pin mapping the library
  sends to the controller, so a wrong value scrambles which buffer row reaches
  which physical row rather than simply cropping the image. No change at all.

- **Ruled out — bus speed.** The library raises the bus to 400 kHz while it
  transfers. The scanner talks to the same panel at the core's default 100 kHz and
  succeeds every time, which made speed the one difference between the case that
  worked and the case that did not. Pinned the bus to 100 kHz. No change.

- **Ruled out — wrong controller chip.** The plan had been to fall back to
  `Adafruit_SH110X` if `Adafruit_SSD1306` came up blank. The PCB silkscreen reads
  `0.91 OLED`, and a 0.91" panel is 128x32 and is almost always an SSD1306;
  SH1106 is typically the 1.3" 128x64 module. The library switch was abandoned on
  that evidence rather than tried blind.

- **Ruled out — insufficient supply.** Multimeter on DC volts: 3.2 V on the 3.3 V
  rail, and 3.2 V measured at the module's own VCC leg rather than only at the
  rail.

- **Ruled out — a loose signal wire or a cold joint on SDA or SCL.** The scanner
  was re-run three times back to back and found the device on all three. An
  intermittent line would have produced at least one miss.

- **A test that was wrong, and why.** A full-screen white fill was used to try to
  take text and layout out of the question and show only which pixels the
  controller lights. On an OLED that is the wrong test. OLED pixels emit their own
  light and there is no backlight, so current draw scales with the number of lit
  pixels: a fully white screen is the maximum-current case, not a baseline. The
  test introduced a new variable while trying to isolate a different one, and its
  result — a completely black screen — pushed the diagnosis sideways for a while.

- **The caveat that follows from it.** The 3.2 V readings were taken while the
  panel was dark, which is to say under almost no load. A high-resistance
  connection drops voltage in proportion to current, so it measures perfectly fine
  when nothing is drawing through it. Those readings therefore do not fully
  exclude a resistive connection. Resistance proves a circuit is complete, voltage
  proves it is correct, and only voltage under load proves it can carry the
  current — the third step, on top of the two recorded in session 8.

- **A test prepared but overtaken.** An `invertDisplay` call was added to send one
  command and no pixel data, to separate the command path from the data path. Its
  result was never observed, because the finger-pressure discovery came first. It
  was removed again without being committed.

- **The actual fault.** Pressing a finger on the far end of the module — the glass
  end, away from the pin header — makes the display work and render legible text.
  Releasing the pressure returns it to the thin band. Photographs of that end show
  the flexible ribbon with its gold contacts exposed and lifted, and the adhesive
  around it crumpled.

- **Diagnosis:** the bond between the controller's flex ribbon and the OLED glass
  has failed. That bond is not solder. It is made in the factory with a conductive
  adhesive containing metal particles, pressed under heat: the particles are
  crushed and conduct only where the pressure was applied, and the cured adhesive
  holds that pressure permanently. **The pressure is the electrical connection.**
  With the adhesive lifted, only some of the dozens of parallel conductors in the
  ribbon still touch. Each conductor feeds a group of rows on the glass, so only
  those rows light — which is exactly the thin band, and pressing by hand restores
  enough contacts to show the screen.

- **Solution:** none available. See "no home repair" below. A replacement module
  has been ordered.

### The finding that did not fit, and why it mattered

The one result that suited no hypothesis was that the I2C scan succeeded three
times out of three while the display failed completely. Every explanation that was
tried had to work around it.

In the end it was the thing that explained everything. VCC, GND, SDA and SCL
happen to sit on ribbon conductors that are still bonded, so the controller
receives everything it needs and acknowledges its address exactly as a healthy
part would. It simply cannot get the result out to the glass. The scanner and the
display were asking different things of the same connector, and only one of those
things was still possible.

**A result that contradicts the hypothesis is not noise to be worked around. It is
often the key.** That sits alongside what session 5 already records — that a fix
which works is not evidence that the explanation for it is correct — and what
session 8 records, that a diagnosis which sounds convincing is not one that has
been tested.

### What is proven working

All of the software. Under finger pressure the test screen rendered with legible
text, so the library choice, the `0x3C` address, the 128x32 resolution, the
100 kHz bus setting and the drawing code are all correct and need no rework. The
rover's own wiring is proven too — SDA on 21, SCL on 22, power and ground. The
fault is internal to the display module.

### Why there is no home repair

Rebonding means aligning dozens of contacts at roughly 0.05 mm pitch while
applying controlled heat and pressure at the same time. A soldering iron is the
wrong tool by an order of magnitude: it would melt the plastic film and bridge
neighbouring contacts before reaching them. A mechanical clamp — tape or similar,
holding the ribbon down — does restore the connection and would work on a
stationary bench.

### Decisions & rationale

- **A replacement module rather than a clamp.** The clamp was considered and
  rejected. A 4WD rover vibrates continuously, and the pressure holding those
  contacts would work loose. The failure would then arrive in phase 7, with every
  subsystem running together, where a screen dropping out is far harder to
  attribute than it is now.
- **The display code stays in the firmware.** It is verified and correct, so
  removing it would throw away proven work and leave nothing to plug the new
  module into.
- **The OLED does not block anything.** Phase 3 is the chassis and motors and
  phase 4 is Wi-Fi control; neither depends on the display. Work continues on
  those while the replacement is in transit.
- **The scanner was worth building as its own environment.** It did more than
  report an address. It became the control measurement — a known-good exchange
  with the same part over the same wires — and the contrast between it and the
  failing display is what eventually located the fault.

### Open

It is not known whether the ribbon was already lifted when the module came out of
its packaging or whether it lifted during handling and soldering. Recorded as
unknown rather than guessed at.

### Photos

| File | What it shows |
|---|---|
| `phase2-oled-under-finger-pressure.jpg` | A finger pressing the far end of the module, at the glass rather than the header. Parts of the test screen light up under that pressure |
| `phase2-oled-ribbon-lifted.jpg` | The same module with no pressure applied and the screen dark. The flexible ribbon at the far end is visible with its gold contacts exposed and the adhesive around them crumpled — the fault itself |
| `phase2-oled-debug-bench.jpg` | The bench during the session: the breadboard with all of phase 2 wired, the multimeter with its probes out, and the board still connected |

### Known, not investigated — an ADC warning at every boot

Two errors print from the core on every start:
`__analogChannelConfig(): Pin is not configured as analog channel`. They arrive at
17 ms and 26 ms, before the firmware prints its own boot line, so they come from
the core's setup path rather than from anything the loop does. Two errors, two
line sensor pins.

No effect on the result: the sensors were checked over white and over black at the
working height and returned the values recorded in session 7. So the warning is
harmless as far as anything measured so far shows — but why it prints has not been
looked into, and those are two different statements. An error that reports
something is not the same as no error at all, and the distinction is easy to lose
once a message has been seen enough times to stop registering.

### Next up
Phase 3 — chassis assembly, motor wiring and the first driving test. The display
returns when the replacement module arrives.

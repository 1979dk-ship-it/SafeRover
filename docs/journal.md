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

**Fault 2 — the tactile button's leg spacing is not symmetric**

- **Symptom:** the button read as permanently pressed from boot, and pressing it
  changed nothing. Swapping in a different button did not change the behaviour.
- **Diagnosis:** an isolation chain ruled out each layer in turn — the code, the
  pin, and GND. Jumping GPIO 23 to GND by hand produced correct PRESSED and
  RELEASED events, which cleared everything below the switch. Pulling only the
  button, without touching any wire, made the symptom disappear and pinned the
  fault on the component itself. The cause: the switch's leg spacing differs
  between its two axes, so the internally connected pair runs along the length of
  the board rather than across it. The two wires, placed on the same horizontal
  line on either side of the centre channel, were therefore both landing on the
  same pair — a permanent connection that no press could change.
- **Solution:** moved the GND wire to an adjacent row on the same side of the
  channel, so each wire faces a different pair. The button started working.

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
- **Assuming a tactile button is symmetric is wrong, and it cost time.** The
  lesson recorded for the rest of the project: check a component's leg spacing
  physically before assuming how it sits across the board.
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

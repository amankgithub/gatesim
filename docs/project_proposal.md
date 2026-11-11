# GateSim: Project Proposal

Course: Programming Fundamentals and Software Engineering (KPIT APEX Lab), individual mini project.
This document holds sections 1 to 5 of the mini project template. `docs/detailed_design.md` continues with section 5
in detail (5. Architecture (detailed)) and sections 6 to 24.

---

## 1. Project Information

### 1.1 Project Title

**GateSim: a gate-level timing simulator that keeps running where student simulators stop**

### 1.2 Engineering Domain

Education (digital electronics laboratory).

### 1.3 Team Details

| Field | Details |
|---|---|
| Student name | Aman Khandelwal |
| Roll number | 612509003 |
| Branch | Instrumentation and Control Engineering |
| Division / batch | none |
| Project Title | GateSim: a gate-level timing simulator that keeps running where student simulators stop |
| Repository name | gatesim |
| GitHub repository link | https://github.com/amankgithub/gatesim |

### 1.4 Individual Project Declaration

I confirm that this project is my individual work. I understand the design, source code, unit tests, Makefile and
documentation included in this repository, and I am prepared to explain or modify any part during evaluation.

Student signature or acknowledgement: Aman Khandelwal (612509003)

Note: individual project (no team partner).

---

## 2. Engineering Scenario

### 2.1 Scenario Description

**Real situation.** In a digital electronics course, students design small circuits by hand: a counter that follows a
given sequence (for example 0, 3, 5, 7), an adder, a multiplexer, a latch or a flip-flop. They draw K-maps and derive
the gate equations. Then they need to see how the circuit really behaves in time: does the counter follow its
sequence, where do the unused states go, does a latch settle or keep switching, does a multiplexer output glitch when
its select input changes?

**Where and who.** In the college digital-electronics lab and at home while preparing lab journals and exams. It is
faced by second-year engineering students (electronics, instrumentation, computer), checked by lab instructors, and
asked about in vivas and exams by examiners.

**Current working method.** The design is solved on paper (state table, K-maps, equations). It is then checked either
on a breadboard with ICs, or in a free student simulator such as Logisim, Digital or CircuitVerse, where the circuit is
drawn with the mouse and the inputs are clicked by hand.

**Problems in the current method.** The free student simulators are good for logic, but they are built to find a stable
state, not to show timing. When a circuit does not settle, they stop:

| Free student tool | What it does with an oscillating or racing circuit | Source |
|---|---|---|
| Logisim | Gives up after many iterations and reports "oscillation"; its documentation calls the gate delays somewhat arbitrary. | cburch.com/logisim/docs/2.3.0/guide/prop/oscillate.html and .../prop/delays.html |
| Digital (hneemann) | Detects the oscillation and stops the simulation; its documentation notes that a simple RS flip-flop may not settle and that a master-slave JK flip-flop needs a reset input to work. | github.com/hneemann/Digital (documentation) |
| CircuitVerse | Cannot simulate circuits that do not reach a stable state between clock ticks; a JK flip-flop with J = K = 1 ends in a "stack limit exceeded" error; hold time is fixed at zero. | docs.circuitverse.org (delay vs clock, error messages) |

So race-around in a level-triggered JK flip-flop, the effect every textbook uses to motivate the master-slave
flip-flop, is exactly where these tools stop, and so is an SR latch released from S = R = 1 or a ring oscillator.
Checking a counter means clicking the clock many times and reading lamps, once per start state; checking an adder
means trying input pairs by hand. Industrial simulators (Icarus Verilog, ModelSim) do get the timing right, but they
need the circuit written in Verilog.

**Why software helps.** A small, focused command-line tool can take the circuit as a short text file of gates with
delays, simulate it event by event with real delays and keep running through oscillations for as long as the student
asks, and write the result as a standard waveform file. The student then looks at the waveform and judges it: the
circuit settled, it oscillates, or it runs through a sequence of values. GateSim is meant as such a lightweight,
focused tool for the lab, not as a full EDA package.

### 2.2 Problem Statement

In digital-electronics education, students currently face the problem that their free circuit simulators stop at
oscillations and races, so the timing behaviour of a hand-designed circuit (gate delays, race-around, a latch that
never decides, glitches, where every start state of a counter goes) cannot be watched in those tools. The current
method results in long manual clicking, paper work and circuits that are "checked" without their timing ever being
seen. The proposed software will provide an event-driven gate-level simulation with per-gate delays that runs until a
time the student chooses, whatever the circuit does, and writes the waveform of every net, so that users can see how
a hand-solved circuit behaves in time and judge from the picture whether it settles, oscillates or cycles through
values.

### 2.3 Proposed Solution

**What it does.** GateSim reads two text files: a netlist (`.net`: inputs, outputs and gates, each gate with a type, its
inputs and a propagation delay; reusable blocks such as flip-flops from a library file) and a stimulus (`.stim`: input
changes at given times, clocks, and the end time of the run). It simulates the circuit event by event with the given
delays and the values 0, 1 and X (unknown), until nothing is left to happen or the end time is reached, and writes
the waveform of every net as a VCD file.

**How it helps.** The student writes the circuit once as a short text file and chooses how long it runs (`end T`). An
oscillating circuit is not refused or stopped: it simply keeps switching in the waveform until the end time. The VCD
file opens in GTKWave, or `tools/plot.py` draws it as a picture (with `--number`, a group of nets is read as one binary
number, so a counter's states can be read directly). The student judges the picture. For example, the counter
`data/counter_0357.net` is run once per start state (`data/counter_s0.stim` .. `data/counter_s7.stim`): eight
commands give eight pictures that show where every state goes (0 -> 3 -> 5 -> 7 -> 0; the unused states 1 -> 3,
2 -> 5, 4 -> 7, 6 -> 1). Every input combination of an adder is produced by giving each input bit its own clock,
each with twice the period of the bit below it (`data/adder4.stim`).

**Major operations.**
- Read and validate the netlist; expand library blocks (`subckt`, `inst`, `include`); check the whole circuit.
- Read and validate the stimulus; turn it into the first events of the run (set lines, one event per clock).
- Simulate: take the earliest events, apply them, re-calculate the gates that read a changed net, and keep, cancel or
  schedule each gate's next change (inertial delay); expand each clock edge into the next one.
- Write every net change into the VCD file while the run goes on.
- Validate a netlist alone and print its summary (`--check-only`).
- Draw a VCD file as a picture (`tools/plot.py`, outside the C program).

**Outputs.**
- `output/waves.vcd` (or `<dir>/waves.vcd` with `--out <dir>`): the waveform of every net, viewable in GTKWave.
- A console summary: `run: stopped at <T> ns (<reason>), <N> events` and `waveform: <path>` (or why it was not
  written).
- For `--check-only`: a summary of the netlist (gates per type, nets, inputs, outputs, blocks, warnings).
- An exit code: 0 run, check or help done; 2 wrong arguments, or an input file could not be read or has an error;
  3 the run could not be completed (event list full, time overflow).
- With `tools/plot.py`: a PNG picture of the chosen nets.

**Information it manages.**
- From the `.net` file: nets (name, driver, start value), gates (type, inputs, output, rise and fall delay), blocks
  (ports and their gate lines) and block instances.
- From the `.stim` file: input changes (net, value, time), clocks (net, period, high time, start), the end time.
- During the run: the list of pending events, the current value of every net, the promised (pending) output change of
  every gate, the nets that changed in the current time step.

**Calculations and decisions.**
- The 0/1/X value of each gate from lookup tables (a 0 decides an AND even when another input is X).
- The delay of each output change: the rise delay for a change to 1, the fall delay for a change to 0, the shorter
  one for a change to X.
- The inertial-delay rule for each re-calculated gate: keep the same promise, cancel a different promise, schedule a
  new change after the gate delay. A pulse shorter than the gate delay is swallowed.
- The next edge of each clock (rise, fall, rise ...) from its period, high time and start.
- Why the run stopped: nothing left to happen (stable) or the end time reached.
- Whether a netlist is valid: one driver per net, every read net driven, every block port connected exactly once.

### 2.4 Target Users

| User Type | Responsibility | How the User Will Use the Application |
|---|---|---|
| Student | Designs circuits by hand (counters, adders, latches, flip-flops) and must show how they behave | Writes the circuit as a `.net` file (using the shipped flip-flop and adder library) and a `.stim` file with the input changes, clocks and end time, runs `gatesim`, opens `waves.vcd` in GTKWave or draws it with `tools/plot.py`, and judges the waveform |
| Lab instructor | Explains timing effects and checks lab work | Uses the sample circuits (race-around, master-slave, SR latch, ring oscillator, mux hazard, ripple counter, counter start states) as demonstrations; runs a student's files to see the design in time |
| Examiner | Asks about timing and design properties in vivas and exams | Uses the waveforms (oscillation period, delays, where each counter state goes) as reference answers and to show what happens when a design is wrong |

---

## 3. Project Scope

### 3.1 In-Scope Features

1. Gates AND, OR, NAND, NOR, XOR, XNOR (1 to 4 inputs), NOT and BUF (1 input); constant nets VCC (always 1) and GND
   (always 0).
2. A delay per gate, `#d` or `#rise,fall`, each from 1 to 1000 ns.
3. Event-driven simulation with inertial delay (the three rules: keep the same promise, cancel a different promise,
   schedule a new change) and the values 0, 1 and X. Every net has exactly one driver.
4. Runs that keep going through oscillations and races until the end time; a run stops earlier only when nothing is
   left to happen.
5. One-level blocks: `subckt ... ends` holding gate lines, `inst` to use a block, `include` for a library file. A
   shipped library: master-slave JK, level-triggered JK and D latch (each with PRE and CLR), SR NOR latch, half and
   full adder.
6. Stimulus: `set` (an input value at a time), `clock` (period, high time, start), `end` (required).
7. Output: `waves.vcd` with every net, a console summary, exit codes; `--check-only` to validate a netlist; `--help`.
8. Error messages that name the file, the line and the offending word.
9. `tools/plot.py`: draws a VCD file as a picture; `--number` reads a group of nets as one binary number.
10. Validation of the simulator itself against Icarus Verilog (`tools/net2v.py`, `tools/compare_vcd.py`,
    `tools/crosscheck.sh`, `make crosscheck`).

### 3.2 Out-of-Scope Features

- A GUI or drag-and-drop circuit editor.
- Automatic judging of the result (expected values, detecting oscillations or glitches, naming the effect): GateSim
  shows what happens, the student decides whether it is right. A short script that reads the VCD file can automate a
  check when one is wanted (the README shows one for the 4-bit adder).
- Analog or electrical effects, real metastability, wire delay (a BUF gate can model it), load-dependent delay.
- Verilog or other HDL input, behavioural code.
- Logic minimisation or automatic design ("solve it for me"): the student solves the design, GateSim simulates it.
- Circuits larger than the fixed limits (512 nets, 512 gates, 1024 pending events).

### 3.3 Assumptions

- The user writes the netlist by hand from a gate diagram, or uses the shipped library blocks.
- Delays are whole nanoseconds and every gate delay is at least 1 ns (so no zero-delay loop can hang a time step).
- Every net has exactly one driver: an input or one gate output (VCC and GND are constants).
- Inputs that the stimulus has not set yet are X (unknown), as in a real circuit before reset; every net starts as X.
- The user chooses an end time long enough to see what they want to see.
- The output folder exists before a run (`make run` creates `output/`).
- The program runs on Linux (Ubuntu 24.04) with gcc, g++, GNU make and GoogleTest installed; Icarus Verilog, GTKWave,
  python3 with matplotlib and cppcheck are optional (cross-check, waveform viewing, pictures, static analysis).

### 3.4 Constraints

- Command-line program only; input and output are plain text files.
- C99 for the application, C++17 only for the GoogleTest unit tests; zero compiler warnings with `-Werror`.
- MISRA C:2012 is followed. The deviations are recorded in `docs/misra_deviations.md` with rule, location, reason, risk
  and mitigation: Rule 21.6 (standard input/output in `main.c`, `text_utils` and `report`) and advisory Rule 11.5 (the
  void-pointer casts in the kernel's callbacks).
- Fixed limits in one header (`include/config.h`): 512 nets, 512 gates, 4 inputs per gate, 1024 pending events, names
  of up to 31 characters, lines of up to 254 characters with up to 16 words, 8 blocks of up to 16 gates and 8 ports,
  one level of `include`, 256 set lines, 8 clocks.
- No dynamic memory (no `malloc`), no recursion; every table is a fixed-size array.
- Integer time in nanoseconds, at most 1 000 000 000 ns per run.
- Individual project, due 12 Nov 2026, delivered as a public GitHub repository.

---

## 4. Requirements

### 4.1 Functional Requirements

| ID | Functional Requirement | Priority | Verification Method |
|---|---|---|---|
| FR-01 | The system shall read a netlist file of `input`, `output` and `gate` lines, where each gate has a name, a type (AND, OR, NAND, NOR, XOR, XNOR with 1 to 4 inputs; NOT and BUF with 1), one output and a delay `#d` or `#rise,fall` of 1 to 1000 ns; a `#` not followed by a digit starts a comment. | High | Test, Demo (`make run`) |
| FR-02 | The system shall reject an invalid circuit (unknown gate type, wrong number of inputs, duplicate name, a net with two drivers, a net that is read but never driven, a full table) and shall warn about gate outputs that are never read. | High | Test |
| FR-03 | The system shall let the user define a block of gates once (`subckt ... ends`), use it several times (`inst`) and include a library file (`include`), and shall ship a library with flip-flops and latches (most with PRE and CLR) and adders. | Medium | Test, Demo |
| FR-04 | The system shall read a stimulus file whose `set` lines change inputs (to 0, 1 or x) at given times and whose required `end` line gives the end time of the run. | High | Test |
| FR-05 | The system shall generate a clock on an input from a `clock` line with a period, a high time and a start time, for up to 8 inputs, so that every input combination can be produced by clocks with doubling periods. | High | Test, Demo |
| FR-06 | The system shall simulate the circuit event by event with the values 0, 1 and X, the rise and fall delay of each gate, and the inertial-delay rules (keep the same pending value, cancel a different one, schedule a new change after the gate delay). | High | Test, Demo (`make crosscheck`) |
| FR-07 | The system shall keep simulating through oscillations and races until the end time of the stimulus, stop earlier only when nothing is left to happen, and print the stop time, the reason and the number of events applied. | High | Test, Demo |
| FR-08 | The system shall write the waveform of every net into a VCD file (`<dir>/waves.vcd`, default `output/`) while the run goes on, covering the whole run; a waveform that cannot be written shall not stop the run. | High | Test, Demo |
| FR-09 | The system shall validate a netlist alone (`--check-only`) and print a summary: gates per type, nets, inputs, outputs, blocks, instances and warnings; `--help` shall print the usage text. | Medium | Test, Demo |
| FR-10 | The system shall report every input error as `gatesim: file:line: message 'word'` and end with an exit code: 0 done, 2 wrong arguments or input error, 3 the run could not be completed. | High | Test, Demo |
| FR-11 | The project shall provide a tool that draws a VCD file as a picture, one row per chosen net, with an optional row that reads a group of nets as one binary number (`tools/plot.py`). | Medium | Demo |

### 4.2 Non-Functional Requirements

| ID | Non-Functional Requirement | Verification Method |
|---|---|---|
| NFR-01 | The program shall compile without warnings with `-Wall -Wextra -Wpedantic -Werror` and the extra warning flags of the Makefile. | Build output of `make all` |
| NFR-02 | The code shall be modular: `main.c` holds only the high-level flow, every module has its own `.c` and `.h` file. | Code review |
| NFR-03 | Invalid input (arguments, file contents, missing files) shall be handled without a crash, with a clear message and an exit code. | Negative unit tests, application tests (design 19.3) |
| NFR-04 | The important functions shall be tested with GoogleTest (positive, negative and boundary cases). | `make test` output |
| NFR-05 | The project shall build, run and test with the Makefile only (`make all`, `make run`, `make test`, `make clean`). | Build demonstration |
| NFR-06 | The project shall be maintained on GitHub with a gradual commit history. | Repository review |
| NFR-07 | The C code shall follow MISRA C:2012; every deviation shall be recorded in `docs/misra_deviations.md` with rule, location, reason, risk and mitigation. | `make check` (cppcheck MISRA addon) |
| NFR-08 | Every demo circuit shall give the same waveform as Icarus Verilog. | `make crosscheck` |
| NFR-09 | The code shall be readable and follow one coding standard (naming, 4-space indentation, brace style, comments), as set in design section 12. | Code review |
| NFR-10 | The project shall be documented (README, proposal, detailed design, architecture diagram) so that another developer can build, run and test it from a fresh clone without help. | Fresh-clone check, repository review |

### 4.3 Requirement Traceability

One row per requirement: the module, the function or feature that meets it and the unit tests (UT-xx, listed in 4.4
and in `docs/detailed_design.md` 14.2) or the check that verifies it.

| Requirement ID | Module | Function or Feature | Test ID |
|---|---|---|---|
| FR-01 | text_utils, logic, netlist | Comment rule, words, numbers, names, line reader (`tu_cut_comment`, `tu_split_words`, `tu_parse_u32`, `tu_check_name`, `tu_open`, `tu_read_line`, `tu_close`); gate types (`lg_type_from_name`); `nl_find_or_add_net`, `nl_parse_delay`, `nl_parse_line`, `nl_load` | UT-04..UT-17, UT-30, UT-33, UT-34, UT-36..UT-39, UT-42, UT-44 |
| FR-02 | logic, netlist | `lg_check_input_count` (through `lg_eval`); `nl_add_input`, `nl_add_gate` (duplicate, two drivers, full table); the whole-circuit check in `nl_load` (undriven nets, unused outputs) | UT-28, UT-35, UT-40, UT-41, UT-46, UT-47 |
| FR-03 | text_utils, netlist, `lib/` | `tu_split_assign`, `tu_join_path`; `subckt`, `ends` and `inst` lines in `nl_parse_line`, `nl_find_block`; `include` in `nl_load`; the flip-flop and adder library | UT-21, UT-22, UT-48..UT-54, UT-70 |
| FR-04 | text_utils, stimulus | `set` and `end` lines: `st_parse_line`, `st_load`, `st_seed`; times through `tu_parse_u32` | UT-10..UT-12, UT-71..UT-73 |
| FR-05 | stimulus, sim_kernel | `clock` lines: `st_parse_line`, `st_find_clock`, `st_seed`, `st_expand` (the edge chain), `sk_set_source` | UT-68, UT-69, UT-74, UT-75 |
| FR-06 | logic, event_queue, sim_kernel | `lg_eval`; `eq_init`, `eq_add`, `eq_find_earliest`, `eq_remove`, `eq_find_gate_event`; `sk_run` (inertial delay), `sk_gate_delay` | UT-23..UT-29, UT-31, UT-32, UT-55..UT-70; `make crosscheck` |
| FR-07 | sim_kernel, main.c | `sk_run` stops with `STOP_END_TIME` or `STOP_QUEUE_EMPTY` and fills `SimResult_t`; the `run:` line of `run_simulation` | UT-64..UT-66, UT-69; `make run` |
| FR-08 | report, text_utils, main.c | `rw_vcd_code`, `rw_vcd_open`, `rw_vcd_listener`, `rw_vcd_close`; `tu_join_dir`; `start_waveform` (a failed waveform does not stop the run) | UT-20, UT-76, UT-77; `make run` |
| FR-09 | netlist, main.c | `nl_load` (the counts and warnings of the netlist); `print_check_only`, `print_usage` | UT-44; design 19.3 |
| FR-10 | status, text_utils, netlist, stimulus, main.c | `status_text`, `tu_set_diag` (`file:line: text 'word'`); the messages of `nl_load` and `st_load`; `print_diag`, `ExitCode_t` | UT-01..UT-03, UT-17..UT-19, UT-43, UT-45; design 19.3 |
| FR-11 | tools | `tools/plot.py` (reads the VCD with the reader of `tools/compare_vcd.py`) | demonstration (a Python tool outside the C program, not unit-tested) |
| NFR-01 | all | zero warnings with `-Werror` | build output of `make all` (design 19.1) |
| NFR-02 | all | one .c/.h per module, main.c flow only | code review (design 6.1) |
| NFR-03 | all readers, main.c | `Status_t` + `Diag_t`, exit codes 2 and 3 | negative UTs (Negative rows of 4.4), design 19.3 |
| NFR-04 | all | GoogleTest suite, positive, negative and boundary cases | `make test`: 77 tests pass (design 19.2) |
| NFR-05 | Makefile | all, run, test, clean | design 15.1, 19.1 |
| NFR-06 | repository | gradual commits | `git log --oneline` (design 17.2) |
| NFR-07 | all | MISRA C:2012, deviations in `docs/misra_deviations.md` | `make check`: exit 0 |
| NFR-08 | tools | every demo circuit against Icarus Verilog | `make crosscheck`: 21 pairs MATCH |
| NFR-09 | all | one coding standard (naming, indentation, braces, comments) | code review (design 12) |
| NFR-10 | README.md, docs/ | README, proposal, detailed design, architecture diagram | fresh-clone check |

### 4.4 Unit Test List

77 GoogleTest tests, one file per module (`tests/test_<module>.cpp`). Each test has a comment
`/* UT-xx FR-yy Category */` on the line above it. Categories: Positive (valid input gives the right result),
Negative (invalid input gives the right status and leaves the outputs unchanged), Boundary (a limit or an edge case).

| Test ID | Test (suite.name) | Module | Function | Category | Requirement |
|---|---|---|---|---|---|
| UT-01 | StatusText.OkHasText | status | `status_text` | Positive | FR-10 |
| UT-02 | StatusText.EveryCodeHasDistinctText | status | `status_text` | Positive | FR-10 |
| UT-03 | StatusText.OutOfRangeGivesUnknown | status | `status_text` | Boundary | FR-10 |
| UT-04 | CutComment.RemovesTrailingComment | text_utils | `tu_cut_comment` | Positive | FR-01 |
| UT-05 | CutComment.KeepsDelayWord | text_utils | `tu_cut_comment` | Positive | FR-01 |
| UT-06 | CutComment.WholeLineComment | text_utils | `tu_cut_comment` | Positive | FR-01 |
| UT-07 | SplitWords.SplitsOnSpacesAndTabs | text_utils | `tu_split_words` | Positive | FR-01 |
| UT-08 | SplitWords.EmptyLineHasNoWords | text_utils | `tu_split_words` | Boundary | FR-01 |
| UT-09 | SplitWords.RejectsTooManyWords | text_utils | `tu_split_words` | Boundary | FR-01 |
| UT-10 | ParseU32.AcceptsRangeAndBoundaries | text_utils | `tu_parse_u32` | Boundary | FR-01, FR-04 |
| UT-11 | ParseU32.RejectsNonDigits | text_utils | `tu_parse_u32` | Negative | FR-01, FR-04 |
| UT-12 | ParseU32.RejectsOutOfRange | text_utils | `tu_parse_u32` | Boundary | FR-01, FR-04 |
| UT-13 | CheckName.AcceptsLettersDigitsUnderscoreDot | text_utils | `tu_check_name` | Positive | FR-01 |
| UT-14 | CheckName.RejectsBadNames | text_utils | `tu_check_name` | Negative | FR-01 |
| UT-15 | ReadLine.ReadsLinesAndCountsThem | text_utils | `tu_open`, `tu_read_line`, `tu_close` | Positive | FR-01 |
| UT-16 | ReadLine.StripsCarriageReturn | text_utils | `tu_open`, `tu_read_line`, `tu_close` | Positive | FR-01 |
| UT-17 | ReadLine.RejectsTooLongLine | text_utils | `tu_open`, `tu_read_line`, `tu_close` | Boundary | FR-01, FR-10 |
| UT-18 | OpenFile.MissingFileFails | text_utils | `tu_open` | Negative | FR-10 |
| UT-19 | SetDiag.FormatsWhatAndName | text_utils | `tu_set_diag` | Positive | FR-10 |
| UT-20 | JoinDir.AddsSlash | text_utils | `tu_join_dir` | Positive + Negative | FR-08 |
| UT-21 | SplitAssign.SplitsPortAndNet | text_utils | `tu_split_assign` | Positive + Negative | FR-03 |
| UT-22 | JoinPath.UsesDirectoryOfBaseFile | text_utils | `tu_join_path` | Positive | FR-03 |
| UT-23 | LogicAnd.MatchesTruthTable | logic | `lg_eval` | Positive | FR-06 |
| UT-24 | LogicOr.MatchesTruthTable | logic | `lg_eval` | Positive | FR-06 |
| UT-25 | LogicNand.ZeroDecidesEvenWithX | logic | `lg_eval` | Positive | FR-06 |
| UT-26 | LogicNand.XWithoutZeroGivesX | logic | `lg_eval` | Positive | FR-06 |
| UT-27 | LogicNotBuf.NotInvertsBufCopies | logic | `lg_eval` | Positive | FR-06 |
| UT-28 | LogicEval.RejectsWrongInputCount | logic | `lg_eval` (`lg_check_input_count`) | Negative | FR-02, FR-06 |
| UT-29 | LogicEval.RejectsInvalidValue | logic | `lg_eval` | Negative | FR-06 |
| UT-30 | LogicType.ParsesNamesAndRejectsUnknown | logic | `lg_type_from_name`, `lg_type_name` | Positive + Negative | FR-01 |
| UT-31 | LogicXor.MatchesTruthTable | logic | `lg_eval` | Positive | FR-06 |
| UT-32 | LogicXnor.IsNotOfXor | logic | `lg_eval` | Positive | FR-06 |
| UT-33 | FindOrAdd.ReturnsExistingIndex | netlist | `nl_find_or_add_net` | Positive | FR-01 |
| UT-34 | FindOrAdd.AddsNewNetAtEnd | netlist | `nl_find_or_add_net` | Positive | FR-01 |
| UT-35 | FindOrAdd.RejectsWhenTableFull | netlist | `nl_find_or_add_net`, `nl_add_gate` | Boundary | FR-02 |
| UT-36 | FindOrAdd.VccAndGndAreConstants | netlist | `nl_find_or_add_net` | Positive | FR-01 |
| UT-37 | ParseDelay.SingleAndRiseFall | netlist | `nl_parse_delay` | Boundary | FR-01 |
| UT-38 | ParseDelay.RejectsBadDelays | netlist | `nl_parse_delay` | Negative | FR-01 |
| UT-39 | GateLine.ValidLineAddsGate | netlist | `nl_parse_line` (`nl_parse_gate_words`, `nl_add_gate`) | Positive | FR-01 |
| UT-40 | GateLine.RejectsUnknownTypeAndBadCounts | netlist | `nl_parse_line` (`nl_parse_gate_words`) | Negative | FR-02 |
| UT-41 | GateLine.RejectsDuplicateNameAndSecondDriver | netlist | `nl_parse_line` (`nl_add_gate`, `nl_add_input`) | Negative | FR-02 |
| UT-42 | ParseLine.SrLatchTablesAsExpected | netlist | `nl_parse_line` | Positive | FR-01 |
| UT-43 | ParseLine.RejectsUnknownKeyword | netlist | `nl_parse_line` | Negative | FR-10 |
| UT-44 | LoadNetlist.Ring5FromFile | netlist | `nl_load` | Positive | FR-01, FR-09 |
| UT-45 | LoadNetlist.ErrorHasFileAndLine | netlist | `nl_load` | Negative | FR-10 |
| UT-46 | CheckNetlist.RejectsUndrivenNet | netlist | `nl_load` | Negative | FR-02 |
| UT-47 | CheckNetlist.WarnsUnusedGateOutput | netlist | `nl_load` | Positive | FR-02 |
| UT-48 | Block.StoresPortsAndGates | netlist | `nl_parse_line` (subckt, ends), `nl_find_block` | Positive | FR-03 |
| UT-49 | Inst.ExpandsNamesWithPrefix | netlist | `nl_parse_line` (inst), `nl_find_net` | Positive | FR-03 |
| UT-50 | Inst.RejectsBadPorts | netlist | `nl_parse_line` (inst) | Negative | FR-03 |
| UT-51 | Subckt.RejectsNestingAndUnclosedBlock | netlist | `nl_parse_line`, `nl_load` | Negative | FR-03 |
| UT-52 | Include.LoadsLibraryRelativeToFile | netlist | `nl_load` | Positive | FR-03 |
| UT-53 | Include.RejectsNestedInclude | netlist | `nl_load` | Negative | FR-03 |
| UT-54 | Library.CounterAndAdderSizes | netlist | `nl_load` | Positive | FR-03 |
| UT-55 | EventQueue.EarliestByTimeExample | event_queue | `eq_add`, `eq_find_earliest` | Positive | FR-06 |
| UT-56 | EventQueue.TieBrokenBySeq | event_queue | `eq_add`, `eq_find_earliest`, `eq_remove` | Boundary | FR-06 |
| UT-57 | EventQueue.RemoveMovesLastIntoHole | event_queue | `eq_add`, `eq_remove` | Positive | FR-06 |
| UT-58 | EventQueue.FullListRejectsAdd | event_queue | `eq_add` | Boundary | FR-06 |
| UT-59 | EventQueue.EmptyListHasNoEarliest | event_queue | `eq_find_earliest` | Negative | FR-06 |
| UT-60 | EventQueue.FindsGateEvent | event_queue | `eq_add`, `eq_find_gate_event` | Positive + Negative | FR-06 |
| UT-61 | Kernel.InertialExampleMatchesTrace | sim_kernel | `sk_run` | Positive | FR-06 |
| UT-62 | Kernel.PulsesNarrowerThanDelaySwallowed | sim_kernel | `sk_run` | Boundary | FR-06 |
| UT-63 | Kernel.RiseFallDelaysShapePulse | sim_kernel | `sk_run`, `sk_gate_delay` | Positive | FR-06 |
| UT-64 | Kernel.SrLatchMatchesTrace | sim_kernel | `sk_run` | Positive | FR-06, FR-07 |
| UT-65 | Kernel.SkewedReleaseSettlesInReset | sim_kernel | `sk_run` | Positive | FR-06, FR-07 |
| UT-66 | Kernel.RingPeriodIsTwenty | sim_kernel | `sk_run` | Positive | FR-06, FR-07 |
| UT-67 | Kernel.RaceAroundPeriodEight | sim_kernel | `sk_run` | Positive | FR-06 |
| UT-68 | Kernel.ClockLineDrivesRaceAround | sim_kernel | `sk_run`, `sk_set_source` (`st_expand`) | Positive | FR-05, FR-06 |
| UT-69 | Kernel.NarrowPulseLeavesLatchUndecided | sim_kernel | `sk_run`, `sk_set_source` (`st_expand`) | Boundary | FR-05, FR-06, FR-07 |
| UT-70 | Kernel.MasterSlaveTogglesOncePerClock | sim_kernel | `sk_run` | Positive | FR-03, FR-06 |
| UT-71 | StimSet.ParsesAndRejects | stimulus | `st_parse_line` (set) | Positive + Negative | FR-04 |
| UT-72 | StimEnd.RequiredExactlyOnce | stimulus | `st_load`, `st_parse_line` (end) | Negative | FR-04 |
| UT-73 | StimSeed.SkipsEventsAfterEnd | stimulus | `st_seed` | Boundary | FR-04 |
| UT-74 | StimClock.ParsesAndRejects | stimulus | `st_parse_line` (clock), `st_find_clock` | Negative | FR-05 |
| UT-75 | StimClock.ExpandGivesEdgeChain | stimulus | `st_seed`, `st_expand` | Boundary | FR-05 |
| UT-76 | VcdCode.OneAndTwoCharacterCodes | report | `rw_vcd_code` | Boundary | FR-08 |
| UT-77 | Vcd.RingFileHeaderAndChanges | report | `rw_vcd_open`, `rw_vcd_listener`, `rw_vcd_close` | Positive | FR-08 |

The kernel tests (UT-61..UT-70) run the netlist from a file, add the events, prepare the run with `sk_init`,
`sk_set_source` and `sk_set_listener`, and compare every recorded net change with the expected list or a whole trace
(`tests/data/*.trace`).

---

## 5. High-Level Architecture

### 5.1 Architecture Description

GateSim is a command-line program built in layers. `main.c` reads the command line and calls the modules in order.
The input layer reads the two text files into fixed tables. The core simulates the circuit. The VCD writer is called
back by the core while it runs and writes every net change into `waves.vcd`. After the program, the student looks at
the waveform in GTKWave or draws it with `tools/plot.py`. Each module uses only the modules listed before it in the
table, so there are no circular dependencies.

| Module | Responsibility |
|---|---|
| `config.h` | Named limits and file names shared by every module |
| `status` | `Status_t` codes and their readable texts; the diagnostic message (file, line, text) |
| `text_utils` | Read a line (fgets), cut the comment, split into words, checked number parsing, name rules, paths |
| `logic` | 0/1/X values, gate types, lookup tables, calculate one gate |
| `netlist` | Nets, gates and blocks tables; find-or-add by name; input, output, gate, subckt, ends, inst and include lines; whole-circuit checks |
| `event_queue` | Events array: add, find the earliest (min-finder), remove, find a gate's event |
| `sim_kernel` | Main event loop, the three inertial-delay rules, the listener callbacks and the expansion hook |
| `stimulus` | set, clock and end lines; the first events of a run; the next clock edge (expansion hook) |
| `report` | The VCD waveform writer (`waves.vcd`), the kernel's listener |
| `main.c` | Command line and the high-level flow only: run, check-only and help modes, console summary, exit code |
| `tools/` (Python) | `plot.py` draws a VCD file as a picture; `net2v.py`, `compare_vcd.py` and `crosscheck.sh` compare GateSim with Icarus Verilog |

### 5.2 Architecture Diagram

```
                       +-------------------------------+
   user (student) ---> |  command line: main.c         | ---> exit code 0 / 2 / 3, console summary
                       +-------------------------------+
                          |                        |
         data/*.net       |                        |  data/*.stim
         lib/*.net        v                        v
                 +-------------------------------------------+
   input layer   |  text_utils  ->  netlist      stimulus     |   (fixed tables, config.h limits)
                 +-------------------------------------------+
                          |                        |  expansion hook: st_expand (clock edges)
                          v                        v
                 +-------------------------------------------+
   core          |  logic      event_queue      sim_kernel   |
                 +-------------------------------------------+
                          |  listener callbacks: on_change, on_end
                          v
                 +-------------------------------------------+
   listener      |  report (VCD writer)                      |
                 +-------------------------------------------+
                          |
                          v
                 +-------------------------------------------+
   output        |  output/waves.vcd                         |
                 +-------------------------------------------+
                          |
                          v
                 +-------------------------------------------+
   viewing       |  GTKWave, or tools/plot.py -> picture     |   the student judges the waveform
                 +-------------------------------------------+
   status: one Status_t and readable texts, used by every module
```

The same architecture as a drawing: [docs/architecture.svg](architecture.svg).

### 5.3 High-Level Workflow

1. Start: read the command line (`gatesim <netlist.net> <stimulus.stim> [--out <dir>]`, `gatesim --check-only
   <netlist.net>` or `gatesim --help`). `--help` prints the usage text and exits 0. Wrong arguments: usage text on
   stderr, exit 2.
2. Load the netlist: read every line, expand library blocks, then check the whole circuit. Any error: print
   `gatesim: file:line: message 'word'`, exit 2.
3. Check-only mode: print the circuit summary (gates by type, nets, blocks, warnings) and exit 0.
4. Load the stimulus: read every line and check it against the netlist. Any error: message, exit 2.
5. Prepare the run: empty the event list, add the first stimulus events (every set line up to the end time, one event
   per clock), set every net to its start value (X, VCC 1, GND 0), give the kernel the end time and the stimulus as
   the source of clock edges, open `<dir>/waves.vcd` and make the VCD writer the kernel's listener. If the waveform
   cannot be opened, the run goes on without it.
6. Simulate: repeat for each time step: take the events of the earliest time, apply them (a clock event also adds
   its next edge), re-calculate the gates that read a changed net, keep, cancel or schedule their next change, tell
   the listener about every net that changed. Stop when nothing is left to happen or the next event is after the end
   time; an oscillation simply keeps adding events until the end time.
7. Close `waves.vcd`; print `run: stopped at <T> ns (<reason>), <N> events` and `waveform: <path>` (or
   `waveform: not written (<reason>)`).
8. Exit: 0 if the run is done, 3 if the run could not be completed (event list full, time overflow).
9. After the program: the student opens `waves.vcd` in GTKWave or draws it with `tools/plot.py`, and judges whether
   the circuit settled, oscillates or cycles through values.

### 5.4 Planned C Concepts

| C concept | Where it is used | Why it is needed |
|---|---|---|
| Functions | Every module; one job per function, a `Status_t` result, outputs through pointers | Small, testable units; errors are returned, never ignored |
| Structs | `Net_t`, `Gate_t`, `Block_t`, `Netlist_t`, `Event_t`, `EventQueue_t`, `SetLine_t`, `ClockDef_t`, `Stimulus_t`, `SimKernel_t`, `SimResult_t`, `VcdWriter_t`, `Diag_t` | Group the data of one net, gate, event, line or run |
| Enums | `Status_t`, `GateType_t`, `DriverKind_t`, `EventKind_t`, `StopReason_t`, `ExitCode_t` | Named values instead of magic numbers; `switch` with `default` |
| Arrays | Nets, gates, blocks, events, set lines, clocks, logic lookup tables, the stack of open files for `include` | Fixed-size tables, no dynamic memory; the include stack avoids recursion |
| Strings | Net and gate names, words of a line, messages, file paths | Parse text files; bounded copies only (`snprintf` with the target size, or `memcpy` after a length check) |
| Pointers | Out-parameters, tables passed by pointer, in-place word splitting, `void *` callback context | Return results and status at the same time; avoid copying big tables |
| Named constants | `include/config.h` (limits, file names), `U` suffix on unsigned literals | No magic numbers; one place to change a limit |
| File handling | `fopen`/`fgets`/`fclose` for the input files, `fprintf`/`fputs` for `waves.vcd` | Input and output are text files; every call is checked |
| Status codes | One `Status_t` for all modules with a text table | Uniform error handling and readable error messages |
| Callbacks (function pointers) | The kernel calls its listener (the VCD writer) and the stimulus expansion hook (`st_expand`) | The kernel stays independent of what is written and of how clock edges are made |

Dynamic memory is intentionally avoided (MISRA C:2012 Rule 21.3 and Directive 4.12): every table is a fixed-size static
array sized in `include/config.h`.

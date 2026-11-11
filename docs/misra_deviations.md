# GateSim: MISRA C:2012 Deviations

GateSim's C code follows MISRA C:2012 (NFR-07). This file is the detailed record behind the deviation log in
[detailed_design.md section 12.5](detailed_design.md#125-misra-c2012-compliance-and-deviation-log): every rule the code
does not meet, where, why, what could go wrong and how that is prevented.

## How the code is checked

```bash
sudo apt install cppcheck      # once (the results below are from cppcheck 2.13)
make check
```

`make check` runs cppcheck with its MISRA C:2012 addon on every file in `src/` (and the headers they include from
`include/`):

```
cppcheck --std=c99 --enable=warning,style,performance,portability \
         --suppressions-list=tools/misra_suppressions.txt \
         --addon=misra --inline-suppr --error-exitcode=1 \
         --suppress=missingIncludeSystem -q -Iinclude src
```

It exits with 0 when nothing is found and with 1 on any finding. The current code gives exit code 0 and no output.
The only findings that are suppressed are the deviations below:
- Rule 21.6 through `tools/misra_suppressions.txt`, one line per file that includes `<stdio.h>`: `src/main.c`,
  `src/text_utils.c`, `include/text_utils.h` and `include/report.h` (report.c gets stdio through report.h, so
  cppcheck reports the include there).
- Rule 11.5 through a comment on the line before each cast, so a new cast is reported until it is reviewed and
  given its own comment:
  `/* cppcheck-suppress misra-c2012-11.5 ; listener context, MISRA deviation */`
  (for the `st_expand` cast the text is `expansion hook source` instead of `listener context`)

Every other finding was fixed in the code. Because of the house function style, the rules that often need deviations
in small programs are met: one `return` per function (15.5), no `break` out of loops (15.4; `break` appears only at
the end of `switch` cases), every `if ... else if` chain ends with `else` (15.7), every `switch` has a `default` (16.4),
no dynamic memory (21.3), no recursion (17.2), no `<time.h>` (21.10, so the VCD file has no `$date`).

## Deviations

| Rule | Location | Justification | Risk | Mitigation |
|---|---|---|---|---|
| 21.6 (required): the standard library input/output functions shall not be used | `src/main.c`; `src/text_utils.c`, `include/text_utils.h`; `src/report.c` (through `include/report.h`) | A command-line tool on a PC must read its input files line by line, write its waveform file and print its results. `text_utils` reads the `.net` and `.stim` files (`fopen`, `fgets`, `ferror`, `feof`, `fclose`; `snprintf` for the error message and the joined paths); its header needs `FILE` for `LineReader_t`. `report` writes `waves.vcd` (`fopen`, `fprintf`, `fputs`, `fclose`); its header needs `FILE` for `VcdWriter_t`. `main.c` prints the usage text, the check-only summary, the run result and the error messages (`printf`, `fprintf` to stdout and stderr) | Unchecked or unbounded input/output: a read past a buffer, a lost write, a file left open | Every return value is used or cast to `(void)`; only bounded calls (`fgets` with the buffer size, `snprintf` with the target size; never `gets`, `scanf` or `sprintf`); a line longer than the buffer is an error (`STATUS_LINE_TOO_LONG`), not a silent cut; a failed write is remembered and reported by `rw_vcd_close` (`STATUS_FILE_WRITE`); every opened file is closed on every path. stdio calls stay in these three modules: the readers (`netlist.c`, `stimulus.c`) call no stdio function (they see `FILE` only inside `LineReader_t`, through `text_utils.h`), and `logic`, `event_queue`, `sim_kernel` and `status` do not include it at all |
| 11.5 (advisory): a conversion should not be performed from pointer to void into pointer to object | The 3 casts listed below | The kernel calls its listener and the expansion hook through callback types with a `void *` (`ctx`, `source`), so it does not depend on the modules that listen (report) or expand clock edges (stimulus). Each callback turns the pointer back into its own struct | The pointer could point to another type, and the callback would read the wrong memory | The listener is built only by `rw_vcd_listener`, which always stores a pointer to its own `VcdWriter_t`; the listener callbacks `vcd_on_change` and `vcd_on_end` are `static`, so no other code can register them. `st_expand` is public (stimulus.h), but in the program it is passed to `sk_set_source` in one place only, `run_simulation` in `main.c`, together with the `Stimulus_t` it loaded (the unit tests do the same with the `Stimulus_t` they load). `st_expand` also checks the event's kind, its clock index and its value. Every callback checks the pointer for NULL before use |

### The 11.5 casts

| File | Function | Statement | What `ctx` / `source` is |
|---|---|---|---|
| `src/report.c` | `vcd_on_change` | `VcdWriter_t *const w = (VcdWriter_t *)ctx;` | the `ctx` of `rw_vcd_listener` |
| `src/report.c` | `vcd_on_end` | `VcdWriter_t *const w = (VcdWriter_t *)ctx;` | the `ctx` of `rw_vcd_listener` |
| `src/stimulus.c` | `st_expand` | `const Stimulus_t *const st = (const Stimulus_t *)source;` | the `source` that main.c gives `sk_set_source` (always the loaded `Stimulus_t`) |

## Not needed

- 21.10 (`<time.h>`): not used anywhere; the VCD header has no `$date`.
- 21.3 (`malloc`, `free`): not used; every table is a fixed-size array sized in `include/config.h`.
- 21.7 (`atoi` and friends): not used; numbers are read with `tu_parse_u32`, which checks every digit and the range.

## Limits of this check

cppcheck's addon checks the rules it can decide from the code; it does not ship the MISRA rule texts (so its
messages say `misra violation (use --rule-texts=<file> to get proper output)` with the rule number). The rules it
cannot check (for example the documentation directives) are covered by code review and the design document
(section 12).

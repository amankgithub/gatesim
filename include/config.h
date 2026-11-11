/**
 * @file    config.h
 * @brief   Every limit and named constant of GateSim (fixed tables, no malloc).
 * @details Each table of the program has a fixed size given here, so the memory a run needs is known
 *          before it starts. A file that needs more than a limit is rejected with STATUS_TABLE_FULL (or
 *          STATUS_LINE_TOO_LONG, STATUS_TOO_MANY_WORDS) and a message; it is never cut short silently.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---- text and files ---- */
#define MAX_LINE_LEN        256U     /* characters in one file line, incl. '\n' and '\0' */
#define MAX_WORDS           16U      /* words in one line (the longest, an inst line with 8 ports, has 11) */
#define MAX_NAME_LEN        32U      /* characters in a name, incl. '\0' (31 visible) */
#define MAX_PATH_LEN        256U     /* characters in a file path, incl. '\0' */
#define MAX_MSG_LEN         160U     /* characters in one diagnostic message, incl. '\0' */
#define MAX_INCLUDE_DEPTH   2U       /* files open at once: the netlist and one included library */

/* ---- circuit ---- */
#define MAX_NETS            512U     /* nets in one netlist (block instances add their inner nets) */
#define MAX_GATES           512U     /* gates in one netlist (block instances add their inner gates) */
#define MAX_GATE_INPUTS     4U       /* inputs of one gate (NOT and BUF have exactly 1) */
#define MIN_DELAY           1U       /* gate delay in ns: a gate never switches in zero time */
#define MAX_DELAY           1000U    /* gate delay in ns (1 us, far above any real gate) */
#define MAX_WARNINGS        16U      /* unused gate outputs remembered for the check summary (all are counted) */
#define MAX_BLOCKS          8U       /* subckt blocks in one netlist */
#define MAX_BLOCK_GATES     16U      /* gate lines in one block */
#define MAX_BLOCK_PORTS     8U       /* ports of one block (inputs and outputs) */

/* ---- time and events ---- */
#define MAX_TIME_NS         1000000000U  /* latest simulated time, ns (1 s); twice it still fits in uint32_t */
#define MAX_EVENTS          1024U    /* pending events in the event list */

/* ---- stimulus ---- */
#define MAX_SET_LINES       256U     /* set lines in one .stim file */
#define MAX_CLOCKS          8U       /* clock lines in one .stim file: the 8 input bits of the 4-bit adder demo,
                                        each driven by its own clock to step through every input combination */
#define MIN_CLOCK_PERIOD    2U       /* ns: a clock is at least 1 ns high and 1 ns low */

/* ---- output ---- */
#define VCD_FIRST_CODE      33U      /* '!', the first character of a VCD net code */
#define VCD_CODE_COUNT      94U      /* '!' .. '~': one-character codes; two characters cover all MAX_NETS */
#define DEFAULT_OUT_DIR     "output" /* folder of the output files, relative to where the program runs */
#define VCD_FILE_NAME       "waves.vcd"   /* the waveform file in the output folder */
#define GATESIM_VERSION     "1.0"    /* in the VCD $version line (program version) */

#endif /* CONFIG_H */

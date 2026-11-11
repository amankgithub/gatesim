/**
 * @file    main.c
 * @brief   Entry point of GateSim: the command line and the high-level flow only.
 * @details `gatesim <netlist.net> <stimulus.stim> [--out <dir>]`: load both files, run the
 *          simulation with the stimulus as the kernel's source of clock edges and the VCD writer
 *          as its listener (<dir>/waves.vcd), then print why and when the run stopped and where
 *          the waveform is. `gatesim --check-only <netlist.net>`: load and check the netlist and
 *          print a summary. `gatesim --help`: the usage text. Errors go to stderr as
 *          "gatesim: file:line: text 'word'". Console output uses <stdio.h>: a recorded MISRA
 *          C:2012 Rule 21.6 deviation.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "event_queue.h"
#include "logic.h"
#include "netlist.h"
#include "report.h"
#include "sim_kernel.h"
#include "status.h"
#include "stimulus.h"
#include "text_utils.h"

/** Number of command-line words of "gatesim <netlist.net> <stimulus.stim>" and of "gatesim --check-only <net>". */
#define ARGC_TWO_FILES      3
/** Number of command-line words of "gatesim --help". */
#define ARGC_ONE_OPTION     2
/** Number of command-line words of "gatesim <netlist.net> <stimulus.stim> --out <dir>". */
#define ARGC_WITH_OUT       5
/** Positions of the words after the program name. */
#define ARG_FIRST           1
#define ARG_NET             1
#define ARG_STIM            2
#define ARG_CHECK_NET       2
#define ARG_OUT_OPTION      3
#define ARG_OUT_DIR         4
/** The options. */
#define OPTION_CHECK_ONLY   "--check-only"
#define OPTION_HELP         "--help"
#define OPTION_OUT          "--out"

/** @brief Exit codes of the program. */
typedef enum
{
    EXIT_OK = 0,            /**< the run (or the check, or the help text) is done */
    EXIT_INPUT_ERROR = 2,   /**< wrong arguments, or a file could not be read or has an error */
    EXIT_RUN_ERROR = 3      /**< the run could not be completed (event list full, time overflow) */
} ExitCode_t;

/** @brief What the command line asks for. */
typedef enum
{
    MODE_RUN = 0,       /**< gatesim <netlist.net> <stimulus.stim> [--out <dir>] */
    MODE_CHECK_ONLY,    /**< gatesim --check-only <netlist.net> */
    MODE_HELP,          /**< gatesim --help */
    MODE_BAD_ARGS       /**< anything else */
} Mode_t;

/** @brief Everything the program works on: one static object in main, shared by the helpers below. */
typedef struct
{
    Netlist_t    nl;       /**< the circuit */
    Stimulus_t   st;       /**< the stimulus */
    EventQueue_t queue;    /**< the event list */
    SimKernel_t  kernel;   /**< the simulation */
    VcdWriter_t  vcd;      /**< the waveform file, the listener of the kernel */
} App_t;

/**
 * @brief   Prints the usage text.
 * @param   stream  stdout (after --help) or stderr (after wrong arguments).
 */
static void print_usage(FILE *stream)
{
    (void)fprintf(stream, "usage: gatesim <netlist.net> <stimulus.stim> [--out <dir>]\n"
                          "           run the simulation, write <dir>/%s (default <dir>: %s, it must exist)\n",
                  VCD_FILE_NAME, DEFAULT_OUT_DIR);
    (void)fprintf(stream, "       gatesim --check-only <netlist.net>\n"
                          "           read and check the netlist, print a summary\n");
    (void)fprintf(stream, "       gatesim --help\n"
                          "           this text\n");
}

/**
 * @brief   Finds out what the command line asks for.
 * @param   argc       Number of words.
 * @param   first      The first word after the program name ("" when there is none).
 * @param   out_word   The word at the place of "--out" ("" when there is none).
 * @return  The mode; MODE_BAD_ARGS for anything that is not one of the three forms.
 */
static Mode_t parse_mode(int argc, const char *first, const char *out_word)
{
    Mode_t mode = MODE_BAD_ARGS;

    if ((argc == ARGC_ONE_OPTION) && (strcmp(first, OPTION_HELP) == 0))
    {
        mode = MODE_HELP;
    }
    else if ((argc == ARGC_TWO_FILES) && (strcmp(first, OPTION_CHECK_ONLY) == 0))
    {
        mode = MODE_CHECK_ONLY;
    }
    else if ((argc == ARGC_TWO_FILES) || ((argc == ARGC_WITH_OUT) && (strcmp(out_word, OPTION_OUT) == 0)))
    {
        mode = MODE_RUN;
    }
    else
    {
        mode = MODE_BAD_ARGS;
    }
    return mode;
}

/**
 * @brief   Prints a load error on stderr: "gatesim: file:line: text"
 * @details No line part for line 0, no file part for an empty file name.
 * @param   diag  The message filled by the reader.
 */
static void print_diag(const Diag_t *diag)
{
    if (diag->file[0] == '\0')
    {
        (void)fprintf(stderr, "gatesim: %s\n", diag->text);
    }
    else if (diag->line == 0U)
    {
        (void)fprintf(stderr, "gatesim: %s: %s\n", diag->file, diag->text);
    }
    else
    {
        (void)fprintf(stderr, "gatesim: %s:%" PRIu32 ": %s\n", diag->file, diag->line, diag->text);
    }
}

/**
 * @brief   Loads and checks the netlist; prints the message of an error.
 * @param   app   In/out: the netlist is filled.
 * @param   path  The .net file.
 * @return  EXIT_OK or EXIT_INPUT_ERROR.
 */
static ExitCode_t load_netlist(App_t *app, const char *path)
{
    ExitCode_t code = EXIT_OK;
    Diag_t diag;

    if (nl_load(&app->nl, path, &diag) != STATUS_OK)
    {
        print_diag(&diag);
        code = EXIT_INPUT_ERROR;
    }
    return code;
}

/**
 * @brief   Prints the summary of a loaded netlist (--check-only)
 * @details On stdout: the file, the gates per type (in GateType_t order, only types that are used),
 *          the nets, the blocks and instances, the warnings (each net that is driven but never read).
 * @param   nl    The loaded and checked netlist.
 * @param   path  Its file name.
 */
static void print_check_only(const Netlist_t *nl, const char *path)
{
    uint32_t type_count[GT_COUNT];
    bool first = true;
    uint32_t i;

    for (i = 0U; i < (uint32_t)GT_COUNT; i++)
    {
        type_count[i] = 0U;
    }
    for (i = 0U; i < (uint32_t)nl->gate_count; i++)
    {
        type_count[nl->gates[i].type]++;
    }
    (void)printf("%s: OK\n", path);
    (void)printf("  gates    : %" PRIu16, nl->gate_count);
    /* the gate types in GateType_t order, only those that are used: " (NAND 1, NOT 4)" */
    for (i = 0U; i < (uint32_t)GT_COUNT; i++)
    {
        if (type_count[i] > 0U)
        {
            (void)printf("%s%s %" PRIu32, first ? " (" : ", ", lg_type_name((GateType_t)i), type_count[i]);
            first = false;
        }
    }
    (void)printf("%s\n", first ? "" : ")");
    (void)printf("  nets     : %" PRIu16 " (inputs %" PRIu16 ", outputs %" PRIu16 ")\n", nl->net_count,
                 nl->input_count, nl->output_count);
    (void)printf("  blocks   : %" PRIu8 " defined, %" PRIu16 " instances\n", nl->block_count, nl->inst_count);
    (void)printf("  warnings : %" PRIu16 "\n", nl->warning_count);
    /* warning_count counts every unused net, but only the first MAX_WARNINGS are kept by name */
    for (i = 0U; (i < (uint32_t)nl->warning_count) && (i < MAX_WARNINGS); i++)
    {
        (void)printf("  warning: net '%s' is driven but never read\n", nl->nets[nl->unused[i]].name);
    }
}

/**
 * @brief   Gives the text of a stop reason for the console line.
 * @param   reason  Why the run stopped.
 * @return  "stable: nothing left to happen" or "end time reached"; "unknown" for any other value.
 */
static const char *stop_reason_text(StopReason_t reason)
{
    const char *text = "unknown";

    switch (reason)
    {
        case STOP_QUEUE_EMPTY:
            text = "stable: nothing left to happen";
            break;
        case STOP_END_TIME:
            text = "end time reached";
            break;
        default:
            text = "unknown";
            break;
    }
    return text;
}

/**
 * @brief   Opens the waveform file <out_dir>/waves.vcd and makes it the kernel's listener.
 * @param   app       In/out: the kernel gets the listener, the writer is opened.
 * @param   out_dir   The output folder.
 * @param   vcd_path  Out: the path of the file (MAX_PATH_LEN bytes), written only on STATUS_OK.
 * @return  STATUS_OK, or why the waveform cannot be written (the run goes on without it).
 */
static Status_t start_waveform(App_t *app, const char *out_dir, char vcd_path[MAX_PATH_LEN])
{
    SkListener_t listener;
    char path[MAX_PATH_LEN];
    Status_t status = tu_join_dir(out_dir, VCD_FILE_NAME, path, MAX_PATH_LEN);

    if (status == STATUS_OK)
    {
        status = rw_vcd_open(&app->vcd, &app->nl, path);
    }
    if (status == STATUS_OK)
    {
        status = rw_vcd_listener(&app->vcd, &listener);
        if (status == STATUS_OK)
        {
            status = sk_set_listener(&app->kernel, &listener);
        }
        if (status != STATUS_OK)
        {
            /* the file is useless without the listener */
            (void)rw_vcd_close(&app->vcd);
        }
    }
    if (status == STATUS_OK)
    {
        (void)tu_copy_text(vcd_path, MAX_PATH_LEN, path);
    }
    return status;
}

/**
 * @brief   Loads the stimulus, runs the simulation with the waveform writer and prints the result.
 * @details Order of calls: an empty event list, the stimulus events, the kernel with the end time of
 *          the stimulus, the stimulus as the source of the clock edges, the waveform writer as the
 *          listener; then the run, and the close of the waveform. Prints "run: stopped at ..." and where
 *          the waveform is (or why it was not written).
 * @param   app        In/out: the netlist is loaded; the stimulus, event list, kernel and writer are filled.
 * @param   stim_path  The .stim file.
 * @param   out_dir    The folder for waves.vcd.
 * @return  EXIT_OK (also when only the waveform failed); EXIT_INPUT_ERROR (stimulus error);
 *          EXIT_RUN_ERROR (the run failed).
 */
static ExitCode_t run_simulation(App_t *app, const char *stim_path, const char *out_dir)
{
    ExitCode_t code = EXIT_OK;
    Diag_t diag;
    Status_t status = st_load(&app->st, &app->nl, stim_path, &diag);

    if (status != STATUS_OK)
    {
        print_diag(&diag);
        code = EXIT_INPUT_ERROR;
    }
    else
    {
        char vcd_path[MAX_PATH_LEN];
        Status_t vcd_status = STATUS_OK;
        SimResult_t result;

        status = eq_init(&app->queue);
        if (status == STATUS_OK)
        {
            status = st_seed(&app->st, &app->queue);
        }
        if (status == STATUS_OK)
        {
            status = sk_init(&app->kernel, &app->nl, &app->queue, app->st.end_time);
        }
        if (status == STATUS_OK)
        {
            /* clock edges are expanded by the stimulus module, one edge at a time */
            status = sk_set_source(&app->kernel, &app->st, st_expand);
        }
        if (status == STATUS_OK)
        {
            /* a waveform that cannot be written does not stop the run */
            vcd_status = start_waveform(app, out_dir, vcd_path);
            status = sk_run(&app->kernel, &result);
            if (vcd_status == STATUS_OK)
            {
                vcd_status = rw_vcd_close(&app->vcd);
            }
        }
        if (status == STATUS_OK)
        {
            (void)printf("run: stopped at %" PRIu32 " ns (%s), %" PRIu32 " events\n", result.stop_time,
                         stop_reason_text(result.reason), result.events_applied);
            if (vcd_status == STATUS_OK)
            {
                (void)printf("waveform: %s\n", vcd_path);
            }
            else
            {
                (void)printf("waveform: not written (%s)\n", status_text(vcd_status));
            }
        }
        else
        {
            (void)fprintf(stderr, "gatesim: run failed: %s\n", status_text(status));
            code = EXIT_RUN_ERROR;
        }
    }
    return code;
}

int main(int argc, char *argv[])
{
    /* static: the tables are too big for the stack; block scope: only main owns them (MISRA 8.9) */
    static App_t app;
    const char *const first = (argc > ARG_FIRST) ? argv[ARG_FIRST] : "";
    const char *const out_word = (argc > ARG_OUT_OPTION) ? argv[ARG_OUT_OPTION] : "";
    ExitCode_t code = EXIT_OK;

    switch (parse_mode(argc, first, out_word))
    {
        case MODE_HELP:
            print_usage(stdout);
            break;
        case MODE_CHECK_ONLY:
            code = load_netlist(&app, argv[ARG_CHECK_NET]);
            if (code == EXIT_OK)
            {
                print_check_only(&app.nl, argv[ARG_CHECK_NET]);
            }
            break;
        case MODE_RUN:
            code = load_netlist(&app, argv[ARG_NET]);
            if (code == EXIT_OK)
            {
                const char *const out_dir = (argc == ARGC_WITH_OUT) ? argv[ARG_OUT_DIR] : DEFAULT_OUT_DIR;

                code = run_simulation(&app, argv[ARG_STIM], out_dir);
            }
            break;
        default:
            print_usage(stderr);
            code = EXIT_INPUT_ERROR;
            break;
    }
    return (int)code;
}

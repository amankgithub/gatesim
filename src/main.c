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

#define ARGC_TWO_FILES      3
#define ARGC_ONE_OPTION     2
#define ARGC_WITH_OUT       5
#define ARG_FIRST           1
#define ARG_NET             1
#define ARG_STIM            2
#define ARG_CHECK_NET       2
#define ARG_OUT_OPTION      3
#define ARG_OUT_DIR         4
#define OPTION_CHECK_ONLY   "--check-only"
#define OPTION_HELP         "--help"
#define OPTION_OUT          "--out"

typedef enum
{
    EXIT_OK = 0,
    EXIT_INPUT_ERROR = 2,
    EXIT_RUN_ERROR = 3
} ExitCode_t;

typedef enum
{
    MODE_RUN = 0,
    MODE_CHECK_ONLY,
    MODE_HELP,
    MODE_BAD_ARGS
} Mode_t;

typedef struct
{
    Netlist_t    nl;
    Stimulus_t   st;
    EventQueue_t queue;
    SimKernel_t  kernel;
    VcdWriter_t  vcd;
} App_t;

/* Prints the usage text. */
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

/* Finds out what the command line asks for. */
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

/* Prints a load error on stderr: "gatesim: file:line: text" */
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

/* Loads and checks the netlist; prints the message of an error. */
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

/* Prints the summary of a loaded netlist (--check-only): the file, the gates per type, the nets, the warnings. */
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
    (void)printf("  warnings : %" PRIu16 "\n", nl->warning_count);
    for (i = 0U; (i < (uint32_t)nl->warning_count) && (i < MAX_WARNINGS); i++)
    {
        (void)printf("  warning: net '%s' is driven but never read\n", nl->nets[nl->unused[i]].name);
    }
}

/* Gives the text of a stop reason for the console line. */
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

/* Opens the waveform file <out_dir>/waves.vcd and makes it the kernel's listener. */
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
            (void)rw_vcd_close(&app->vcd);
        }
    }
    if (status == STATUS_OK)
    {
        (void)tu_copy_text(vcd_path, MAX_PATH_LEN, path);
    }
    return status;
}

/* Loads the stimulus, runs the simulation with the waveform writer and prints the result. */
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
            status = sk_set_source(&app->kernel, &app->st, st_expand);
        }
        if (status == STATUS_OK)
        {
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

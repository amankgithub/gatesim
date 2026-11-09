/**
 * @file    main.c
 * @brief   Entry point of GateSim: the command line and the high-level flow only.
 * @details `gatesim <netlist.net> <stimulus.stim>`: load both files, run the simulation
 *          and print every change of an output net, then why and when the run stopped.
 *          Errors go to stderr as "gatesim: file:line: text". Console output uses
 *          <stdio.h>: a recorded MISRA C:2012 Rule 21.6 deviation.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "event_queue.h"
#include "logic.h"
#include "netlist.h"
#include "sim_kernel.h"
#include "status.h"
#include "stimulus.h"

#define ARGC_RUN    3

typedef enum
{
    EXIT_OK = 0,
    EXIT_INPUT_ERROR = 2,
    EXIT_RUN_ERROR = 3
} ExitCode_t;

/* Prints the usage text on stderr (after wrong arguments). */
static void print_usage(void)
{
    (void)fprintf(stderr, "usage: gatesim <netlist.net> <stimulus.stim>\n");
}

/* Prints a load error on stderr: "gatesim: file:line: text" (no line part for line 0). */
static void print_load_error(const char *path, uint32_t line, Status_t status)
{
    if (line == 0U)
    {
        (void)fprintf(stderr, "gatesim: %s: %s\n", path, status_text(status));
    }
    else
    {
        (void)fprintf(stderr, "gatesim: %s:%" PRIu32 ": %s\n", path, line, status_text(status));
    }
}

/* Prints every change of an output net in the step just processed: "<t> ns: <net> = <v>". */
static void print_output_changes(const SimKernel_t *k)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)k->changed_count; i++)
    {
        const Net_t *net = &k->nl->nets[k->changed[i]];

        if (net->is_output)
        {
            (void)printf("%" PRIu32 " ns: %s = %c\n", k->now, net->name, lg_value_char(net->value));
        }
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

int main(int argc, char *argv[])
{
    static Netlist_t nl;
    static Stimulus_t st;
    static EventQueue_t queue;
    static SimKernel_t kernel;
    ExitCode_t code = EXIT_OK;

    if (argc != ARGC_RUN)
    {
        print_usage();
        code = EXIT_INPUT_ERROR;
    }
    else
    {
        const char *const net_path = argv[1];
        const char *const stim_path = argv[2];
        uint32_t line = 0U;
        Status_t status = nl_load(&nl, net_path, &line);

        if (status != STATUS_OK)
        {
            print_load_error(net_path, line, status);
            code = EXIT_INPUT_ERROR;
        }
        else
        {
            status = st_load(&st, &nl, stim_path, &line);
            if (status != STATUS_OK)
            {
                print_load_error(stim_path, line, status);
                code = EXIT_INPUT_ERROR;
            }
        }
        if (code == EXIT_OK)
        {
            bool done = false;

            status = eq_init(&queue);
            if (status == STATUS_OK)
            {
                status = st_seed(&st, &queue);
            }
            if (status == STATUS_OK)
            {
                status = sk_init(&kernel, &nl, &queue, st.end_time);
            }
            while ((status == STATUS_OK) && !done)
            {
                status = sk_step(&kernel, &done);
                if ((status == STATUS_OK) && !done)
                {
                    print_output_changes(&kernel);
                }
            }
            if (status == STATUS_OK)
            {
                const uint32_t stop_time = (kernel.reason == STOP_END_TIME) ? st.end_time : kernel.now;

                (void)printf("run: stopped at %" PRIu32 " ns (%s), %" PRIu32 " events\n", stop_time,
                             stop_reason_text(kernel.reason), kernel.events_applied);
            }
            else
            {
                (void)fprintf(stderr, "gatesim: run failed: %s\n", status_text(status));
                code = EXIT_RUN_ERROR;
            }
        }
    }
    return (int)code;
}

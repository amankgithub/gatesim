#ifndef STIMULUS_H
#define STIMULUS_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "event_queue.h"
#include "logic.h"
#include "netlist.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t     time;
    uint16_t     net;
    LogicValue_t value;
} SetLine_t;

typedef struct
{
    SetLine_t sets[MAX_SET_LINES];
    uint16_t  set_count;
    bool      has_end;
    uint32_t  end_time;
} Stimulus_t;

/* Empties the stimulus: no set lines, no end line. */
Status_t st_init(Stimulus_t *st);

/* Handles one line of a .stim file: `set NET VALUE */
Status_t st_parse_line(Stimulus_t *st, const Netlist_t *nl, char *line, Diag_t *out_diag);

/* Reads a whole .stim file line by line, then checks that it had an end line. */
Status_t st_load(Stimulus_t *st, const Netlist_t *nl, const char *path, Diag_t *out_diag);

/* Adds the first events of a run: one EV_SET per set line, in file order. */
Status_t st_seed(const Stimulus_t *st, EventQueue_t *queue);

#ifdef __cplusplus
}
#endif

#endif

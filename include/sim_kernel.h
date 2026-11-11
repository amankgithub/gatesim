#ifndef SIM_KERNEL_H
#define SIM_KERNEL_H

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

typedef enum
{
    STOP_QUEUE_EMPTY = 0,
    STOP_END_TIME
} StopReason_t;

typedef struct
{
    Netlist_t    *nl;
    EventQueue_t *queue;
    uint32_t      end_time;
    bool          first;
    bool          done;
    StopReason_t  reason;
    uint32_t      now;
    uint32_t      events_applied;
    bool          changed_flag[MAX_NETS];
    uint16_t      changed[MAX_NETS];
    LogicValue_t  old_value[MAX_NETS];
    uint16_t      changed_count;
} SimKernel_t;

/* Prepares a run: every net value becomes X (power-up unknown) and no gate has a pending change. */
Status_t sk_init(SimKernel_t *k, Netlist_t *nl, EventQueue_t *queue, uint32_t end_time);

/* Gives the delay of a gate's output change */
Status_t sk_gate_delay(const Gate_t *gate, LogicValue_t new_value, uint16_t *out_delay);

/* Processes one time step. */
Status_t sk_step(SimKernel_t *k, bool *out_done);

#ifdef __cplusplus
}
#endif

#endif

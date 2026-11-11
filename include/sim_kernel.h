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
    StopReason_t reason;
    uint32_t     stop_time;
    uint32_t     events_applied;
} SimResult_t;

typedef struct
{
    uint16_t     net;
    LogicValue_t value;
    bool         has_next;
    uint32_t     next_time;
    LogicValue_t next_value;
} SkExpansion_t;

typedef Status_t (*SkExpandFn_t)(const void *source, const Event_t *event, uint32_t end_time,
                                 SkExpansion_t *out);

typedef void (*SkOnChangeFn_t)(void *ctx, uint16_t net, uint32_t now, LogicValue_t old_value,
                               LogicValue_t new_value);

typedef struct
{
    void          *ctx;
    SkOnChangeFn_t on_change;
} SkListener_t;

typedef struct
{
    Netlist_t    *nl;
    EventQueue_t *queue;
    const void   *source;
    SkExpandFn_t  expand;
    SkListener_t  listener;
    bool          has_listener;
    uint32_t      end_time;
    bool          changed_flag[MAX_NETS];
    uint16_t      changed[MAX_NETS];
    LogicValue_t  old_value[MAX_NETS];
    uint16_t      changed_count;
} SimKernel_t;

/* Prepares a run: every net takes its start value (init_value: X, the power-up unknown; */
Status_t sk_init(SimKernel_t *k, Netlist_t *nl, EventQueue_t *queue, uint32_t end_time);

/* Sets the listener of the run (after sk_init, before sk_run); a second call replaces it. */
Status_t sk_set_listener(SimKernel_t *k, const SkListener_t *listener);

/* Sets the source of the stimulus events that the kernel does not know */
Status_t sk_set_source(SimKernel_t *k, const void *source, SkExpandFn_t expand);

/* Gives the delay of a gate's output change */
Status_t sk_gate_delay(const Gate_t *gate, LogicValue_t new_value, uint16_t *out_delay);

/* Runs the simulation until the list is empty or the next event is after the end time. */
Status_t sk_run(SimKernel_t *k, SimResult_t *out_result);

#ifdef __cplusplus
}
#endif

#endif

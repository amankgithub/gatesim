/**
 * @file    sim_kernel.h
 * @brief   The event-driven simulation with inertial delay: sk_run runs the whole loop.
 * @details One pass of the loop handles one time: take every event of the earliest time and apply it,
 *          re-calculate the gates that read a changed net and schedule each output change after the gate
 *          delay, then report every real net change to the listener.
 *          A gate has at most one pending change, so a pulse shorter than its delay is
 *          swallowed. Time jumps from one event to the next. Every net starts as X
 *          (unknown) until something drives it; VCC and GND start as 1 and 0. A stimulus event that the kernel
 *          does not know (a clock edge) is handed to the expansion hook of its source (sk_set_source),
 *          which says which net it sets and which event follows it. A run ends when nothing is left
 *          to happen or at the end time.
 */
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

/** @brief Why a run stopped. */
typedef enum
{
    STOP_QUEUE_EMPTY = 0,   /**< no event left: the circuit is stable */
    STOP_END_TIME           /**< the next event is after the end time */
} StopReason_t;

/** @brief The result of one run. */
typedef struct
{
    StopReason_t reason;           /**< why it stopped */
    uint32_t     stop_time;        /**< END_TIME: the end time; otherwise the time of the last step (0 if none), ns */
    uint32_t     events_applied;   /**< events taken from the list */
} SimResult_t;

/** @brief What one stimulus event does: the net it sets and the event that follows it. */
typedef struct
{
    uint16_t     net;          /**< the net it sets */
    LogicValue_t value;        /**< the new value of that net */
    bool         has_next;     /**< a follow-up event is added to the list */
    uint32_t     next_time;    /**< time of the follow-up, ns (valid when has_next; after the event's own time) */
    LogicValue_t next_value;   /**< value of the follow-up, which has the same kind and index */
} SkExpansion_t;

/**
 * @brief Expansion hook: turns a stimulus event that the kernel does not know into a net change
 *        and its follow-up. `source` is what sk_set_source was given; no follow-up after end_time.
 */
typedef Status_t (*SkExpandFn_t)(const void *source, const Event_t *event, uint32_t end_time,
                                 SkExpansion_t *out);

/**
 * @brief Called once for every net whose value really changed in a time step, after the
 *        gates of that step are calculated, in the order of the first change.
 */
typedef void (*SkOnChangeFn_t)(void *ctx, uint16_t net, uint32_t now, LogicValue_t old_value,
                               LogicValue_t new_value);

/** @brief Called once after a run that ended without an error, with its result. */
typedef void (*SkOnEndFn_t)(void *ctx, const SimResult_t *result);

/** @brief Who is told about a run (the waveform writer, a test recorder). */
typedef struct
{
    void          *ctx;         /**< the listener's own data, handed back to every callback */
    SkOnChangeFn_t on_change;   /**< may be NULL (any callback may be NULL) */
    SkOnEndFn_t    on_end;
} SkListener_t;

/** @brief The kernel: what it runs on, who listens, and the scratch data of one time step. */
typedef struct
{
    Netlist_t    *nl;                       /**< the circuit (net values and pending gate changes are run state) */
    EventQueue_t *queue;                    /**< the event list, seeded by the caller */
    const void   *source;                   /**< what expand reads (the stimulus); NULL without a source */
    SkExpandFn_t  expand;                   /**< expands every event kind other than EV_GATE and EV_SET */
    SkListener_t  listener;                 /**< told about the run (valid when has_listener) */
    bool          has_listener;             /**< sk_set_listener was called */
    uint32_t      end_time;                 /**< events after this time are not processed, ns */
    bool          changed_flag[MAX_NETS];   /**< net changed in this step ("visited" flag) */
    uint16_t      changed[MAX_NETS];        /**< the changed nets of this step, in order of first change */
    LogicValue_t  old_value[MAX_NETS];      /**< value at the start of the step, per changed[] entry */
    uint16_t      changed_count;            /**< used rows of changed[] and old_value[] */
} SimKernel_t;

/**
 * @brief   Prepares a run: every net takes its start value (init_value: X, the power-up unknown; 1 for VCC,
 *          0 for GND), no gate has a pending change and no net is marked as changed; no listener, no source.
 * @details The event list is not touched: the caller empties it (eq_init) and adds the stimulus events,
 *          before or after sk_init. Called again before every run, so a run that ended with an error in
 *          the middle of a step leaves nothing behind for the next one.
 * @param   k         Out: the kernel.
 * @param   nl        In/out: the circuit (kept by pointer; its net values and pending changes are reset).
 * @param   queue     The event list (kept by pointer).
 * @param   end_time  Last time that is processed, 0..MAX_TIME_NS.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if end_time > MAX_TIME_NS (nothing changes).
 */
Status_t sk_init(SimKernel_t *k, Netlist_t *nl, EventQueue_t *queue, uint32_t end_time);

/**
 * @brief   Sets the listener of the run (after sk_init, before sk_run); a second call replaces it.
 * @param   k         In/out: the kernel.
 * @param   listener  The listener (copied; its ctx must stay valid during sk_run).
 * @return  STATUS_OK; STATUS_NULL_ARG.
 */
Status_t sk_set_listener(SimKernel_t *k, const SkListener_t *listener);

/**
 * @brief   Sets the source of the stimulus events that the kernel does not know (after sk_init,
 *          before sk_run): such an event is applied through expand(source, ...).
 * @param   k       In/out: the kernel.
 * @param   source  What expand reads (kept by pointer; must stay valid during sk_run).
 * @param   expand  The expansion hook.
 * @return  STATUS_OK; STATUS_NULL_ARG.
 */
Status_t sk_set_source(SimKernel_t *k, const void *source, SkExpandFn_t expand);

/**
 * @brief   Gives the delay of a gate's output change: the rise delay for a change to 1, the
 *          fall delay for a change to 0, and the shorter of the two for a change to X.
 * @param   gate       The gate.
 * @param   new_value  The new output value.
 * @param   out_delay  Out: the delay in ns, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG for a value >= LV_COUNT.
 */
Status_t sk_gate_delay(const Gate_t *gate, LogicValue_t new_value, uint16_t *out_delay);

/**
 * @brief   Runs the simulation until the list is empty or the next event is after the end time.
 * @details Each time step: every event of that time is applied (EV_GATE and EV_SET directly,
 *          any other kind through the expansion hook, which sets its net and may add a follow-up event), the
 *          gates are calculated (all of them in the first step, later only those with a
 *          changed input) with the three rules of inertial delay (the same pending value is
 *          kept, a different one is cancelled, a value that differs from the output net is
 *          scheduled at now + its delay, sk_gate_delay), then the listener's on_change is
 *          called for every net whose value really changed at that time. After the loop, a run
 *          without an error calls the listener's on_end once with the result. One run per sk_init.
 * @param   k           In/out: the kernel (after sk_init and the stimulus events).
 * @param   out_result  Out: why and when the run stopped and how many events were applied,
 *                      written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG (an event with a bad index, an event to
 *          expand without a source);
 *          the status of the expansion hook; STATUS_QUEUE_FULL; STATUS_TIME_OVERFLOW (now + delay
 *          past MAX_TIME_NS);
 *          STATUS_NOT_FOUND (a pending gate event is missing from the list).
 *          After an error (no on_end, no result) the event list must be emptied and seeded again and the
 *          run prepared again with sk_init.
 */
Status_t sk_run(SimKernel_t *k, SimResult_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* SIM_KERNEL_H */

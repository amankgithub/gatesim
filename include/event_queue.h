/**
 * @file    event_queue.h
 * @brief   The event list of the simulation: future changes, each with a time.
 * @details An unsorted array. The earliest event is found by a min-finder on
 *          (time, seq); seq is the order of adding, so two events at the same time
 *          come out in the order they were added. Remove copies the last row into
 *          the hole. Every search is a short loop over at most MAX_EVENTS rows.
 */
#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdint.h>

#include "config.h"
#include "logic.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What an event does when its time comes. */
typedef enum
{
    EV_GATE = 0,    /**< a gate output takes its new value (index = gate) */
    EV_SET,         /**< a stimulus sets an input net (index = net) */
    EV_CLOCK,       /**< an edge of a clock line (index = clock); the stimulus module expands it */
    EV_COUNT        /**< number of kinds, keep last */
} EventKind_t;

/** @brief One future change. */
typedef struct
{
    uint32_t     time;    /**< when it happens, ns */
    uint32_t     seq;     /**< stamped by eq_add: 0, 1, 2, ... (breaks ties between equal times) */
    EventKind_t  kind;    /**< what it does */
    uint16_t     index;   /**< EV_GATE: gate; EV_SET: net; EV_CLOCK: clock */
    LogicValue_t value;   /**< the new value */
} Event_t;

/** @brief The event list (owned by the caller, passed by pointer; about 16 KB). */
typedef struct
{
    Event_t  rows[MAX_EVENTS];   /**< rows 0..count-1 are used, in no particular order */
    uint16_t count;              /**< used rows */
    uint32_t next_seq;           /**< seq of the next added event */
} EventQueue_t;

/**
 * @brief   Empties the list (count 0, next_seq 0).
 * @param   q  Out: the list.
 * @return  STATUS_OK or STATUS_NULL_ARG.
 */
Status_t eq_init(EventQueue_t *q);

/**
 * @brief   Adds an event after the used rows, stamped with seq = next_seq.
 * @param   q      In/out: the list.
 * @param   time   When it happens, ns.
 * @param   kind   EV_GATE, EV_SET or EV_CLOCK.
 * @param   index  Gate (EV_GATE), net (EV_SET) or clock (EV_CLOCK) index.
 * @param   value  A logic value (below LV_COUNT).
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG (unknown kind or value);
 *          STATUS_QUEUE_FULL when count == MAX_EVENTS (nothing changes).
 */
Status_t eq_add(EventQueue_t *q, uint32_t time, EventKind_t kind, uint16_t index, LogicValue_t value);

/**
 * @brief   Finds the earliest event: smallest time, then smallest seq (min-finder).
 * @param   q        The list.
 * @param   out_row  Out: its row, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_QUEUE_EMPTY when count == 0.
 */
Status_t eq_find_earliest(const EventQueue_t *q, uint16_t *out_row);

/**
 * @brief   Removes one row: the last row is copied into the hole and count goes down by 1.
 * @param   q          In/out: the list.
 * @param   row        The row to remove, 0..count-1.
 * @param   out_event  Out: the removed event, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if row >= count.
 */
Status_t eq_remove(EventQueue_t *q, uint16_t row, Event_t *out_event);

/**
 * @brief   Finds the pending event of one gate (linear search over the used rows).
 * @details The kernel keeps at most one EV_GATE event per gate, so the first match is the only one.
 * @param   q        The list.
 * @param   gate     The gate index.
 * @param   out_row  Out: the row of its EV_GATE event, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_NOT_FOUND if the gate has no event in the list.
 */
Status_t eq_find_gate_event(const EventQueue_t *q, uint16_t gate, uint16_t *out_row);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_QUEUE_H */

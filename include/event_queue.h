#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdint.h>

#include "config.h"
#include "logic.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    EV_GATE = 0,
    EV_SET,
    EV_CLOCK,
    EV_COUNT
} EventKind_t;

typedef struct
{
    uint32_t     time;
    uint32_t     seq;
    EventKind_t  kind;
    uint16_t     index;
    LogicValue_t value;
} Event_t;

typedef struct
{
    Event_t  rows[MAX_EVENTS];
    uint16_t count;
    uint32_t next_seq;
} EventQueue_t;

/* Empties the list (count 0, next_seq 0). */
Status_t eq_init(EventQueue_t *q);

/* Adds an event after the used rows, stamped with seq = next_seq. */
Status_t eq_add(EventQueue_t *q, uint32_t time, EventKind_t kind, uint16_t index, LogicValue_t value);

/* Finds the earliest event: smallest time, then smallest seq (min-finder). */
Status_t eq_find_earliest(const EventQueue_t *q, uint16_t *out_row);

/* Removes one row: the last row is copied into the hole and count goes down by 1. */
Status_t eq_remove(EventQueue_t *q, uint16_t row, Event_t *out_event);

/* Finds the pending event of one gate (linear search over the used rows). */
Status_t eq_find_gate_event(const EventQueue_t *q, uint16_t gate, uint16_t *out_row);

#ifdef __cplusplus
}
#endif

#endif

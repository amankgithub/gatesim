/**
 * @file    event_queue.c
 * @brief   Event list as an unsorted array: add, earliest (min-finder), remove, find a gate's event.
 * @details Simple on purpose: finding the earliest event looks at every used row,
 *          which for a few hundred pending events is fast and easy to check.
 */
#include "event_queue.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief   Tells whether event a comes before event b: smaller time, or the same time and smaller seq.
 * @param   a  First event.
 * @param   b  Second event.
 * @return  true if a is earlier.
 */
static bool is_earlier(const Event_t *a, const Event_t *b)
{
    return ((a->time < b->time) || ((a->time == b->time) && (a->seq < b->seq)));
}

Status_t eq_init(EventQueue_t *q)
{
    Status_t status = STATUS_NULL_ARG;

    if (q != NULL)
    {
        /* the rows keep old data; only rows 0..count-1 are ever read */
        q->count = 0U;
        q->next_seq = 0U;
        status = STATUS_OK;
    }
    return status;
}

Status_t eq_add(EventQueue_t *q, uint32_t time, EventKind_t kind, uint16_t index, LogicValue_t value)
{
    Status_t status = STATUS_OK;

    if (q == NULL)
    {
        status = STATUS_NULL_ARG;
    }
    else if ((kind >= EV_COUNT) || (value >= LV_COUNT))
    {
        status = STATUS_BAD_ARG;
    }
    else if ((uint32_t)q->count >= MAX_EVENTS)
    {
        status = STATUS_QUEUE_FULL;
    }
    else
    {
        Event_t *row = &q->rows[q->count];

        row->time = time;
        row->seq = q->next_seq;
        row->kind = kind;
        row->index = index;
        row->value = value;
        q->count++;
        q->next_seq++;
    }
    return status;
}

Status_t eq_find_earliest(const EventQueue_t *q, uint16_t *out_row)
{
    Status_t status = STATUS_OK;

    if ((q == NULL) || (out_row == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (q->count == 0U)
    {
        status = STATUS_QUEUE_EMPTY;
    }
    else
    {
        /* min-finder: start with row 0, keep the earlier one of each further row */
        uint32_t best = 0U;
        uint32_t i;

        for (i = 1U; i < (uint32_t)q->count; i++)
        {
            if (is_earlier(&q->rows[i], &q->rows[best]))
            {
                best = i;
            }
        }
        *out_row = (uint16_t)best;
    }
    return status;
}

Status_t eq_remove(EventQueue_t *q, uint16_t row, Event_t *out_event)
{
    Status_t status = STATUS_OK;

    if ((q == NULL) || (out_event == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (row >= q->count)
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        const uint16_t last = (uint16_t)(q->count - 1U);

        *out_event = q->rows[row];
        /* the order of the rows does not matter, so the last row fills the hole (no shifting) */
        q->rows[row] = q->rows[last];
        q->count = last;
    }
    return status;
}

Status_t eq_find_gate_event(const EventQueue_t *q, uint16_t gate, uint16_t *out_row)
{
    Status_t status = STATUS_NULL_ARG;

    if ((q != NULL) && (out_row != NULL))
    {
        uint32_t i = 0U;

        status = STATUS_NOT_FOUND;
        while ((i < (uint32_t)q->count) && (status != STATUS_OK))
        {
            if ((q->rows[i].kind == EV_GATE) && (q->rows[i].index == gate))
            {
                *out_row = (uint16_t)i;
                status = STATUS_OK;
            }
            i++;
        }
    }
    return status;
}

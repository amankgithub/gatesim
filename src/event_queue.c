#include "event_queue.h"

#include <stdbool.h>
#include <stddef.h>

/* Tells whether event a comes before event b: smaller time, or the same time and smaller seq. */
static bool is_earlier(const Event_t *a, const Event_t *b)
{
    return ((a->time < b->time) || ((a->time == b->time) && (a->seq < b->seq)));
}

Status_t eq_init(EventQueue_t *q)
{
    Status_t status = STATUS_NULL_ARG;

    if (q != NULL)
    {
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
        q->rows[row] = q->rows[last];
        q->count = last;
    }
    return status;
}

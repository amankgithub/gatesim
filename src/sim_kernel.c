#include "sim_kernel.h"

#include <stddef.h>

/* Gives a net a new value and remembers, once per step, that it changed and its old value. */
static void set_net(SimKernel_t *k, uint16_t net, LogicValue_t value)
{
    Net_t *n = &k->nl->nets[net];

    if (n->value != value)
    {
        if (!k->changed_flag[net])
        {
            k->changed_flag[net] = true;
            k->changed[k->changed_count] = net;
            k->old_value[k->changed_count] = n->value;
            k->changed_count++;
        }
        n->value = value;
    }
}

/* Applies a stimulus event that the kernel does not know, through the source's expansion hook */
static Status_t expand_event(SimKernel_t *k, const Event_t *event)
{
    Status_t status = STATUS_BAD_ARG;

    if (k->expand != NULL)
    {
        SkExpansion_t x;

        status = k->expand(k->source, event, k->end_time, &x);
        if (status == STATUS_OK)
        {
            set_net(k, x.net, x.value);
            if (x.has_next)
            {
                status = eq_add(k->queue, x.next_time, event->kind, event->index, x.next_value);
            }
        }
    }
    return status;
}

/* Applies one event taken from the list. */
static Status_t apply_event(SimKernel_t *k, const Event_t *event)
{
    Status_t status = STATUS_OK;

    switch (event->kind)
    {
        case EV_GATE:
            if (event->index < k->nl->gate_count)
            {
                Gate_t *g = &k->nl->gates[event->index];

                g->has_pending = false;
                set_net(k, g->out, event->value);
            }
            else
            {
                status = STATUS_BAD_ARG;
            }
            break;
        case EV_SET:
            if (event->index < k->nl->net_count)
            {
                set_net(k, event->index, event->value);
            }
            else
            {
                status = STATUS_BAD_ARG;
            }
            break;
        default:
            status = expand_event(k, event);
            break;
    }
    return status;
}

/* Takes every event of time now from the list and applies it. */
static Status_t apply_events_of_now(SimKernel_t *k, uint32_t now, uint32_t *events_applied)
{
    Status_t status = STATUS_OK;
    bool running = true;

    while ((status == STATUS_OK) && running)
    {
        uint16_t row = 0U;

        if ((eq_find_earliest(k->queue, &row) != STATUS_OK) || (k->queue->rows[row].time != now))
        {
            running = false;
        }
        else
        {
            Event_t event;

            status = eq_remove(k->queue, row, &event);
            if (status == STATUS_OK)
            {
                (*events_applied)++;
                status = apply_event(k, &event);
            }
        }
    }
    return status;
}

/* Tells whether a gate reads a net that changed in this step. */
static bool has_changed_input(const SimKernel_t *k, const Gate_t *gate)
{
    bool found = false;
    uint32_t i = 0U;

    while ((i < (uint32_t)gate->in_count) && !found)
    {
        found = k->changed_flag[gate->in[i]];
        i++;
    }
    return found;
}

/* Chooses the delay of an output change (worker of sk_gate_delay). */
static uint16_t gate_delay(const Gate_t *gate, LogicValue_t new_value)
{
    uint16_t delay = gate->rise;

    if (new_value == LV_0)
    {
        delay = gate->fall;
    }
    else if (new_value == LV_X)
    {
        delay = (gate->fall < gate->rise) ? gate->fall : gate->rise;
    }
    else
    {
        /* LV_1: the rise delay */
    }
    return delay;
}

/* Removes the promised output change of a gate from the event list (rule 2: cancel). */
static Status_t cancel_pending(SimKernel_t *k, uint16_t gate)
{
    uint16_t row = 0U;
    Status_t status = eq_find_gate_event(k->queue, gate, &row);

    if (status == STATUS_OK)
    {
        Event_t removed;

        status = eq_remove(k->queue, row, &removed);
    }
    if (status == STATUS_OK)
    {
        k->nl->gates[gate].has_pending = false;
    }
    return status;
}

/* Re-calculates one gate and applies the three rules of inertial delay. */
static Status_t evaluate_gate(SimKernel_t *k, uint16_t gate, uint32_t now)
{
    Gate_t *g = &k->nl->gates[gate];
    LogicValue_t inputs[MAX_GATE_INPUTS];
    LogicValue_t value = LV_0;
    uint32_t i;
    Status_t status;

    for (i = 0U; i < (uint32_t)g->in_count; i++)
    {
        inputs[i] = k->nl->nets[g->in[i]].value;
    }
    status = lg_eval(g->type, inputs, g->in_count, &value);
    if ((status == STATUS_OK) && (!g->has_pending || (g->pending != value)))
    {
        if (g->has_pending)
        {
            status = cancel_pending(k, gate);
        }
        if ((status == STATUS_OK) && (value != k->nl->nets[g->out].value))
        {
            const uint32_t delay = (uint32_t)gate_delay(g, value);

            if (now > (MAX_TIME_NS - delay))
            {
                status = STATUS_TIME_OVERFLOW;
            }
            else
            {
                status = eq_add(k->queue, now + delay, EV_GATE, gate, value);
                if (status == STATUS_OK)
                {
                    g->has_pending = true;
                    g->pending = value;
                }
            }
        }
    }
    return status;
}

/* Re-calculates every gate on the first step, later only the gates with a changed input. */
static Status_t evaluate_gates(SimKernel_t *k, uint32_t now, bool first)
{
    Status_t status = STATUS_OK;
    uint32_t g = 0U;

    while ((status == STATUS_OK) && (g < (uint32_t)k->nl->gate_count))
    {
        if (first || has_changed_input(k, &k->nl->gates[g]))
        {
            status = evaluate_gate(k, (uint16_t)g, now);
        }
        g++;
    }
    return status;
}

/* Tells the listener about every net whose value differs from the start of the step */
static void report_changes(SimKernel_t *k, uint32_t now)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)k->changed_count; i++)
    {
        const uint16_t net = k->changed[i];
        const LogicValue_t value = k->nl->nets[net].value;

        k->changed_flag[net] = false;
        if ((value != k->old_value[i]) && k->has_listener && (k->listener.on_change != NULL))
        {
            k->listener.on_change(k->listener.ctx, net, now, k->old_value[i], value);
        }
    }
    k->changed_count = 0U;
}

Status_t sk_init(SimKernel_t *k, Netlist_t *nl, EventQueue_t *queue, uint32_t end_time)
{
    Status_t status = STATUS_OK;

    if ((k == NULL) || (nl == NULL) || (queue == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (end_time > MAX_TIME_NS)
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        uint32_t i;

        for (i = 0U; i < (uint32_t)nl->net_count; i++)
        {
            nl->nets[i].value = nl->nets[i].init_value;
        }
        for (i = 0U; i < (uint32_t)nl->gate_count; i++)
        {
            nl->gates[i].has_pending = false;
        }
        for (i = 0U; i < MAX_NETS; i++)
        {
            k->changed_flag[i] = false;
        }
        k->nl = nl;
        k->queue = queue;
        k->source = NULL;
        k->expand = NULL;
        k->listener.ctx = NULL;
        k->listener.on_change = NULL;
        k->has_listener = false;
        k->end_time = end_time;
        k->changed_count = 0U;
    }
    return status;
}

Status_t sk_gate_delay(const Gate_t *gate, LogicValue_t new_value, uint16_t *out_delay)
{
    Status_t status = STATUS_OK;

    if ((gate == NULL) || (out_delay == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (new_value >= LV_COUNT)
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        *out_delay = gate_delay(gate, new_value);
    }
    return status;
}

Status_t sk_set_listener(SimKernel_t *k, const SkListener_t *listener)
{
    Status_t status = STATUS_NULL_ARG;

    if ((k != NULL) && (listener != NULL))
    {
        k->listener = *listener;
        k->has_listener = true;
        status = STATUS_OK;
    }
    return status;
}

Status_t sk_set_source(SimKernel_t *k, const void *source, SkExpandFn_t expand)
{
    Status_t status = STATUS_NULL_ARG;

    if ((k != NULL) && (source != NULL) && (expand != NULL))
    {
        k->source = source;
        k->expand = expand;
        status = STATUS_OK;
    }
    return status;
}

Status_t sk_run(SimKernel_t *k, SimResult_t *out_result)
{
    Status_t status = STATUS_OK;

    if ((k == NULL) || (out_result == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        StopReason_t reason = STOP_QUEUE_EMPTY;
        uint32_t events_applied = 0U;
        uint32_t last_time = 0U;
        bool first = true;
        bool running = true;

        while ((status == STATUS_OK) && running)
        {
            uint16_t row = 0U;
            const Status_t found = eq_find_earliest(k->queue, &row);

            if (found == STATUS_QUEUE_EMPTY)
            {
                reason = STOP_QUEUE_EMPTY;
                running = false;
            }
            else if (found != STATUS_OK)
            {
                status = found;
            }
            else if (k->queue->rows[row].time > k->end_time)
            {
                reason = STOP_END_TIME;
                running = false;
            }
            else
            {
                const uint32_t now = k->queue->rows[row].time;

                status = apply_events_of_now(k, now, &events_applied);
                if (status == STATUS_OK)
                {
                    status = evaluate_gates(k, now, first);
                }
                if (status == STATUS_OK)
                {
                    report_changes(k, now);
                    first = false;
                    last_time = now;
                }
            }
        }
        if (status == STATUS_OK)
        {
            SimResult_t result;

            result.reason = reason;
            result.stop_time = (reason == STOP_END_TIME) ? k->end_time : last_time;
            result.events_applied = events_applied;
            *out_result = result;
        }
    }
    return status;
}

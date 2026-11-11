/**
 * @file    sim_kernel.c
 * @brief   The event-driven simulation loop with inertial delay.
 * @details A gate holds at most one promised output change (has_pending, pending). When
 *          its inputs change, the new value keeps, cancels or replaces that promise, so a
 *          pulse shorter than the gate delay never reaches the output (inertial delay).
 *          Every delay is at least 1 ns, so a new event always lands after now and a step
 *          always ends. The state of a run (time, first step, event count) lives in
 *          sk_run's locals; the kernel struct keeps only the scratch data of one step.
 *          The kernel knows only gate events and plain input changes; any other stimulus
 *          event (a clock edge) is expanded by the hook of its source.
 */
#include "sim_kernel.h"

#include <stddef.h>

/**
 * @brief   Gives a net a new value and remembers, once per step, that it changed and its old value.
 * @param   k      In/out: the kernel.
 * @param   net    The net (a valid row).
 * @param   value  The new value.
 */
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

/**
 * @brief   Applies a stimulus event that the kernel does not know, through the source's expansion
 *          hook: the net it sets, then its follow-up event (same kind and index) if there is one.
 * @param   k      In/out: the kernel.
 * @param   event  The event.
 * @return  STATUS_OK; STATUS_BAD_ARG (no source); the status of the hook or of eq_add.
 */
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

/**
 * @brief   Applies one event taken from the list.
 * @param   k      In/out: the kernel.
 * @param   event  The event.
 * @return  STATUS_OK; STATUS_BAD_ARG for a bad index; the status of expand_event for other kinds.
 */
static Status_t apply_event(SimKernel_t *k, const Event_t *event)
{
    Status_t status = STATUS_OK;

    switch (event->kind)
    {
        case EV_GATE:
            if (event->index < k->nl->gate_count)
            {
                /* the promise is kept: the gate has nothing pending any more */
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
            /* every other kind belongs to the stimulus: the kernel does not know what a clock is */
            status = expand_event(k, event);
            break;
    }
    return status;
}

/**
 * @brief   Takes every event of time now from the list and applies it.
 * @param   k               In/out: the kernel.
 * @param   now             The time of this step, ns.
 * @param   events_applied  In/out: the count of events taken from the list.
 * @return  STATUS_OK, or the status of the failing call.
 */
static Status_t apply_events_of_now(SimKernel_t *k, uint32_t now, uint32_t *events_applied)
{
    Status_t status = STATUS_OK;
    bool running = true;

    while ((status == STATUS_OK) && running)
    {
        uint16_t row = 0U;

        if ((eq_find_earliest(k->queue, &row) != STATUS_OK) || (k->queue->rows[row].time != now))
        {
            /* the list is empty or the next event belongs to a later step */
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

/**
 * @brief   Tells whether a gate reads a net that changed in this step.
 * @param   k     The kernel.
 * @param   gate  The gate.
 * @return  true if one of its inputs has its changed flag set.
 */
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

/**
 * @brief   Chooses the delay of an output change (worker of sk_gate_delay).
 * @param   gate       The gate.
 * @param   new_value  The new output value (below LV_COUNT).
 * @return  rise for a change to 1, fall for a change to 0, the shorter of the two for a change to X.
 */
static uint16_t gate_delay(const Gate_t *gate, LogicValue_t new_value)
{
    uint16_t delay = gate->rise;

    if (new_value == LV_0)
    {
        delay = gate->fall;
    }
    else if (new_value == LV_X)
    {
        /* X may be a 0 or a 1, so it arrives with the shorter delay */
        delay = (gate->fall < gate->rise) ? gate->fall : gate->rise;
    }
    else
    {
        /* LV_1: the rise delay */
    }
    return delay;
}

/**
 * @brief   Removes the promised output change of a gate from the event list (rule 2: cancel).
 * @param   k     In/out: the kernel.
 * @param   gate  The gate row (it has a pending event).
 * @return  STATUS_OK; STATUS_NOT_FOUND if its event is missing (the list was changed from outside).
 */
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

/**
 * @brief   Re-calculates one gate and applies the three rules of inertial delay.
 * @details Rule 1: the same value is already promised -> keep it. Rule 2: a different value
 *          is promised -> cancel that event. Rule 3: the new value differs from the output
 *          net -> schedule it at now + the delay of that change (rise or fall). So an output
 *          change that the inputs take back within the delay never happens.
 * @param   k     In/out: the kernel.
 * @param   gate  The gate row.
 * @param   now   The time of this step, ns.
 * @return  STATUS_OK; the status of lg_eval, cancel_pending or eq_add; STATUS_TIME_OVERFLOW.
 */
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
    /* rule 1: when the same value is already promised, the promise is kept and nothing is done */
    if ((status == STATUS_OK) && (!g->has_pending || (g->pending != value)))
    {
        /* rule 2: a different value is promised, so that event is cancelled */
        if (g->has_pending)
        {
            status = cancel_pending(k, gate);
        }
        /* rule 3: a value that differs from the output net is scheduled after the delay of that change */
        if ((status == STATUS_OK) && (value != k->nl->nets[g->out].value))
        {
            const uint32_t delay = (uint32_t)gate_delay(g, value);

            /* MAX_TIME_NS + MAX_DELAY fits a uint32_t, but a time past MAX_TIME_NS is not allowed */
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

/**
 * @brief   Re-calculates every gate on the first step, later only the gates with a changed input.
 * @param   k      In/out: the kernel.
 * @param   now    The time of this step, ns.
 * @param   first  true in the first step of the run.
 * @return  STATUS_OK, or the status of the first failing gate.
 */
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

/**
 * @brief   Tells the listener about every net whose value differs from the start of the step
 *          (a net may go 0 -> 1 -> 0 within one step: no change), then clears the step's scratch data.
 * @param   k    In/out: the kernel.
 * @param   now  The time of this step, ns.
 */
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

        /* power-up state: every net has its start value (X; 1 for VCC, 0 for GND) and no gate has a pending
           change; the first step calculates every gate, so a gate whose inputs are known leaves X after its
           delay */
        for (i = 0U; i < (uint32_t)nl->net_count; i++)
        {
            nl->nets[i].value = nl->nets[i].init_value;
        }
        for (i = 0U; i < (uint32_t)nl->gate_count; i++)
        {
            nl->gates[i].has_pending = false;
        }
        /* a run that failed in the middle of a step leaves its flags set: the next run must not see them */
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
        k->listener.on_end = NULL;
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
        /* the state of this run */
        StopReason_t reason = STOP_QUEUE_EMPTY;
        uint32_t events_applied = 0U;
        uint32_t last_time = 0U;
        bool first = true;
        bool running = true;

        /* one time step per pass; the loop ends through its condition (no break) */
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

            /* END_TIME: the run covered everything up to the end time */
            result.reason = reason;
            result.stop_time = (reason == STOP_END_TIME) ? k->end_time : last_time;
            result.events_applied = events_applied;
            /* the listener learns where the run stopped (a waveform must end at the stop time) */
            if (k->has_listener && (k->listener.on_end != NULL))
            {
                k->listener.on_end(k->listener.ctx, &result);
            }
            *out_result = result;
        }
    }
    return status;
}

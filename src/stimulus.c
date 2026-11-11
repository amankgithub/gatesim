/**
 * @file    stimulus.c
 * @brief   Reads a .stim file (set, clock and end lines), adds its first events to the event list and
 *          expands clock events while the kernel runs.
 * @details Same pattern as the netlist reader: the first word picks the kind of line, every word of a
 *          line is checked before anything is stored, and a rejected line names its offending word in a
 *          Diag_t. A net has one driver: set lines or one clock. st_parse_line and st_find_clock are
 *          pointer-checking wrappers around the static workers that the rest of this file uses. No stdio
 *          here: the file is read through text_utils.
 */
#include "stimulus.h"

#include <stddef.h>
#include <string.h>

#include "text_utils.h"

/** The first word of each kind of line. */
#define SET_KEYWORD         "set"
#define CLOCK_KEYWORD       "clock"
#define END_KEYWORD         "end"
/** First character of a time word ("@20"). */
#define TIME_MARK           '@'

/** Word positions in "set NET VALUE @TIME". */
#define SET_W_NET           1U
#define SET_W_VALUE         2U
#define SET_W_TIME          3U
#define SET_WORDS           4U

/** Word positions in "clock NET period P high H start S". */
#define CLOCK_W_NET         1U
#define CLOCK_W_PERIOD_KEY  2U
#define CLOCK_W_PERIOD      3U
#define CLOCK_W_HIGH_KEY    4U
#define CLOCK_W_HIGH        5U
#define CLOCK_W_START_KEY   6U
#define CLOCK_W_START       7U
#define CLOCK_WORDS         8U
#define PERIOD_WORD         "period"
#define HIGH_WORD           "high"
#define START_WORD          "start"
/** Shortest high time of a clock, ns. */
#define MIN_CLOCK_HIGH      1U

/** Word positions in "end TIME". */
#define END_W_TIME          1U
#define END_WORDS           2U

/**
 * @brief   Fills the text of an error message: the status text and the offending word.
 * @param   diag    Out: the message (file and line are filled by st_load).
 * @param   status  The error.
 * @param   word    The offending word, or NULL for none.
 */
static void set_stim_diag(Diag_t *diag, Status_t status, const char *word)
{
    if (tu_set_diag(diag, NULL, 0U, status_text(status), word) != STATUS_OK)
    {
        /* a word too long for the message: the status text alone always fits */
        (void)tu_set_diag(diag, NULL, 0U, status_text(status), NULL);
    }
}

/**
 * @brief   Empties the stimulus (worker of st_init and st_load).
 * @param   st  Out: the stimulus.
 */
static void clear_stimulus(Stimulus_t *st)
{
    st->set_count = 0U;
    st->clock_count = 0U;
    st->has_end = false;
    st->end_time = 0U;
}

/**
 * @brief   Reads a time word "@T" with T in 0..MAX_TIME_NS (a time the kernel can reach).
 * @param   word      The word.
 * @param   out_time  Out: the time in ns, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_SYNTAX if the word does not start with '@';
 *          STATUS_BAD_NUMBER if T is not a number in range.
 */
static Status_t parse_time_word(const char *word, uint32_t *out_time)
{
    Status_t status = STATUS_SYNTAX;

    if (word[0] == TIME_MARK)
    {
        status = tu_parse_u32(&word[1], 0U, MAX_TIME_NS, out_time);
    }
    return status;
}

/**
 * @brief   Finds the clock line of a net (worker of st_find_clock).
 * @param   st         The stimulus.
 * @param   net        The net.
 * @param   out_clock  Out: the clock index, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NOT_FOUND.
 */
static Status_t find_clock(const Stimulus_t *st, uint16_t net, uint8_t *out_clock)
{
    Status_t status = STATUS_NOT_FOUND;
    uint32_t i = 0U;

    while ((status != STATUS_OK) && (i < (uint32_t)st->clock_count))
    {
        if (st->clocks[i].net == net)
        {
            *out_clock = (uint8_t)i;
            status = STATUS_OK;
        }
        i++;
    }
    return status;
}

/**
 * @brief   Tells whether a set line already drives a net.
 * @param   st   The stimulus.
 * @param   net  The net.
 * @return  true if some set line names the net.
 */
static bool has_set_line(const Stimulus_t *st, uint16_t net)
{
    bool found = false;
    uint32_t i = 0U;

    while (!found && (i < (uint32_t)st->set_count))
    {
        found = (st->sets[i].net == net);
        i++;
    }
    return found;
}

/**
 * @brief   Finds a net that a stimulus line may drive: it exists and is an input.
 * @param   nl       The netlist.
 * @param   name     The net name.
 * @param   out_net  Out: the net index, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NOT_FOUND; STATUS_NOT_INPUT.
 */
static Status_t find_input_net(const Netlist_t *nl, const char *name, uint16_t *out_net)
{
    uint16_t net = 0U;
    Status_t status = nl_find_net(nl, name, &net);

    if ((status == STATUS_OK) && (nl->nets[net].driver != DRV_INPUT))
    {
        /* only an input may be driven by the stimulus: a gate output already has a driver */
        status = STATUS_NOT_INPUT;
    }
    if (status == STATUS_OK)
    {
        *out_net = net;
    }
    return status;
}

/**
 * @brief   Handles "set NET VALUE @TIME": every word checked left to right, then one set line stored.
 * @param   st            In/out: the stimulus.
 * @param   nl            The netlist.
 * @param   words         Every word of the line (word 0 is the keyword).
 * @param   count         Number of words.
 * @param   out_bad_word  Out: on an error, the position of the offending word; unchanged on STATUS_OK.
 * @return  STATUS_OK; STATUS_SYNTAX; STATUS_NOT_FOUND; STATUS_NOT_INPUT; STATUS_TWO_DRIVERS;
 *          STATUS_BAD_NUMBER; STATUS_TABLE_FULL.
 */
static Status_t parse_set_line(Stimulus_t *st, const Netlist_t *nl, char *const words[], uint32_t count,
                               uint32_t *out_bad_word)
{
    Status_t status = STATUS_SYNTAX;
    uint32_t bad_word = 0U;

    if (count == SET_WORDS)
    {
        SetLine_t set = { 0U, 0U, LV_0 };

        /* only written by find_clock; the set line needs to know whether a clock exists, not which one */
        uint8_t clock = 0U;

        bad_word = SET_W_NET;
        status = find_input_net(nl, words[SET_W_NET], &set.net);
        if ((status == STATUS_OK) && (find_clock(st, set.net, &clock) != STATUS_NOT_FOUND))
        {
            /* a clock line already drives this net */
            status = STATUS_TWO_DRIVERS;
        }
        if (status == STATUS_OK)
        {
            bad_word = SET_W_VALUE;
            status = lg_value_from_text(words[SET_W_VALUE], &set.value);
        }
        if (status == STATUS_OK)
        {
            bad_word = SET_W_TIME;
            status = parse_time_word(words[SET_W_TIME], &set.time);
        }
        if ((status == STATUS_OK) && ((uint32_t)st->set_count >= MAX_SET_LINES))
        {
            /* the table is full: the keyword names the line */
            bad_word = 0U;
            status = STATUS_TABLE_FULL;
        }
        if (status == STATUS_OK)
        {
            st->sets[st->set_count] = set;
            st->set_count++;
        }
    }
    if (status != STATUS_OK)
    {
        *out_bad_word = bad_word;
    }
    return status;
}

/**
 * @brief   Handles "clock NET period P high H start S": every word checked left to right, then one
 *          clock stored.
 * @param   st            In/out: the stimulus.
 * @param   nl            The netlist.
 * @param   words         Every word of the line (word 0 is the keyword).
 * @param   count         Number of words.
 * @param   out_bad_word  Out: on an error, the position of the offending word; unchanged on STATUS_OK.
 * @return  STATUS_OK; STATUS_SYNTAX; STATUS_NOT_FOUND; STATUS_NOT_INPUT; STATUS_TWO_DRIVERS;
 *          STATUS_BAD_NUMBER; STATUS_TABLE_FULL.
 */
static Status_t parse_clock_line(Stimulus_t *st, const Netlist_t *nl, char *const words[], uint32_t count,
                                 uint32_t *out_bad_word)
{
    Status_t status = STATUS_SYNTAX;
    uint32_t bad_word = 0U;

    if (count == CLOCK_WORDS)
    {
        ClockDef_t clock = { 0U, 0U, 0U, 0U };
        uint8_t other = 0U;

        bad_word = CLOCK_W_NET;
        status = find_input_net(nl, words[CLOCK_W_NET], &clock.net);
        if ((status == STATUS_OK) &&
            (has_set_line(st, clock.net) || (find_clock(st, clock.net, &other) != STATUS_NOT_FOUND)))
        {
            /* set lines or another clock already drive this net */
            status = STATUS_TWO_DRIVERS;
        }
        if ((status == STATUS_OK) && (strcmp(words[CLOCK_W_PERIOD_KEY], PERIOD_WORD) != 0))
        {
            bad_word = CLOCK_W_PERIOD_KEY;
            status = STATUS_SYNTAX;
        }
        if (status == STATUS_OK)
        {
            bad_word = CLOCK_W_PERIOD;
            status = tu_parse_u32(words[CLOCK_W_PERIOD], MIN_CLOCK_PERIOD, MAX_TIME_NS, &clock.period);
        }
        if ((status == STATUS_OK) && (strcmp(words[CLOCK_W_HIGH_KEY], HIGH_WORD) != 0))
        {
            bad_word = CLOCK_W_HIGH_KEY;
            status = STATUS_SYNTAX;
        }
        if (status == STATUS_OK)
        {
            /* high and low both last at least 1 ns */
            bad_word = CLOCK_W_HIGH;
            status = tu_parse_u32(words[CLOCK_W_HIGH], MIN_CLOCK_HIGH, clock.period - 1U, &clock.high);
        }
        if ((status == STATUS_OK) && (strcmp(words[CLOCK_W_START_KEY], START_WORD) != 0))
        {
            bad_word = CLOCK_W_START_KEY;
            status = STATUS_SYNTAX;
        }
        if (status == STATUS_OK)
        {
            /* start and period are each at most MAX_TIME_NS, so time + period never overflows */
            bad_word = CLOCK_W_START;
            status = tu_parse_u32(words[CLOCK_W_START], 0U, MAX_TIME_NS, &clock.start);
        }
        if ((status == STATUS_OK) && ((uint32_t)st->clock_count >= MAX_CLOCKS))
        {
            /* the table is full: the keyword names the line */
            bad_word = 0U;
            status = STATUS_TABLE_FULL;
        }
        if (status == STATUS_OK)
        {
            st->clocks[st->clock_count] = clock;
            st->clock_count++;
        }
    }
    if (status != STATUS_OK)
    {
        *out_bad_word = bad_word;
    }
    return status;
}

/**
 * @brief   Handles "end TIME" (once per file).
 * @param   st            In/out: the stimulus.
 * @param   words         Every word of the line.
 * @param   count         Number of words.
 * @param   out_bad_word  Out: on an error, the position of the offending word; unchanged on STATUS_OK.
 * @return  STATUS_OK; STATUS_SYNTAX; STATUS_BAD_NUMBER; STATUS_DUPLICATE (a second end line).
 */
static Status_t parse_end_line(Stimulus_t *st, char *const words[], uint32_t count, uint32_t *out_bad_word)
{
    Status_t status = STATUS_SYNTAX;
    uint32_t bad_word = 0U;

    if (count == END_WORDS)
    {
        uint32_t end_time = 0U;

        bad_word = END_W_TIME;
        status = tu_parse_u32(words[END_W_TIME], 0U, MAX_TIME_NS, &end_time);
        if ((status == STATUS_OK) && st->has_end)
        {
            /* the second "end" itself is the offending word */
            bad_word = 0U;
            status = STATUS_DUPLICATE;
        }
        if (status == STATUS_OK)
        {
            st->has_end = true;
            st->end_time = end_time;
        }
    }
    if (status != STATUS_OK)
    {
        *out_bad_word = bad_word;
    }
    return status;
}

/**
 * @brief   Handles one line (worker of st_parse_line and st_load).
 * @param   st        In/out: the stimulus.
 * @param   nl        The netlist.
 * @param   line      The line, changed in place.
 * @param   out_diag  Out: on an error, the status text and the offending word.
 * @return  See st_parse_line.
 */
static Status_t parse_stim_line(Stimulus_t *st, const Netlist_t *nl, char *line, Diag_t *out_diag)
{
    char *words[MAX_WORDS];
    uint32_t count = 0U;
    uint32_t bad_word = 0U;
    Status_t status = tu_cut_comment(line);

    if (status == STATUS_OK)
    {
        status = tu_split_words(line, words, MAX_WORDS, &count);
    }
    /* an empty or comment-only line is OK and adds nothing */
    if ((status == STATUS_OK) && (count > 0U))
    {
        if (strcmp(words[0], SET_KEYWORD) == 0)
        {
            status = parse_set_line(st, nl, words, count, &bad_word);
        }
        else if (strcmp(words[0], CLOCK_KEYWORD) == 0)
        {
            status = parse_clock_line(st, nl, words, count, &bad_word);
        }
        else if (strcmp(words[0], END_KEYWORD) == 0)
        {
            status = parse_end_line(st, words, count, &bad_word);
        }
        else
        {
            status = STATUS_UNKNOWN_KEYWORD;
        }
        if (status != STATUS_OK)
        {
            set_stim_diag(out_diag, status, words[bad_word]);
        }
    }
    else if (status != STATUS_OK)
    {
        /* too many words: the line was not split, so there is no word to name */
        set_stim_diag(out_diag, status, NULL);
    }
    else
    {
        /* an empty or comment-only line */
    }
    return status;
}

Status_t st_parse_line(Stimulus_t *st, const Netlist_t *nl, char *line, Diag_t *out_diag)
{
    Status_t status = STATUS_NULL_ARG;

    if ((st != NULL) && (nl != NULL) && (line != NULL) && (out_diag != NULL))
    {
        status = parse_stim_line(st, nl, line, out_diag);
    }
    return status;
}

Status_t st_init(Stimulus_t *st)
{
    Status_t status = STATUS_NULL_ARG;

    if (st != NULL)
    {
        clear_stimulus(st);
        status = STATUS_OK;
    }
    return status;
}

Status_t st_load(Stimulus_t *st, const Netlist_t *nl, const char *path, Diag_t *out_diag)
{
    Status_t status = STATUS_NULL_ARG;

    if ((st != NULL) && (nl != NULL) && (path != NULL) && (out_diag != NULL))
    {
        LineReader_t reader;
        Diag_t diag;
        uint32_t error_line = 0U;

        clear_stimulus(st);
        status = tu_open(&reader, path);
        if (status != STATUS_OK)
        {
            set_stim_diag(&diag, status, NULL);
        }
        else
        {
            char line[MAX_LINE_LEN];
            bool running = true;
            Status_t close_status;

            /* one line per pass; the loop leaves through its condition (no break) */
            while ((status == STATUS_OK) && running)
            {
                const Status_t read_status = tu_read_line(&reader, line, MAX_LINE_LEN);

                if (read_status == STATUS_END_OF_FILE)
                {
                    running = false;
                }
                else if (read_status != STATUS_OK)
                {
                    status = read_status;
                    set_stim_diag(&diag, status, NULL);
                }
                else
                {
                    status = parse_stim_line(st, nl, line, &diag);
                }
            }
            if (status != STATUS_OK)
            {
                error_line = reader.line_no;
            }
            /* closed on every path; a close error matters only if the load itself went well */
            close_status = tu_close(&reader);
            if (status == STATUS_OK)
            {
                status = close_status;
                if (status != STATUS_OK)
                {
                    set_stim_diag(&diag, status, NULL);
                }
            }
        }
        /* a whole-file check (not tied to a line): the end line is required */
        if ((status == STATUS_OK) && !st->has_end)
        {
            status = STATUS_NO_END;
            set_stim_diag(&diag, status, NULL);
        }
        if (status != STATUS_OK)
        {
            /* a path too long for the message leaves the file part empty: the text is still shown */
            (void)tu_copy_text(diag.file, MAX_PATH_LEN, path);
            diag.line = error_line;
            *out_diag = diag;
        }
    }
    return status;
}

Status_t st_seed(const Stimulus_t *st, EventQueue_t *queue)
{
    Status_t status = STATUS_OK;

    if ((st == NULL) || (queue == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (!st->has_end)
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        uint32_t needed = 0U;
        uint32_t i;

        /* check-only pass first, so a list without room is left as it was; each clock adds one event at 0 */
        for (i = 0U; i < (uint32_t)st->set_count; i++)
        {
            if (st->sets[i].time <= st->end_time)
            {
                needed++;
            }
        }
        needed += (uint32_t)st->clock_count;
        if (((uint32_t)queue->count + needed) > MAX_EVENTS)
        {
            status = STATUS_QUEUE_FULL;
        }
        i = 0U;
        while ((i < (uint32_t)st->set_count) && (status == STATUS_OK))
        {
            const SetLine_t *set = &st->sets[i];

            /* a change after the end time could never be applied */
            if (set->time <= st->end_time)
            {
                status = eq_add(queue, set->time, EV_SET, set->net, set->value);
            }
            i++;
        }
        i = 0U;
        while ((i < (uint32_t)st->clock_count) && (status == STATUS_OK))
        {
            /* a clock that starts later is driven low from 0 until its first rise */
            const LogicValue_t first = (st->clocks[i].start > 0U) ? LV_0 : LV_1;

            status = eq_add(queue, 0U, EV_CLOCK, (uint16_t)i, first);
            i++;
        }
    }
    return status;
}

Status_t st_find_clock(const Stimulus_t *st, uint16_t net, uint8_t *out_clock)
{
    Status_t status = STATUS_NULL_ARG;

    if ((st != NULL) && (out_clock != NULL))
    {
        status = find_clock(st, net, out_clock);
    }
    return status;
}

Status_t st_expand(const void *source, const Event_t *event, uint32_t end_time, SkExpansion_t *out)
{
    Status_t status = STATUS_OK;

    if ((source == NULL) || (event == NULL) || (out == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        /* the kernel hands back what main.c gave sk_set_source: always a Stimulus_t (MISRA 11.5) */
        /* cppcheck-suppress misra-c2012-11.5 ; expansion hook source, MISRA deviation */
        const Stimulus_t *const st = (const Stimulus_t *)source;

        /* gate and set events are applied by the kernel itself; only clock events come here */
        if ((event->kind != EV_CLOCK) || ((uint32_t)event->index >= (uint32_t)st->clock_count) ||
            ((event->value != LV_0) && (event->value != LV_1)))
        {
            status = STATUS_BAD_ARG;
        }
        else
        {
            const ClockDef_t *const clock = &st->clocks[event->index];
            SkExpansion_t x;

            x.net = clock->net;
            x.value = event->value;
            if (event->value == LV_1)
            {
                /* after a rise: the fall */
                x.next_time = event->time + clock->high;
                x.next_value = LV_0;
            }
            else if (event->time < clock->start)
            {
                /* the low at 0 of a clock that starts later: its first rise */
                x.next_time = clock->start;
                x.next_value = LV_1;
            }
            else
            {
                /* after a fall: the next rise, one period after the last one */
                x.next_time = event->time + (clock->period - clock->high);
                x.next_value = LV_1;
            }
            x.has_next = (x.next_time <= end_time);
            *out = x;
        }
    }
    return status;
}

#include "stimulus.h"

#include <stddef.h>
#include <string.h>

#include "text_utils.h"

#define SET_KEYWORD         "set"
#define END_KEYWORD         "end"
#define TIME_MARK           '@'

#define SET_W_NET           1U
#define SET_W_VALUE         2U
#define SET_W_TIME          3U
#define SET_WORDS           4U

#define END_W_TIME          1U
#define END_WORDS           2U

/* Fills the text of an error message: the status text and the offending word. */
static void set_stim_diag(Diag_t *diag, Status_t status, const char *word)
{
    if (tu_set_diag(diag, NULL, 0U, status_text(status), word) != STATUS_OK)
    {
        (void)tu_set_diag(diag, NULL, 0U, status_text(status), NULL);
    }
}

/* Empties the stimulus (worker of st_init and st_load). */
static void clear_stimulus(Stimulus_t *st)
{
    st->set_count = 0U;
    st->has_end = false;
    st->end_time = 0U;
}

/* Reads a time word "@T" with T in 0..MAX_TIME_NS (a time the kernel can reach). */
static Status_t parse_time_word(const char *word, uint32_t *out_time)
{
    Status_t status = STATUS_SYNTAX;

    if (word[0] == TIME_MARK)
    {
        status = tu_parse_u32(&word[1], 0U, MAX_TIME_NS, out_time);
    }
    return status;
}

/* Handles "set NET VALUE */
static Status_t parse_set_line(Stimulus_t *st, const Netlist_t *nl, char *const words[], uint32_t count,
                               uint32_t *out_bad_word)
{
    Status_t status = STATUS_SYNTAX;
    uint32_t bad_word = 0U;

    if (count == SET_WORDS)
    {
        SetLine_t set = { 0U, 0U, LV_0 };

        bad_word = SET_W_NET;
        status = nl_find_net(nl, words[SET_W_NET], &set.net);
        if ((status == STATUS_OK) && (nl->nets[set.net].driver != DRV_INPUT))
        {
            status = STATUS_NOT_INPUT;
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

/* Handles "end TIME" (once per file). */
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

Status_t st_parse_line(Stimulus_t *st, const Netlist_t *nl, char *line, Diag_t *out_diag)
{
    Status_t status = STATUS_NULL_ARG;

    if ((st != NULL) && (nl != NULL) && (line != NULL) && (out_diag != NULL))
    {
        char *words[MAX_WORDS];
        uint32_t count = 0U;
        uint32_t bad_word = 0U;

        status = tu_cut_comment(line);
        if (status == STATUS_OK)
        {
            status = tu_split_words(line, words, MAX_WORDS, &count);
        }
        if ((status == STATUS_OK) && (count > 0U))
        {
            if (strcmp(words[0], SET_KEYWORD) == 0)
            {
                status = parse_set_line(st, nl, words, count, &bad_word);
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
            set_stim_diag(out_diag, status, NULL);
        }
        else
        {
            /* an empty or comment-only line */
        }
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
                    status = st_parse_line(st, nl, line, &diag);
                }
            }
            if (status != STATUS_OK)
            {
                error_line = reader.line_no;
            }
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
        if ((status == STATUS_OK) && !st->has_end)
        {
            status = STATUS_NO_END;
            set_stim_diag(&diag, status, NULL);
        }
        if (status != STATUS_OK)
        {
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

        for (i = 0U; i < (uint32_t)st->set_count; i++)
        {
            if (st->sets[i].time <= st->end_time)
            {
                needed++;
            }
        }
        if (((uint32_t)queue->count + needed) > MAX_EVENTS)
        {
            status = STATUS_QUEUE_FULL;
        }
        i = 0U;
        while ((i < (uint32_t)st->set_count) && (status == STATUS_OK))
        {
            const SetLine_t *set = &st->sets[i];

            if (set->time <= st->end_time)
            {
                status = eq_add(queue, set->time, EV_SET, set->net, set->value);
            }
            i++;
        }
    }
    return status;
}

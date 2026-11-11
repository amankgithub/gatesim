/**
 * @file    stimulus.h
 * @brief   The stimulus of one run, read from a .stim file: input changes (`set`), clocks (`clock`) and
 *          the end time (`end`).
 * @details Each `set` line becomes one SetLine_t and each `clock` line one ClockDef_t; the `end` line is
 *          required exactly once. A net has one driver: set lines or one clock. st_seed turns the lines
 *          into the first events of the run; a clock keeps only one event in the list, and st_expand (the
 *          kernel's expansion hook) gives the next one when that event is taken. Every line is checked
 *          completely before anything is stored. To try every input combination, each input bit gets its
 *          own clock, each period twice the one before (the 4-bit adder demo drives its 8 inputs so).
 */
#ifndef STIMULUS_H
#define STIMULUS_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "event_queue.h"
#include "logic.h"
#include "netlist.h"
#include "sim_kernel.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One `set` line: an input net takes a value at a time. */
typedef struct
{
    uint32_t     time;    /**< when, ns (0..MAX_TIME_NS) */
    uint16_t     net;     /**< the input net (index into the netlist) */
    LogicValue_t value;   /**< LV_0, LV_1 or LV_X */
} SetLine_t;

/** @brief One `clock` line: an input net that is 0 until start, then rises every period, high for `high` ns. */
typedef struct
{
    uint16_t net;      /**< the input net */
    uint32_t period;   /**< ns, MIN_CLOCK_PERIOD..MAX_TIME_NS */
    uint32_t high;     /**< ns the clock is 1 in each period, 1..period-1 */
    uint32_t start;    /**< the first rising edge, ns (0..MAX_TIME_NS); before it the clock is 0 */
} ClockDef_t;

/** @brief The whole stimulus (owned by the caller, passed by pointer; about 2 KB). */
typedef struct
{
    SetLine_t  sets[MAX_SET_LINES];   /**< set lines in file order */
    uint16_t   set_count;             /**< used rows of sets[] */
    ClockDef_t clocks[MAX_CLOCKS];    /**< clock lines in file order */
    uint8_t    clock_count;           /**< used rows of clocks[] */
    bool       has_end;               /**< an end line was read */
    uint32_t   end_time;              /**< the end time, ns (valid when has_end) */
} Stimulus_t;

/**
 * @brief   Empties the stimulus: no set lines, no clocks, no end line.
 * @param   st  Out: the stimulus.
 * @return  STATUS_OK or STATUS_NULL_ARG.
 */
Status_t st_init(Stimulus_t *st);

/**
 * @brief   Handles one line of a .stim file: `set NET VALUE @TIME`, `clock NET period P high H start S`
 *          or `end TIME`.
 * @details Cuts the comment, splits the words; an empty line is OK; the first word picks the kind of
 *          line. A bad line adds nothing.
 *          set: 4 words; the net must exist (NOT_FOUND), be an input (NOT_INPUT) and have no clock line
 *          (TWO_DRIVERS); value `0`, `1` or `x`; time `@0`..`@1000000000`; at most MAX_SET_LINES.
 *          clock: 8 words with `period`, `high`, `start` at words 2, 4, 6 (else SYNTAX); an input net
 *          without set lines or another clock (TWO_DRIVERS); MIN_CLOCK_PERIOD <= P <= MAX_TIME_NS,
 *          1 <= H < P, S <= MAX_TIME_NS (else BAD_NUMBER); at most MAX_CLOCKS.
 *          end: 2 words, a time 0..MAX_TIME_NS, once only (DUPLICATE).
 *          On an error the message text is the status text and the offending word: the net name
 *          (NOT_FOUND, NOT_INPUT, TWO_DRIVERS), the value, time or number word (BAD_NUMBER, SYNTAX for a
 *          time without '@'), the word that should be `period`, `high` or `start`, word 0
 *          (UNKNOWN_KEYWORD, wrong number of words, a second end line, TABLE_FULL); no word for
 *          TOO_MANY_WORDS.
 * @param   st        In/out: the stimulus.
 * @param   nl        The loaded netlist (names are looked up in it).
 * @param   line      The line, changed in place (comment cut, '\0' after each word).
 * @param   out_diag  Out: on an error, the text "net is not an input 'Q'" (file "" and
 *                    line 0: st_load fills them); unchanged on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_TOO_MANY_WORDS; STATUS_UNKNOWN_KEYWORD;
 *          STATUS_SYNTAX (wrong word count, time without '@', a missing `period`, `high` or `start`);
 *          STATUS_NOT_FOUND; STATUS_NOT_INPUT; STATUS_TWO_DRIVERS (set lines and a clock line, or two
 *          clock lines, on one net); STATUS_BAD_NUMBER (value, time, period, high, start, end time);
 *          STATUS_DUPLICATE (a second end line); STATUS_TABLE_FULL (MAX_SET_LINES, MAX_CLOCKS).
 */
Status_t st_parse_line(Stimulus_t *st, const Netlist_t *nl, char *line, Diag_t *out_diag);

/**
 * @brief   Reads a whole .stim file line by line, then checks that it had an end line.
 * @details Starts with an empty stimulus. The first error stops the load; the file is
 *          closed on every path. On an error the stimulus must not be used.
 * @param   st        Out: the stimulus.
 * @param   nl        The loaded netlist.
 * @param   path      Path of the .stim file.
 * @param   out_diag  Out: on an error, the message: file = path, the number of the line that
 *                    failed (0 if the file could not be opened or the end line is missing) and
 *                    the text; unchanged on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_FILE_OPEN; STATUS_FILE_READ;
 *          STATUS_LINE_TOO_LONG; every status of st_parse_line; STATUS_NO_END.
 */
Status_t st_load(Stimulus_t *st, const Netlist_t *nl, const char *path, Diag_t *out_diag);

/**
 * @brief   Adds the first events of a run: one EV_SET per set line, in file order, then one EV_CLOCK per clock
 *          at time 0 (value 0 if the clock starts later, else its first rise).
 * @details A set line after the end time is not added (it could never be reached).
 * @param   st     The loaded stimulus (with its end line).
 * @param   queue  In/out: the event list.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG (no end line);
 *          STATUS_QUEUE_FULL if the events do not fit (then nothing is added).
 */
Status_t st_seed(const Stimulus_t *st, EventQueue_t *queue);

/**
 * @brief   Finds the clock line of a net (linear search of clocks[]).
 * @param   st         The stimulus.
 * @param   net        The net.
 * @param   out_clock  Out: the clock index, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_NOT_FOUND if the net has no clock line.
 */
Status_t st_find_clock(const Stimulus_t *st, uint16_t net, uint8_t *out_clock);

/**
 * @brief   The kernel's expansion hook (SkExpandFn_t) for the stimulus: what an EV_CLOCK event does.
 * @details The clock's net takes the event's value. The next edge: after a rise, the fall at
 *          time + high; after the 0 at time 0 of a clock that starts later, the first rise at start;
 *          after a fall, the rise at time + period - high. No follow-up after end_time.
 * @param   source    The Stimulus_t (given to sk_set_source).
 * @param   event     The event taken from the list.
 * @param   end_time  The end time of the run, ns.
 * @param   out       Out: the net, its value and the follow-up, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG (another kind, an unknown clock, a clock
 *          value that is not 0 or 1).
 */
Status_t st_expand(const void *source, const Event_t *event, uint32_t end_time, SkExpansion_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STIMULUS_H */

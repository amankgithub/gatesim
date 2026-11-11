/**
 * @file    netlist.h
 * @brief   The circuit as two tables, nets and gates, filled from the lines of a .net file and checked as a whole.
 * @details Rows refer to each other by index (a gate stores the row numbers of its
 *          nets), never by pointer. Every function checks everything first and writes
 *          only on success, so a bad line adds nothing to the tables. A block
 *          (`subckt` ... `ends`) stores its gate lines; each `inst` line copies them into the
 *          gate table with renamed nets. An `include` line reads a library file of blocks.
 *          VCC and GND are constant nets (1 and 0). After the last line the whole circuit is
 *          checked: every net that is read must have a driver.
 */
#ifndef NETLIST_H
#define NETLIST_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "logic.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What drives a net (after loading, every net that is read should have a driver). */
typedef enum
{
    DRV_NONE = 0,   /**< nothing yet */
    DRV_INPUT,      /**< declared on an input line, set by the stimulus */
    DRV_GATE,       /**< the output of one gate */
    DRV_CONST       /**< VCC (always 1) or GND (always 0) */
} DriverKind_t;

/** @brief One wire of the circuit. */
typedef struct
{
    char         name[MAX_NAME_LEN];   /**< name from the file ("Q", "n0") */
    DriverKind_t driver;               /**< what drives it */
    bool         is_output;            /**< named on an output line */
    bool         is_read;              /**< some gate reads it */
    LogicValue_t init_value;           /**< value at the start of a run: X; 1 for VCC, 0 for GND */
    LogicValue_t value;                /**< current value during a run (written only by sim_kernel) */
} Net_t;

/** @brief One gate of the circuit. */
typedef struct
{
    char         name[MAX_NAME_LEN];     /**< gate name from the file */
    GateType_t   type;                   /**< one of the GateType_t values */
    uint8_t      in_count;               /**< 1..MAX_GATE_INPUTS (NOT, BUF: 1) */
    uint16_t     in[MAX_GATE_INPUTS];    /**< input net indices, in file order */
    uint16_t     out;                    /**< output net index */
    uint16_t     rise;                   /**< delay of a change to 1, MIN_DELAY..MAX_DELAY ns */
    uint16_t     fall;                   /**< delay of a change to 0, MIN_DELAY..MAX_DELAY ns */
    bool         has_pending;            /**< run state (written only by sim_kernel): an output change of
                                              this gate is in the event list */
    LogicValue_t pending;                /**< run state: the promised new output value, when has_pending */
} Gate_t;

/** @brief One gate line before its names are turned into net indices. */
typedef struct
{
    char       name[MAX_NAME_LEN];                  /**< gate name */
    GateType_t type;                                /**< gate type */
    uint16_t   rise;                                /**< delay to 1, ns */
    uint16_t   fall;                                /**< delay to 0, ns */
    char       out[MAX_NAME_LEN];                   /**< output net name */
    uint8_t    in_count;                            /**< number of inputs */
    char       in[MAX_GATE_INPUTS][MAX_NAME_LEN];   /**< input net names */
} GateSpec_t;

/** @brief A block: gate lines stored once (`subckt NAME P1 .. Pk [-> Q1 .. Qm]` ... `ends`), copied by `inst`. */
typedef struct
{
    char       name[MAX_NAME_LEN];                   /**< block name */
    uint8_t    port_count;                           /**< 1..MAX_BLOCK_PORTS */
    uint8_t    first_output_port;                    /**< ports before "->" are inputs (informational only) */
    char       ports[MAX_BLOCK_PORTS][MAX_NAME_LEN]; /**< port names, in line order */
    uint8_t    gate_count;                           /**< used rows of gates[] */
    GateSpec_t gates[MAX_BLOCK_GATES];               /**< the gate lines, names as written in the block */
} Block_t;

/** @brief What nl_parse_line remembers from one line to the next. */
typedef struct
{
    bool    in_block;                      /**< between `subckt` and `ends` */
    uint8_t block;                         /**< the open block (valid when in_block) */
    bool    include_requested;             /**< an include line was read; nl_load opens the file and clears it */
    char    include_path[MAX_PATH_LEN];    /**< the path of that line, relative to the file that has it */
} NlParseState_t;

/** @brief The whole circuit (owned by the caller, passed by pointer). */
typedef struct
{
    Net_t    nets[MAX_NETS];     /**< nets in order of first mention */
    uint16_t net_count;          /**< used rows of nets[] */
    Gate_t   gates[MAX_GATES];   /**< gates in file order (an instance's gates where its inst line is) */
    uint16_t gate_count;         /**< used rows of gates[] */
    Block_t  blocks[MAX_BLOCKS]; /**< blocks in file order */
    uint8_t  block_count;        /**< used rows of blocks[] */
    uint16_t inst_count;         /**< inst lines expanded */
    uint16_t input_count;        /**< nets declared on input lines */
    uint16_t output_count;       /**< nets declared on output lines */
    uint16_t unused[MAX_WARNINGS];   /**< nl_load check: gate outputs that nothing reads and that are no outputs */
    uint16_t warning_count;      /**< nl_load check: number of such nets (unused[] keeps the first MAX_WARNINGS) */
} Netlist_t;

/**
 * @brief   Empties both tables.
 * @param   nl  Out: the netlist.
 * @return  STATUS_OK or STATUS_NULL_ARG.
 */
Status_t nl_init(Netlist_t *nl);

/**
 * @brief   Finds a net by name (linear search).
 * @param   nl         The netlist.
 * @param   name       The name.
 * @param   out_index  Out: its row, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_NOT_FOUND.
 */
Status_t nl_find_net(const Netlist_t *nl, const char *name, uint16_t *out_index);

/**
 * @brief   Finds a gate by name (linear search).
 * @param   nl         The netlist.
 * @param   name       The gate name.
 * @param   out_index  Out: its row, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_NOT_FOUND.
 */
Status_t nl_find_gate(const Netlist_t *nl, const char *name, uint16_t *out_index);

/**
 * @brief   Finds a net by name, or adds it at the end. A new net has no driver and the
 *          value X, except VCC and GND: they are constants (DRV_CONST) with the value 1 and 0.
 * @param   nl         In/out: the netlist.
 * @param   name       The name (checked with tu_check_name).
 * @param   out_index  Out: the row of the found or new net, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_NAME; STATUS_TABLE_FULL (nothing added).
 */
Status_t nl_find_or_add_net(Netlist_t *nl, const char *name, uint16_t *out_index);

/**
 * @brief   Declares one input net (found or added) and counts it in input_count.
 * @param   nl    In/out: the netlist.
 * @param   name  The net name.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_NAME; STATUS_DUPLICATE if it is
 *          already an input; STATUS_TWO_DRIVERS if a gate drives it or it is VCC or GND;
 *          STATUS_TABLE_FULL (net table full). Nothing changes on an error.
 */
Status_t nl_add_input(Netlist_t *nl, const char *name);

/**
 * @brief   Reads a delay word: "#d" (the same delay both ways) or "#r,f" (rise r, fall f).
 * @param   word      The word.
 * @param   out_rise  Out: the delay of a change to 1, ns, written only on STATUS_OK.
 * @param   out_fall  Out: the delay of a change to 0, ns, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_SYNTAX if the word does not start with '#';
 *          STATUS_BAD_NUMBER if a delay is missing or not a number in MIN_DELAY..MAX_DELAY.
 */
Status_t nl_parse_delay(const char *word, uint16_t *out_rise, uint16_t *out_fall);

/**
 * @brief   Checks the words of a gate line and fills a GateSpec_t.
 * @details words: gate NAME TYPE #d OUT = IN1 [IN2 ...] (or #r,f), at most MAX_GATE_INPUTS inputs.
 * @param   words     The words of the line.
 * @param   count     Number of words.
 * @param   out_spec  Out: the gate line, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_SYNTAX (fewer than 7 words, word 0 not
 *          "gate", word 5 not "="); STATUS_BAD_NAME; STATUS_UNKNOWN_TYPE;
 *          STATUS_BAD_NUMBER (delay); STATUS_INPUT_COUNT (wrong number of inputs for the
 *          type, also more than MAX_GATE_INPUTS).
 */
Status_t nl_parse_gate_words(char *const words[], uint32_t count, GateSpec_t *out_spec);

/**
 * @brief   Adds one gate: its output and input nets are found or added, the inputs are marked as read.
 * @details A check-only pass comes first (gate name, table space, output driver, number
 *          of new nets), so on any error nothing is added.
 * @param   nl    In/out: the netlist.
 * @param   spec  The gate line (from nl_parse_gate_words).
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_DUPLICATE (a gate with this name exists);
 *          STATUS_TWO_DRIVERS (output already driven, or VCC or GND);
 *          STATUS_TABLE_FULL (MAX_GATES, or not enough room for the new nets).
 */
Status_t nl_add_gate(Netlist_t *nl, const GateSpec_t *spec);

/**
 * @brief   Finds a block by name (linear search).
 * @param   nl         The netlist.
 * @param   name       The block name.
 * @param   out_block  Out: its index, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_NOT_FOUND.
 */
Status_t nl_find_block(const Netlist_t *nl, const char *name, uint8_t *out_block);

/**
 * @brief   Handles one line of a .net file: input, output, gate, subckt, ends, inst or include.
 * @details Cuts the comment, splits the words; an empty line is OK. A bad line adds nothing
 *          (an inst line whose k-th gate fails keeps its first k-1 gates).
 *          Between `subckt` and `ends` (state->in_block) a gate line goes into the open block;
 *          input, output, subckt, inst and include lines are not allowed there (NESTING), and
 *          neither is `ends` outside a block. A block without gates is a SYNTAX error at its
 *          `ends`. `include PATH` (2 words) only records the path in the state
 *          (include_requested); nl_load opens the file.
 *          On an error the message text is the status text and the offending word:
 *          UNKNOWN_KEYWORD word 0; UNKNOWN_TYPE and INPUT_COUNT the type (word 2);
 *          BAD_NUMBER the delay word; BAD_NAME the bad name; DUPLICATE the repeated name
 *          (gate: its name, word 1; input/output: that name; subckt: the block or port name;
 *          inst: the renamed gate);
 *          TWO_DRIVERS the net that already has a driver (gate: its output; input: that name;
 *          inst: the connected or renamed net);
 *          SYNTAX word 0 (wrong number of words) or the word that broke the form ("=", "->",
 *          PORT=NET); NOT_FOUND the unknown block; BAD_PORT the port word (a missing port: its
 *          name); NESTING word 0; TABLE_FULL word 0; TOO_MANY_WORDS no word.
 * @param   nl        In/out: the netlist.
 * @param   state     In/out: the open block, kept from line to line (all false before the first line).
 * @param   line      The line, changed in place (comment cut, '\0' after each word).
 * @param   out_diag  Out: on an error, the text "unknown gate type 'NAN'" (file "" and
 *                    line 0: nl_load fills them); unchanged on STATUS_OK.
 * @return  STATUS_OK or the status of the first problem found: STATUS_NULL_ARG,
 *          STATUS_TOO_MANY_WORDS, STATUS_UNKNOWN_KEYWORD, STATUS_SYNTAX, STATUS_NESTING,
 *          STATUS_NOT_FOUND and STATUS_BAD_PORT (inst line), and the statuses of nl_add_input,
 *          nl_parse_gate_words and nl_add_gate (an output, subckt or inst line gives the same kinds).
 */
Status_t nl_parse_line(Netlist_t *nl, NlParseState_t *state, char *line, Diag_t *out_diag);

/**
 * @brief   Reads a whole .net file and the file it includes into the tables (they are emptied
 *          first), then checks the whole circuit.
 * @details A stack of MAX_INCLUDE_DEPTH line readers (no recursion): an include line opens its
 *          file, relative to the including file (tu_join_path), and reading goes on there; at its
 *          end reading goes back to the including file. An include inside an included file is
 *          NESTING. Stops at the first bad line; every open file is closed on every path. A file
 *          that ends inside a block is a NESTING error ("subckt NAME not closed", its last line).
 * @param   nl        Out: the netlist (discard it after an error).
 * @param   path      Path of the .net file.
 * @param   out_diag  Out: on an error, the message: the file that has the bad line (path or the
 *                    included file), the number of that line (0 if the netlist could not be
 *                    opened, and for the whole-circuit check) and the text of nl_parse_line or
 *                    the circuit check (an include that cannot be opened: its path); unchanged on
 *                    STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_FILE_OPEN; STATUS_FILE_READ;
 *          STATUS_LINE_TOO_LONG; the status of nl_parse_line for the bad line; STATUS_NESTING;
 *          STATUS_UNDRIVEN.
 */
Status_t nl_load(Netlist_t *nl, const char *path, Diag_t *out_diag);

#ifdef __cplusplus
}
#endif

#endif /* NETLIST_H */

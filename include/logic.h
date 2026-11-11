/**
 * @file    logic.h
 * @brief   Logic values 0, 1 and X (unknown), gate types, and the value of one gate from its inputs.
 * @details Every gate is calculated with small lookup tables:
 *          the inputs are combined one by one through the AND, OR or XOR table,
 *          and NAND, NOR and XNOR end with NOT.
 */
#ifndef LOGIC_H
#define LOGIC_H

#include <stdint.h>

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Value of a net: LV_0, LV_1 or LV_X (a small number, so it can index the tables). */
typedef uint8_t LogicValue_t;

#define LV_0        ((LogicValue_t)0U)   /**< logic 0 */
#define LV_1        ((LogicValue_t)1U)   /**< logic 1 */
#define LV_X        ((LogicValue_t)2U)   /**< unknown: not driven yet, or depends on an unknown input */
#define LV_COUNT    3U                   /**< number of values (size of the tables) */

/**
 * @brief Gate types: the families with 1..MAX_GATE_INPUTS inputs first, the one-input
 *        gates last. GT_COUNT is the number of types, keep it last.
 */
typedef enum
{
    GT_AND = 0,
    GT_OR,
    GT_NAND,
    GT_NOR,
    GT_XOR,
    GT_XNOR,
    GT_NOT,
    GT_BUF,
    GT_COUNT
} GateType_t;

/**
 * @brief   Calculates the output of one gate.
 * @details result = input 0; then result = TABLE[result][input i] for every further
 *          input; NAND, NOR and XNOR end with NOT. NOT inverts input 0, BUF copies it.
 *          A deciding value wins over X (a 0 into AND gives 0, a 1 into OR gives 1);
 *          otherwise an X input gives X. XOR has no deciding value.
 * @param   type       The gate type.
 * @param   inputs     The input values (count entries, each LV_0, LV_1 or LV_X).
 * @param   count      Number of inputs (see lg_check_input_count).
 * @param   out_value  Out: the output value, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG for a type >= GT_COUNT or an
 *          input value >= LV_COUNT; STATUS_INPUT_COUNT if count is wrong for the type.
 */
Status_t lg_eval(GateType_t type, const LogicValue_t inputs[], uint32_t count, LogicValue_t *out_value);

/**
 * @brief   Checks the number of inputs for a gate type.
 * @param   type   The gate type.
 * @param   count  Number of inputs.
 * @return  STATUS_OK (NOT and BUF: exactly 1; the others 1..MAX_GATE_INPUTS);
 *          STATUS_INPUT_COUNT otherwise; STATUS_BAD_ARG for a type >= GT_COUNT.
 */
Status_t lg_check_input_count(GateType_t type, uint32_t count);

/**
 * @brief   Finds the gate type of a name from a netlist file.
 * @param   name      Exact upper-case name: "AND", "OR", "NAND", "NOR", "XOR", "XNOR", "NOT", "BUF".
 * @param   out_type  Out: the type, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_UNKNOWN_TYPE for any other text.
 */
Status_t lg_type_from_name(const char *name, GateType_t *out_type);

/**
 * @brief   Gives the name of a gate type, as written in a netlist file.
 * @details A pure lookup that cannot fail, so it returns the text directly
 *          (documented exception to the Status_t rule).
 * @param   type  The gate type.
 * @return  "AND", "OR", "NAND", ...; "?" for a type >= GT_COUNT. Never NULL.
 */
const char *lg_type_name(GateType_t type);

/**
 * @brief   Reads a logic value written in a stimulus file.
 * @param   text       "0", "1" or "x".
 * @param   out_value  Out: LV_0, LV_1 or LV_X, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_NUMBER for any other text.
 */
Status_t lg_value_from_text(const char *text, LogicValue_t *out_value);

/**
 * @brief   Gives the character that shows a value on the console.
 * @details A pure lookup that cannot fail, so it returns the character directly
 *          (documented exception to the Status_t rule).
 * @param   value  The value.
 * @return  '0', '1' or 'x'; '?' for a value >= LV_COUNT.
 */
char lg_value_char(LogicValue_t value);

#ifdef __cplusplus
}
#endif

#endif /* LOGIC_H */

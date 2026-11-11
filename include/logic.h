#ifndef LOGIC_H
#define LOGIC_H

#include <stdint.h>

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t LogicValue_t;

#define LV_0        ((LogicValue_t)0U)
#define LV_1        ((LogicValue_t)1U)
#define LV_X        ((LogicValue_t)2U)
#define LV_COUNT    3U

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

/* Calculates the output of one gate. */
Status_t lg_eval(GateType_t type, const LogicValue_t inputs[], uint32_t count, LogicValue_t *out_value);

/* Checks the number of inputs for a gate type. */
Status_t lg_check_input_count(GateType_t type, uint32_t count);

/* Finds the gate type of a name from a netlist file. */
Status_t lg_type_from_name(const char *name, GateType_t *out_type);

/* Reads a logic value written in a stimulus file. */
Status_t lg_value_from_text(const char *text, LogicValue_t *out_value);

/* Gives the character that shows a value on the console. */
char lg_value_char(LogicValue_t value);

#ifdef __cplusplus
}
#endif

#endif

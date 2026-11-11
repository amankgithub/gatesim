/**
 * @file    logic.c
 * @brief   Lookup tables for 0/1/X gate logic, the gate type names and the value texts.
 */
#include "logic.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "config.h"

/** @brief One row of the gate type name table. */
typedef struct
{
    const char *name;   /**< name as written in a netlist file */
    GateType_t  type;   /**< the matching type */
} GateTypeName_t;

/** Text of lg_type_name for a type that does not exist. */
#define UNKNOWN_TYPE_NAME   "?"

/** Name of every gate type, as written in a netlist file, in GateType_t order (lg_type_name indexes it by type). */
static const GateTypeName_t TYPE_NAMES[GT_COUNT] =
{
    { "AND",  GT_AND  },
    { "OR",   GT_OR   },
    { "NAND", GT_NAND },
    { "NOR",  GT_NOR  },
    { "XOR",  GT_XOR  },
    { "XNOR", GT_XNOR },
    { "NOT",  GT_NOT  },
    { "BUF",  GT_BUF  }
};

/**
 * @brief   Tells whether every input value is LV_0, LV_1 or LV_X.
 * @param   inputs  The values.
 * @param   count   Number of values.
 * @return  true if all are valid.
 */
static bool values_are_valid(const LogicValue_t inputs[], uint32_t count)
{
    bool valid = true;
    uint32_t i = 0U;

    while ((i < count) && valid)
    {
        if (inputs[i] >= LV_COUNT)
        {
            valid = false;
        }
        i++;
    }
    return valid;
}

Status_t lg_check_input_count(GateType_t type, uint32_t count)
{
    Status_t status = STATUS_OK;

    if (type >= GT_COUNT)
    {
        status = STATUS_BAD_ARG;
    }
    else if ((type == GT_NOT) || (type == GT_BUF))
    {
        if (count != 1U)
        {
            status = STATUS_INPUT_COUNT;
        }
    }
    else if ((count < 1U) || (count > MAX_GATE_INPUTS))
    {
        status = STATUS_INPUT_COUNT;
    }
    else
    {
        /* a valid count for this type */
    }
    return status;
}

/**
 * @brief   Combines the inputs one by one through a 3 x 3 table.
 * @param   table   AND, OR or XOR table, indexed [result so far][next input].
 * @param   inputs  The values (already checked, count >= 1).
 * @param   count   Number of values.
 * @return  The combined value.
 */
static LogicValue_t combine_inputs(const LogicValue_t table[LV_COUNT][LV_COUNT],
                                   const LogicValue_t inputs[], uint32_t count)
{
    LogicValue_t result = inputs[0];
    uint32_t i;

    for (i = 1U; i < count; i++)
    {
        result = table[result][inputs[i]];
    }
    return result;
}

Status_t lg_eval(GateType_t type, const LogicValue_t inputs[], uint32_t count, LogicValue_t *out_value)
{
    /* Truth tables indexed [a][b] with a, b in 0, 1, X. A 0 decides an AND and a 1 decides an OR
       even when the other input is X; XOR has no deciding value. */
    static const LogicValue_t AND_TABLE[LV_COUNT][LV_COUNT] =
    {
        { LV_0, LV_0, LV_0 },
        { LV_0, LV_1, LV_X },
        { LV_0, LV_X, LV_X }
    };
    static const LogicValue_t OR_TABLE[LV_COUNT][LV_COUNT] =
    {
        { LV_0, LV_1, LV_X },
        { LV_1, LV_1, LV_1 },
        { LV_X, LV_1, LV_X }
    };
    static const LogicValue_t XOR_TABLE[LV_COUNT][LV_COUNT] =
    {
        { LV_0, LV_1, LV_X },
        { LV_1, LV_0, LV_X },
        { LV_X, LV_X, LV_X }
    };
    static const LogicValue_t NOT_TABLE[LV_COUNT] = { LV_1, LV_0, LV_X };
    Status_t status = STATUS_OK;

    if ((inputs == NULL) || (out_value == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        /* Also rejects a type >= GT_COUNT (BAD_ARG); the count is checked before any input is read. */
        status = lg_check_input_count(type, count);
        if ((status == STATUS_OK) && !values_are_valid(inputs, count))
        {
            status = STATUS_BAD_ARG;
        }
        if (status == STATUS_OK)
        {
            LogicValue_t result = LV_0;

            switch (type)
            {
                case GT_AND:
                    result = combine_inputs(AND_TABLE, inputs, count);
                    break;
                case GT_OR:
                    result = combine_inputs(OR_TABLE, inputs, count);
                    break;
                case GT_NAND:
                    result = NOT_TABLE[combine_inputs(AND_TABLE, inputs, count)];
                    break;
                case GT_NOR:
                    result = NOT_TABLE[combine_inputs(OR_TABLE, inputs, count)];
                    break;
                case GT_XOR:
                    result = combine_inputs(XOR_TABLE, inputs, count);
                    break;
                case GT_XNOR:
                    result = NOT_TABLE[combine_inputs(XOR_TABLE, inputs, count)];
                    break;
                case GT_NOT:
                    result = NOT_TABLE[inputs[0]];
                    break;
                case GT_BUF:
                    /* a copy: X stays X */
                    result = inputs[0];
                    break;
                default:
                    /* not reached: lg_check_input_count has rejected the type */
                    status = STATUS_BAD_ARG;
                    break;
            }
            if (status == STATUS_OK)
            {
                *out_value = result;
            }
        }
    }
    return status;
}

Status_t lg_type_from_name(const char *name, GateType_t *out_type)
{
    Status_t status = STATUS_UNKNOWN_TYPE;

    if ((name == NULL) || (out_type == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        uint32_t i = 0U;

        while ((i < (uint32_t)GT_COUNT) && (status != STATUS_OK))
        {
            if (strcmp(name, TYPE_NAMES[i].name) == 0)
            {
                *out_type = TYPE_NAMES[i].type;
                status = STATUS_OK;
            }
            i++;
        }
    }
    return status;
}

const char *lg_type_name(GateType_t type)
{
    const char *name = UNKNOWN_TYPE_NAME;

    if (type < GT_COUNT)
    {
        name = TYPE_NAMES[type].name;
    }
    return name;
}

Status_t lg_value_from_text(const char *text, LogicValue_t *out_value)
{
    Status_t status = STATUS_OK;

    if ((text == NULL) || (out_value == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (strcmp(text, "0") == 0)
    {
        *out_value = LV_0;
    }
    else if (strcmp(text, "1") == 0)
    {
        *out_value = LV_1;
    }
    else if (strcmp(text, "x") == 0)
    {
        *out_value = LV_X;
    }
    else
    {
        status = STATUS_BAD_NUMBER;
    }
    return status;
}

char lg_value_char(LogicValue_t value)
{
    /* One character per value, in the order of LogicValue_t. */
    static const char VALUE_CHARS[LV_COUNT] = { '0', '1', 'x' };
    char shown = '?';

    if (value < LV_COUNT)
    {
        shown = VALUE_CHARS[value];
    }
    return shown;
}

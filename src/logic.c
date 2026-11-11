#include "logic.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "config.h"

typedef struct
{
    const char *name;
    GateType_t  type;
} GateTypeName_t;

/* Tells whether every input value is LV_0, LV_1 or LV_X. */
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

/* Combines the inputs one by one through a 3 x 3 table. */
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
                    result = inputs[0];
                    break;
                default:
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
    static const char VALUE_CHARS[LV_COUNT] = { '0', '1', 'x' };
    char shown = '?';

    if (value < LV_COUNT)
    {
        shown = VALUE_CHARS[value];
    }
    return shown;
}

#include "netlist.h"

#include <stddef.h>
#include <string.h>

#include "text_utils.h"

#define INPUT_KEYWORD       "input"
#define OUTPUT_KEYWORD      "output"
#define GATE_KEYWORD        "gate"
#define ASSIGN_WORD         "="

#define GW_NAME             1U
#define GW_TYPE             2U
#define GW_DELAY            3U
#define GW_OUT              4U
#define GW_ASSIGN           5U
#define GW_FIRST_IN         6U
#define GATE_MIN_WORDS      7U
#define DECLARE_MIN_WORDS   2U

/* Checks a name and copies it into a name field. */
static Status_t copy_name(char dst[MAX_NAME_LEN], const char *name)
{
    Status_t status = tu_check_name(name);

    if (status == STATUS_OK)
    {
        status = tu_copy_text(dst, MAX_NAME_LEN, name);
    }
    return status;
}

/* Tells whether names[index] already appeared in names[0 .. index-1]. */
static bool name_seen_before(const char *const names[], uint32_t index)
{
    bool seen = false;
    uint32_t i = 0U;

    while ((i < index) && !seen)
    {
        seen = (strcmp(names[i], names[index]) == 0);
        i++;
    }
    return seen;
}

/* Checks whether an existing net may become an input. */
static Status_t input_conflict(const Net_t *net)
{
    Status_t status = STATUS_OK;

    switch (net->driver)
    {
        case DRV_NONE:
            status = STATUS_OK;
            break;
        case DRV_INPUT:
            status = STATUS_DUPLICATE;
            break;
        case DRV_GATE:
            status = STATUS_TWO_DRIVERS;
            break;
        default:
            status = STATUS_BAD_ARG;
            break;
    }
    return status;
}

/* Empties both tables (worker of nl_init and nl_load). */
static void clear_tables(Netlist_t *nl)
{
    (void)memset(nl, 0, sizeof(*nl));
}

Status_t nl_find_net(const Netlist_t *nl, const char *name, uint16_t *out_index)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (name != NULL) && (out_index != NULL))
    {
        uint32_t i = 0U;

        status = STATUS_NOT_FOUND;
        while ((i < (uint32_t)nl->net_count) && (status != STATUS_OK))
        {
            if (strcmp(nl->nets[i].name, name) == 0)
            {
                *out_index = (uint16_t)i;
                status = STATUS_OK;
            }
            i++;
        }
    }
    return status;
}

Status_t nl_find_or_add_net(Netlist_t *nl, const char *name, uint16_t *out_index)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (name != NULL) && (out_index != NULL))
    {
        Net_t added;
        uint16_t index = 0U;

        status = copy_name(added.name, name);
        if (status == STATUS_OK)
        {
            if (nl_find_net(nl, name, &index) == STATUS_OK)
            {
                *out_index = index;
            }
            else if ((uint32_t)nl->net_count >= MAX_NETS)
            {
                status = STATUS_TABLE_FULL;
            }
            else
            {
                added.driver = DRV_NONE;
                added.is_output = false;
                added.value = LV_0;
                index = nl->net_count;
                nl->nets[index] = added;
                nl->net_count++;
                *out_index = index;
            }
        }
    }
    return status;
}

Status_t nl_add_input(Netlist_t *nl, const char *name)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (name != NULL))
    {
        uint16_t index = 0U;

        status = nl_find_net(nl, name, &index);
        if (status == STATUS_OK)
        {
            status = input_conflict(&nl->nets[index]);
        }
        else
        {
            status = nl_find_or_add_net(nl, name, &index);
        }
        if (status == STATUS_OK)
        {
            nl->nets[index].driver = DRV_INPUT;
        }
    }
    return status;
}

/* Declares one output net (found or added): the user wants to see it. */
static Status_t add_output(Netlist_t *nl, const char *name)
{
    uint16_t index = 0U;
    Status_t status = nl_find_or_add_net(nl, name, &index);

    if (status == STATUS_OK)
    {
        nl->nets[index].is_output = true;
    }
    return status;
}

Status_t nl_parse_delay(const char *word, uint16_t *out_delay)
{
    Status_t status = STATUS_NULL_ARG;

    if ((word != NULL) && (out_delay != NULL))
    {
        status = STATUS_SYNTAX;
        if (word[0] == '#')
        {
            uint32_t delay = 0U;

            status = tu_parse_u32(&word[1], MIN_DELAY, MAX_DELAY, &delay);
            if (status == STATUS_OK)
            {
                *out_delay = (uint16_t)delay;
            }
        }
    }
    return status;
}

Status_t nl_parse_gate_words(char *const words[], uint32_t count, GateSpec_t *out_spec)
{
    Status_t status = STATUS_NULL_ARG;

    if ((words != NULL) && (out_spec != NULL))
    {
        status = STATUS_OK;
        if ((count < GATE_MIN_WORDS) || (strcmp(words[0], GATE_KEYWORD) != 0))
        {
            status = STATUS_SYNTAX;
        }
        else
        {
            GateSpec_t spec;
            const uint32_t in_count = count - GW_FIRST_IN;

            (void)memset(&spec, 0, sizeof(spec));
            status = copy_name(spec.name, words[GW_NAME]);
            if (status == STATUS_OK)
            {
                status = lg_type_from_name(words[GW_TYPE], &spec.type);
            }
            if (status == STATUS_OK)
            {
                status = nl_parse_delay(words[GW_DELAY], &spec.delay);
            }
            if (status == STATUS_OK)
            {
                status = copy_name(spec.out, words[GW_OUT]);
            }
            if ((status == STATUS_OK) && (strcmp(words[GW_ASSIGN], ASSIGN_WORD) != 0))
            {
                status = STATUS_SYNTAX;
            }
            if (status == STATUS_OK)
            {
                status = lg_check_input_count(spec.type, in_count);
            }
            if (status == STATUS_OK)
            {
                uint32_t i = 0U;

                while ((i < in_count) && (status == STATUS_OK))
                {
                    status = copy_name(spec.in[i], words[GW_FIRST_IN + i]);
                    i++;
                }
            }
            if (status == STATUS_OK)
            {
                spec.in_count = (uint8_t)in_count;
                *out_spec = spec;
            }
        }
    }
    return status;
}

/* Check-only pass of add_gate: nothing is written. */
static Status_t check_gate(const Netlist_t *nl, const GateSpec_t *spec)
{
    Status_t status = STATUS_OK;

    if ((uint32_t)nl->gate_count >= MAX_GATES)
    {
        status = STATUS_TABLE_FULL;
    }
    if (status == STATUS_OK)
    {
        const char *names[1U + MAX_GATE_INPUTS];
        const uint32_t name_count = 1U + (uint32_t)spec->in_count;
        uint32_t new_nets = 0U;
        uint32_t k;

        names[0] = spec->out;
        for (k = 0U; k < (uint32_t)spec->in_count; k++)
        {
            names[1U + k] = spec->in[k];
        }
        k = 0U;
        while ((k < name_count) && (status == STATUS_OK))
        {
            uint16_t index = 0U;

            if (nl_find_net(nl, names[k], &index) == STATUS_OK)
            {
                if ((k == 0U) && (nl->nets[index].driver != DRV_NONE))
                {
                    status = STATUS_TWO_DRIVERS;
                }
            }
            else if (!name_seen_before(names, k))
            {
                new_nets++;
            }
            else
            {
                /* a new name already counted (the same net twice in this gate) */
            }
            k++;
        }
        if ((status == STATUS_OK) && (((uint32_t)nl->net_count + new_nets) > MAX_NETS))
        {
            status = STATUS_TABLE_FULL;
        }
    }
    return status;
}

Status_t nl_add_gate(Netlist_t *nl, const GateSpec_t *spec)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (spec != NULL))
    {
        status = check_gate(nl, spec);
        if (status == STATUS_OK)
        {
            Gate_t gate;
            uint32_t i = 0U;

            (void)memset(&gate, 0, sizeof(gate));
            status = tu_copy_text(gate.name, MAX_NAME_LEN, spec->name);
            gate.type = spec->type;
            gate.in_count = spec->in_count;
            gate.delay = spec->delay;
            if (status == STATUS_OK)
            {
                status = nl_find_or_add_net(nl, spec->out, &gate.out);
            }
            while ((i < (uint32_t)spec->in_count) && (status == STATUS_OK))
            {
                status = nl_find_or_add_net(nl, spec->in[i], &gate.in[i]);
                i++;
            }
            if (status == STATUS_OK)
            {
                nl->nets[gate.out].driver = DRV_GATE;
                nl->gates[nl->gate_count] = gate;
                nl->gate_count++;
            }
        }
    }
    return status;
}

/* Check-only pass of an input or output line: every name, before any is added. */
static Status_t check_declare_names(const Netlist_t *nl, const char *const names[], uint32_t name_count,
                                    bool as_input)
{
    Status_t status = STATUS_OK;
    uint32_t new_nets = 0U;
    uint32_t i = 0U;

    while ((i < name_count) && (status == STATUS_OK))
    {
        uint16_t index = 0U;

        status = tu_check_name(names[i]);
        if (status == STATUS_OK)
        {
            if (name_seen_before(names, i))
            {
                if (as_input)
                {
                    status = STATUS_DUPLICATE;
                }
            }
            else if (nl_find_net(nl, names[i], &index) == STATUS_OK)
            {
                if (as_input)
                {
                    status = input_conflict(&nl->nets[index]);
                }
            }
            else
            {
                new_nets++;
            }
        }
        i++;
    }
    if ((status == STATUS_OK) && (((uint32_t)nl->net_count + new_nets) > MAX_NETS))
    {
        status = STATUS_TABLE_FULL;
    }
    return status;
}

/* Handles "input A B ..." or "output Q QN ...": all names checked, then added. */
static Status_t parse_declare_line(Netlist_t *nl, const char *const words[], uint32_t count, bool as_input)
{
    Status_t status = STATUS_SYNTAX;

    if (count >= DECLARE_MIN_WORDS)
    {
        const char *const *const names = &words[1];
        const uint32_t name_count = count - 1U;
        uint32_t i = 0U;

        status = check_declare_names(nl, names, name_count, as_input);
        while ((i < name_count) && (status == STATUS_OK))
        {
            if (as_input)
            {
                status = nl_add_input(nl, names[i]);
            }
            else
            {
                status = add_output(nl, names[i]);
            }
            i++;
        }
    }
    return status;
}

Status_t nl_parse_line(Netlist_t *nl, char *line)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (line != NULL))
    {
        char *words[MAX_WORDS];
        uint32_t count = 0U;

        status = tu_cut_comment(line);
        if (status == STATUS_OK)
        {
            status = tu_split_words(line, words, MAX_WORDS, &count);
        }
        if ((status == STATUS_OK) && (count > 0U))
        {
            if (strcmp(words[0], INPUT_KEYWORD) == 0)
            {
                status = parse_declare_line(nl, (const char *const *)words, count, true);
            }
            else if (strcmp(words[0], OUTPUT_KEYWORD) == 0)
            {
                status = parse_declare_line(nl, (const char *const *)words, count, false);
            }
            else if (strcmp(words[0], GATE_KEYWORD) == 0)
            {
                GateSpec_t spec;

                status = nl_parse_gate_words(words, count, &spec);
                if (status == STATUS_OK)
                {
                    status = nl_add_gate(nl, &spec);
                }
            }
            else
            {
                status = STATUS_UNKNOWN_KEYWORD;
            }
        }
    }
    return status;
}

Status_t nl_init(Netlist_t *nl)
{
    Status_t status = STATUS_NULL_ARG;

    if (nl != NULL)
    {
        clear_tables(nl);
        status = STATUS_OK;
    }
    return status;
}

Status_t nl_load(Netlist_t *nl, const char *path, uint32_t *out_line)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (path != NULL) && (out_line != NULL))
    {
        LineReader_t reader;

        clear_tables(nl);
        status = tu_open(&reader, path);
        if (status != STATUS_OK)
        {
            *out_line = 0U;
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
                }
                else
                {
                    status = nl_parse_line(nl, line);
                }
            }
            close_status = tu_close(&reader);
            if (status == STATUS_OK)
            {
                status = close_status;
            }
            if (status != STATUS_OK)
            {
                *out_line = reader.line_no;
            }
        }
    }
    return status;
}

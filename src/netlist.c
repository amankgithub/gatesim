#include "netlist.h"

#include <stddef.h>
#include <string.h>

#include "text_utils.h"

#define INPUT_KEYWORD       "input"
#define OUTPUT_KEYWORD      "output"
#define GATE_KEYWORD        "gate"
#define SUBCKT_KEYWORD      "subckt"
#define ENDS_KEYWORD        "ends"
#define INST_KEYWORD        "inst"
#define INCLUDE_KEYWORD     "include"
#define ASSIGN_WORD         "="
#define ARROW_WORD          "->"
#define INST_SEPARATOR      "."
#define VCC_NAME            "VCC"
#define GND_NAME            "GND"

#define GW_NAME             1U
#define GW_TYPE             2U
#define GW_DELAY            3U
#define GW_OUT              4U
#define GW_ASSIGN           5U
#define GW_FIRST_IN         6U
#define GATE_MIN_WORDS      7U
#define DECLARE_MIN_WORDS   2U

#define SW_NAME             1U
#define SW_FIRST_PORT       2U
#define SUBCKT_MIN_WORDS    3U
#define ENDS_WORDS          1U
#define IW_NAME             1U
#define IW_BLOCK            2U
#define IW_FIRST_PORT       3U
#define INST_MIN_WORDS      4U
#define INCLUDE_W_PATH      1U
#define INCLUDE_WORDS       2U
#define NOT_CLOSED_TEXT     " not closed"

typedef enum
{
    KW_INPUT = 0,
    KW_OUTPUT,
    KW_GATE,
    KW_SUBCKT,
    KW_ENDS,
    KW_INST,
    KW_INCLUDE,
    KW_COUNT
} NetKeyword_t;

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

/* Joins three texts into a buffer ("FF2" "." "m1" gives "FF2.m1"), without stdio. */
static Status_t join_three(char *out, uint32_t out_size, const char *a, const char *b, const char *c)
{
    Status_t status = STATUS_OK;
    const size_t len_a = strlen(a);
    const size_t len_b = strlen(b);
    const size_t len_c = strlen(c);

    if ((len_a + len_b + len_c) >= (size_t)out_size)
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        (void)memcpy(out, a, len_a);
        (void)memcpy(&out[len_a], b, len_b);
        (void)memcpy(&out[len_a + len_b], c, len_c + 1U);
    }
    return status;
}

/* Finds the keyword of a line in the keyword table. */
static Status_t find_keyword(const char *word, NetKeyword_t *out_keyword)
{
    static const char *const KEYWORDS[KW_COUNT] = {
        INPUT_KEYWORD, OUTPUT_KEYWORD, GATE_KEYWORD, SUBCKT_KEYWORD, ENDS_KEYWORD, INST_KEYWORD,
        INCLUDE_KEYWORD
    };
    Status_t status = STATUS_UNKNOWN_KEYWORD;
    uint32_t i = 0U;

    while ((i < (uint32_t)KW_COUNT) && (status != STATUS_OK))
    {
        if (strcmp(word, KEYWORDS[i]) == 0)
        {
            *out_keyword = (NetKeyword_t)i;
            status = STATUS_OK;
        }
        i++;
    }
    return status;
}

/* Fills the text of an error message: the status text and the offending word. */
static void set_line_diag(Diag_t *diag, Status_t status, const char *word)
{
    if (tu_set_diag(diag, NULL, 0U, status_text(status), word) != STATUS_OK)
    {
        (void)tu_set_diag(diag, NULL, 0U, status_text(status), NULL);
    }
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

/* Tells whether a name is one of the constant nets, and its value. */
static bool constant_value(const char *name, LogicValue_t *out_value)
{
    bool is_constant = true;

    if (strcmp(name, VCC_NAME) == 0)
    {
        *out_value = LV_1;
    }
    else if (strcmp(name, GND_NAME) == 0)
    {
        *out_value = LV_0;
    }
    else
    {
        is_constant = false;
    }
    return is_constant;
}

/* Tells whether a name is VCC or GND. */
static bool is_constant_name(const char *name)
{
    LogicValue_t value = LV_X;

    return constant_value(name, &value);
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
        case DRV_CONST:
            status = STATUS_TWO_DRIVERS;
            break;
        default:
            status = STATUS_BAD_ARG;
            break;
    }
    return status;
}

/* Checks whether an existing net may become an output. */
static Status_t output_conflict(const Net_t *net)
{
    Status_t status = STATUS_OK;

    if (net->is_output)
    {
        status = STATUS_DUPLICATE;
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

Status_t nl_find_gate(const Netlist_t *nl, const char *name, uint16_t *out_index)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (name != NULL) && (out_index != NULL))
    {
        uint32_t i = 0U;

        status = STATUS_NOT_FOUND;
        while ((i < (uint32_t)nl->gate_count) && (status != STATUS_OK))
        {
            if (strcmp(nl->gates[i].name, name) == 0)
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
                LogicValue_t constant = LV_X;

                added.driver = constant_value(name, &constant) ? DRV_CONST : DRV_NONE;
                added.is_output = false;
                added.is_read = false;
                added.init_value = constant;
                added.value = constant;
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
        else if (is_constant_name(name))
        {
            status = STATUS_TWO_DRIVERS;
        }
        else
        {
            status = nl_find_or_add_net(nl, name, &index);
        }
        if (status == STATUS_OK)
        {
            nl->nets[index].driver = DRV_INPUT;
            nl->input_count++;
        }
    }
    return status;
}

/* Declares one output net (found or added): the user wants to see it. Counts it in output_count. */
static Status_t add_output(Netlist_t *nl, const char *name)
{
    uint16_t index = 0U;
    Status_t status = nl_find_net(nl, name, &index);

    if (status == STATUS_OK)
    {
        status = output_conflict(&nl->nets[index]);
    }
    else
    {
        status = nl_find_or_add_net(nl, name, &index);
    }
    if (status == STATUS_OK)
    {
        nl->nets[index].is_output = true;
        nl->output_count++;
    }
    return status;
}

Status_t nl_parse_delay(const char *word, uint16_t *out_rise, uint16_t *out_fall)
{
    Status_t status = STATUS_NULL_ARG;

    if ((word != NULL) && (out_rise != NULL) && (out_fall != NULL))
    {
        status = STATUS_SYNTAX;
        if (word[0] == '#')
        {
            char text[MAX_LINE_LEN];
            uint32_t rise = 0U;
            uint32_t fall = 0U;

            status = (tu_copy_text(text, MAX_LINE_LEN, &word[1]) == STATUS_OK) ? STATUS_OK : STATUS_BAD_NUMBER;
            if (status == STATUS_OK)
            {
                char *const comma = strchr(text, (int)',');

                if (comma == NULL)
                {
                    status = tu_parse_u32(text, MIN_DELAY, MAX_DELAY, &rise);
                    fall = rise;
                }
                else
                {
                    *comma = '\0';
                    status = tu_parse_u32(text, MIN_DELAY, MAX_DELAY, &rise);
                    if (status == STATUS_OK)
                    {
                        status = tu_parse_u32(&comma[1], MIN_DELAY, MAX_DELAY, &fall);
                    }
                }
            }
            if (status == STATUS_OK)
            {
                *out_rise = (uint16_t)rise;
                *out_fall = (uint16_t)fall;
            }
        }
    }
    return status;
}

/* Checks the words of a gate line (worker of nl_parse_gate_words). */
static Status_t parse_gate_words(char *const words[], uint32_t count, GateSpec_t *out_spec,
                                 uint32_t *out_bad_word)
{
    Status_t status = STATUS_OK;
    uint32_t bad_word = 0U;

    if ((count < GATE_MIN_WORDS) || (strcmp(words[0], GATE_KEYWORD) != 0))
    {
        status = STATUS_SYNTAX;
    }
    else
    {
        GateSpec_t spec;
        const uint32_t in_count = count - GW_FIRST_IN;

        (void)memset(&spec, 0, sizeof(spec));
        bad_word = GW_NAME;
        status = copy_name(spec.name, words[GW_NAME]);
        if (status == STATUS_OK)
        {
            bad_word = GW_TYPE;
            status = lg_type_from_name(words[GW_TYPE], &spec.type);
        }
        if (status == STATUS_OK)
        {
            bad_word = GW_DELAY;
            status = nl_parse_delay(words[GW_DELAY], &spec.rise, &spec.fall);
        }
        if (status == STATUS_OK)
        {
            bad_word = GW_OUT;
            status = copy_name(spec.out, words[GW_OUT]);
        }
        if ((status == STATUS_OK) && (strcmp(words[GW_ASSIGN], ASSIGN_WORD) != 0))
        {
            bad_word = GW_ASSIGN;
            status = STATUS_SYNTAX;
        }
        if (status == STATUS_OK)
        {
            bad_word = GW_TYPE;
            status = lg_check_input_count(spec.type, in_count);
        }
        if (status == STATUS_OK)
        {
            uint32_t i = 0U;

            while ((i < in_count) && (status == STATUS_OK))
            {
                bad_word = GW_FIRST_IN + i;
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
    if (status != STATUS_OK)
    {
        *out_bad_word = bad_word;
    }
    return status;
}

/* Check-only pass of add_gate: nothing is written. */
static Status_t check_gate(const Netlist_t *nl, const GateSpec_t *spec)
{
    Status_t status = STATUS_OK;
    uint16_t same_name = 0U;

    if (nl_find_gate(nl, spec->name, &same_name) == STATUS_OK)
    {
        status = STATUS_DUPLICATE;
    }
    if ((status == STATUS_OK) && ((uint32_t)nl->gate_count >= MAX_GATES))
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
            else if ((k == 0U) && is_constant_name(names[k]))
            {
                status = STATUS_TWO_DRIVERS;
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
            gate.rise = spec->rise;
            gate.fall = spec->fall;
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
                for (i = 0U; i < (uint32_t)gate.in_count; i++)
                {
                    nl->nets[gate.in[i]].is_read = true;
                }
                nl->gates[nl->gate_count] = gate;
                nl->gate_count++;
            }
        }
    }
    return status;
}

Status_t nl_find_block(const Netlist_t *nl, const char *name, uint8_t *out_block)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (name != NULL) && (out_block != NULL))
    {
        uint32_t i = 0U;

        status = STATUS_NOT_FOUND;
        while ((i < (uint32_t)nl->block_count) && (status != STATUS_OK))
        {
            if (strcmp(nl->blocks[i].name, name) == 0)
            {
                *out_block = (uint8_t)i;
                status = STATUS_OK;
            }
            i++;
        }
    }
    return status;
}

/* Check-only pass of a subckt line: the block name, every port, at most one "->". */
static Status_t check_block_words(const Netlist_t *nl, char *const words[], uint32_t count, uint32_t *out_bad_word)
{
    const char *const *const ports = (const char *const *)&words[SW_FIRST_PORT];
    uint8_t found = 0U;
    uint32_t port_count = 0U;
    bool seen_arrow = false;
    uint32_t bad_word = SW_NAME;
    uint32_t i = SW_FIRST_PORT;
    Status_t status = tu_check_name(words[SW_NAME]);

    if ((status == STATUS_OK) && (nl_find_block(nl, words[SW_NAME], &found) == STATUS_OK))
    {
        status = STATUS_DUPLICATE;
    }
    if ((status == STATUS_OK) && ((uint32_t)nl->block_count >= MAX_BLOCKS))
    {
        bad_word = 0U;
        status = STATUS_TABLE_FULL;
    }
    while ((status == STATUS_OK) && (i < count))
    {
        bad_word = i;
        if (strcmp(words[i], ARROW_WORD) == 0)
        {
            status = seen_arrow ? STATUS_SYNTAX : STATUS_OK;
            seen_arrow = true;
        }
        else if (port_count >= MAX_BLOCK_PORTS)
        {
            bad_word = 0U;
            status = STATUS_TABLE_FULL;
        }
        else
        {
            status = tu_check_name(words[i]);
            if ((status == STATUS_OK) && name_seen_before(ports, i - SW_FIRST_PORT))
            {
                status = STATUS_DUPLICATE;
            }
            port_count++;
        }
        i++;
    }
    if ((status == STATUS_OK) && (port_count == 0U))
    {
        bad_word = 0U;
        status = STATUS_SYNTAX;
    }
    if (status != STATUS_OK)
    {
        *out_bad_word = bad_word;
    }
    return status;
}

/* Starts a block from the words of a `subckt NAME P1 .. Pk [-> Q1 .. Qm]` line */
static Status_t begin_block(Netlist_t *nl, char *const words[], uint32_t count, uint8_t *out_block,
                            uint32_t *out_bad_word)
{
    Status_t status = STATUS_SYNTAX;

    if (count < SUBCKT_MIN_WORDS)
    {
        *out_bad_word = 0U;
    }
    else
    {
        status = check_block_words(nl, words, count, out_bad_word);
    }
    if (status == STATUS_OK)
    {
        Block_t *const block = &nl->blocks[nl->block_count];
        bool seen_arrow = false;
        uint32_t i;

        (void)memset(block, 0, sizeof(*block));
        status = tu_copy_text(block->name, MAX_NAME_LEN, words[SW_NAME]);
        for (i = SW_FIRST_PORT; (i < count) && (status == STATUS_OK); i++)
        {
            if (strcmp(words[i], ARROW_WORD) == 0)
            {
                block->first_output_port = block->port_count;
                seen_arrow = true;
            }
            else
            {
                status = tu_copy_text(block->ports[block->port_count], MAX_NAME_LEN, words[i]);
                block->port_count++;
            }
        }
        if (!seen_arrow)
        {
            block->first_output_port = block->port_count;
        }
        if (status == STATUS_OK)
        {
            *out_block = nl->block_count;
            nl->block_count++;
        }
    }
    return status;
}

/* Stores one gate line in a block (it becomes gates only when an inst line uses the block). */
static Status_t add_block_gate(Netlist_t *nl, uint8_t block, const GateSpec_t *spec)
{
    Status_t status = STATUS_BAD_ARG;

    if (block < nl->block_count)
    {
        Block_t *const b = &nl->blocks[block];
        uint32_t i = 0U;

        status = STATUS_OK;
        while ((status == STATUS_OK) && (i < (uint32_t)b->gate_count))
        {
            if (strcmp(b->gates[i].name, spec->name) == 0)
            {
                status = STATUS_DUPLICATE;
            }
            else if (strcmp(b->gates[i].out, spec->out) == 0)
            {
                status = STATUS_TWO_DRIVERS;
            }
            else
            {
                /* a different gate with a different output */
            }
            i++;
        }
        if ((status == STATUS_OK) && ((uint32_t)b->gate_count >= MAX_BLOCK_GATES))
        {
            status = STATUS_TABLE_FULL;
        }
        if (status == STATUS_OK)
        {
            b->gates[b->gate_count] = *spec;
            b->gate_count++;
        }
    }
    return status;
}

/* Gives the name of a block's net inside one instance */
static Status_t inner_net_name(const Block_t *block, const char *const connected[], const char *inst,
                               const char *name, char out[MAX_NAME_LEN])
{
    Status_t status = STATUS_OK;
    bool found = false;
    uint32_t p = 0U;

    while ((p < (uint32_t)block->port_count) && !found)
    {
        found = (strcmp(block->ports[p], name) == 0);
        if (!found)
        {
            p++;
        }
    }
    if (found)
    {
        status = tu_copy_text(out, MAX_NAME_LEN, connected[p]);
    }
    else if (is_constant_name(name))
    {
        status = tu_copy_text(out, MAX_NAME_LEN, name);
    }
    else
    {
        status = join_three(out, MAX_NAME_LEN, inst, INST_SEPARATOR, name);
    }
    return (status == STATUS_OK) ? STATUS_OK : STATUS_BAD_NAME;
}

/* Reads the PORT=NET words of an inst line: every port of the block exactly once. */
static Status_t connect_ports(const Block_t *block, char *const words[], uint32_t count,
                              const char *connected[], const char **out_bad)
{
    Status_t status = STATUS_OK;
    uint32_t i = IW_FIRST_PORT;
    uint32_t p;

    for (p = 0U; p < (uint32_t)block->port_count; p++)
    {
        connected[p] = NULL;
    }
    while ((status == STATUS_OK) && (i < count))
    {
        char *port = NULL;
        char *net = NULL;

        *out_bad = words[i];
        status = tu_split_assign(words[i], &port, &net);
        if (status == STATUS_OK)
        {
            bool found = false;

            p = 0U;
            while ((p < (uint32_t)block->port_count) && !found)
            {
                found = (strcmp(block->ports[p], port) == 0);
                if (!found)
                {
                    p++;
                }
            }
            status = (found && (connected[p] == NULL)) ? STATUS_OK : STATUS_BAD_PORT;
            if (status == STATUS_OK)
            {
                *out_bad = net;
                status = tu_check_name(net);
            }
            if (status == STATUS_OK)
            {
                connected[p] = net;
            }
        }
        i++;
    }
    p = 0U;
    while ((status == STATUS_OK) && (p < (uint32_t)block->port_count))
    {
        if (connected[p] == NULL)
        {
            *out_bad = block->ports[p];
            status = STATUS_BAD_PORT;
        }
        p++;
    }
    return status;
}

/* Expands an `inst INST BLOCK PORT=NET ...` line. */
static Status_t expand_inst(Netlist_t *nl, char *const words[], uint32_t count, char out_bad[MAX_LINE_LEN])
{
    const char *bad = words[0];
    bool bad_copied = false;
    Status_t status = STATUS_SYNTAX;

    if (count >= INST_MIN_WORDS)
    {
        uint8_t index = 0U;

        bad = words[IW_NAME];
        status = tu_check_name(words[IW_NAME]);
        if (status == STATUS_OK)
        {
            bad = words[IW_BLOCK];
            status = nl_find_block(nl, words[IW_BLOCK], &index);
        }
        if (status == STATUS_OK)
        {
            const Block_t *const block = &nl->blocks[index];
            const char *connected[MAX_BLOCK_PORTS];
            uint32_t g = 0U;

            status = connect_ports(block, words, count, connected, &bad);
            while ((status == STATUS_OK) && (g < (uint32_t)block->gate_count))
            {
                const GateSpec_t *const inner = &block->gates[g];
                GateSpec_t spec = *inner;
                uint32_t i = 0U;

                bad = words[IW_NAME];
                status = (join_three(spec.name, MAX_NAME_LEN, words[IW_NAME], INST_SEPARATOR, inner->name) ==
                          STATUS_OK) ? STATUS_OK : STATUS_BAD_NAME;
                if (status == STATUS_OK)
                {
                    status = inner_net_name(block, connected, words[IW_NAME], inner->out, spec.out);
                }
                while ((status == STATUS_OK) && (i < (uint32_t)inner->in_count))
                {
                    status = inner_net_name(block, connected, words[IW_NAME], inner->in[i], spec.in[i]);
                    i++;
                }
                if (status == STATUS_OK)
                {
                    status = nl_add_gate(nl, &spec);
                    if (status == STATUS_DUPLICATE)
                    {
                        (void)tu_copy_text(out_bad, MAX_LINE_LEN, spec.name);
                        bad_copied = true;
                    }
                    else if (status == STATUS_TWO_DRIVERS)
                    {
                        (void)tu_copy_text(out_bad, MAX_LINE_LEN, spec.out);
                        bad_copied = true;
                    }
                    else
                    {
                        bad = words[0];
                    }
                }
                g++;
            }
        }
        if (status == STATUS_OK)
        {
            nl->inst_count++;
        }
    }
    if ((status != STATUS_OK) && !bad_copied)
    {
        (void)tu_copy_text(out_bad, MAX_LINE_LEN, bad);
    }
    return status;
}

/* Check-only pass of an input or output line: every name, before any is added. */
static Status_t check_declare_names(const Netlist_t *nl, const char *const names[], uint32_t name_count,
                                    bool as_input, uint32_t *out_bad_name)
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
                status = STATUS_DUPLICATE;
            }
            else if (nl_find_net(nl, names[i], &index) == STATUS_OK)
            {
                if (as_input)
                {
                    status = input_conflict(&nl->nets[index]);
                }
                else
                {
                    status = output_conflict(&nl->nets[index]);
                }
            }
            else if (as_input && is_constant_name(names[i]))
            {
                status = STATUS_TWO_DRIVERS;
            }
            else
            {
                new_nets++;
            }
        }
        if (status != STATUS_OK)
        {
            *out_bad_name = i;
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
static Status_t parse_declare_line(Netlist_t *nl, const char *const words[], uint32_t count, bool as_input,
                                   uint32_t *out_bad_word)
{
    Status_t status = STATUS_SYNTAX;
    uint32_t bad_word = 0U;

    if (count >= DECLARE_MIN_WORDS)
    {
        const char *const *const names = &words[1];
        const uint32_t name_count = count - 1U;
        uint32_t bad_name = 0U;
        uint32_t i = 0U;

        status = check_declare_names(nl, names, name_count, as_input, &bad_name);
        if ((status != STATUS_OK) && (status != STATUS_TABLE_FULL))
        {
            bad_word = bad_name + 1U;
        }
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
    if (status != STATUS_OK)
    {
        *out_bad_word = bad_word;
    }
    return status;
}

/* Handles a gate line: into the open block, or into the circuit. */
static Status_t parse_gate_line(Netlist_t *nl, const NlParseState_t *state, char *const words[], uint32_t count,
                                uint32_t *out_bad_word)
{
    GateSpec_t spec;
    Status_t status = parse_gate_words(words, count, &spec, out_bad_word);

    if (status == STATUS_OK)
    {
        status = state->in_block ? add_block_gate(nl, state->block, &spec) : nl_add_gate(nl, &spec);
        if (status == STATUS_TWO_DRIVERS)
        {
            *out_bad_word = GW_OUT;
        }
        else if (status == STATUS_DUPLICATE)
        {
            *out_bad_word = GW_NAME;
        }
        else
        {
            *out_bad_word = 0U;
        }
    }
    return status;
}

/* Handles "ends": closes the open block, which must have at least one gate. */
static Status_t parse_ends_line(const Netlist_t *nl, NlParseState_t *state, uint32_t count)
{
    Status_t status = STATUS_OK;

    if (count != ENDS_WORDS)
    {
        status = STATUS_SYNTAX;
    }
    else if (!state->in_block)
    {
        status = STATUS_NESTING;
    }
    else if (nl->blocks[state->block].gate_count == 0U)
    {
        status = STATUS_SYNTAX;
    }
    else
    {
        state->in_block = false;
    }
    return status;
}

/* Handles "include PATH": the path is kept in the state, nl_load opens the file. */
static Status_t parse_include_line(NlParseState_t *state, char *const words[], uint32_t count,
                                   uint32_t *out_bad_word)
{
    Status_t status = STATUS_SYNTAX;

    if (count == INCLUDE_WORDS)
    {
        *out_bad_word = INCLUDE_W_PATH;
        status = (tu_copy_text(state->include_path, MAX_PATH_LEN, words[INCLUDE_W_PATH]) == STATUS_OK) ?
                 STATUS_OK : STATUS_SYNTAX;
        state->include_requested = (status == STATUS_OK);
    }
    else
    {
        *out_bad_word = 0U;
    }
    return status;
}

Status_t nl_parse_line(Netlist_t *nl, NlParseState_t *state, char *line, Diag_t *out_diag)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (state != NULL) && (line != NULL) && (out_diag != NULL))
    {
        char *words[MAX_WORDS];
        char inst_bad[MAX_LINE_LEN];
        uint32_t count = 0U;
        uint32_t bad_word = 0U;
        bool bad_is_inst = false;
        NetKeyword_t keyword = KW_INPUT;

        status = tu_cut_comment(line);
        if (status == STATUS_OK)
        {
            status = tu_split_words(line, words, MAX_WORDS, &count);
        }
        if ((status == STATUS_OK) && (count > 0U))
        {
            status = find_keyword(words[0], &keyword);
            if ((status == STATUS_OK) && state->in_block && (keyword != KW_GATE) && (keyword != KW_ENDS))
            {
                status = STATUS_NESTING;
            }
            if (status == STATUS_OK)
            {
                switch (keyword)
                {
                    case KW_INPUT:
                        status = parse_declare_line(nl, (const char *const *)words, count, true, &bad_word);
                        break;
                    case KW_OUTPUT:
                        status = parse_declare_line(nl, (const char *const *)words, count, false, &bad_word);
                        break;
                    case KW_GATE:
                        status = parse_gate_line(nl, state, words, count, &bad_word);
                        break;
                    case KW_SUBCKT:
                        status = begin_block(nl, words, count, &state->block, &bad_word);
                        state->in_block = (status == STATUS_OK);
                        break;
                    case KW_ENDS:
                        status = parse_ends_line(nl, state, count);
                        break;
                    case KW_INST:
                        status = expand_inst(nl, words, count, inst_bad);
                        bad_is_inst = true;
                        break;
                    case KW_INCLUDE:
                        status = parse_include_line(state, words, count, &bad_word);
                        break;
                    default:
                        status = STATUS_UNKNOWN_KEYWORD;
                        break;
                }
            }
            if (status != STATUS_OK)
            {
                set_line_diag(out_diag, status, bad_is_inst ? inst_bad : words[bad_word]);
            }
        }
        else if (status != STATUS_OK)
        {
            set_line_diag(out_diag, status, NULL);
        }
        else
        {
            /* an empty or comment-only line */
        }
    }
    return status;
}

/* Checks the whole circuit after its last line (called by nl_load). */
static Status_t check_circuit(Netlist_t *nl, Diag_t *out_diag)
{
    Status_t status = STATUS_OK;
    uint16_t unused[MAX_WARNINGS];
    uint32_t warnings = 0U;
    uint32_t i = 0U;

    while ((i < (uint32_t)nl->net_count) && (status == STATUS_OK))
    {
        const Net_t *net = &nl->nets[i];

        if ((net->is_read || net->is_output) && (net->driver == DRV_NONE))
        {
            status = STATUS_UNDRIVEN;
            set_line_diag(out_diag, status, net->name);
        }
        else if ((net->driver == DRV_GATE) && !net->is_read && !net->is_output)
        {
            if (warnings < MAX_WARNINGS)
            {
                unused[warnings] = (uint16_t)i;
            }
            warnings++;
        }
        else
        {
            /* driven and used, or an input that nothing reads */
        }
        i++;
    }
    if (status == STATUS_OK)
    {
        const uint32_t kept = (warnings < MAX_WARNINGS) ? warnings : MAX_WARNINGS;

        for (i = 0U; i < kept; i++)
        {
            nl->unused[i] = unused[i];
        }
        nl->warning_count = (uint16_t)warnings;
    }
    return status;
}

/* Checks the end of one file of a load: a block must not go on past the end of its file. */
static Status_t end_of_file(const Netlist_t *nl, const NlParseState_t *state, Diag_t *out_diag)
{
    Status_t status = STATUS_OK;

    if (state->in_block)
    {
        char what[MAX_LINE_LEN];

        status = STATUS_NESTING;
        if (join_three(what, MAX_LINE_LEN, SUBCKT_KEYWORD " ", nl->blocks[state->block].name, NOT_CLOSED_TEXT) ==
            STATUS_OK)
        {
            set_line_diag(out_diag, status, what);
        }
        else
        {
            set_line_diag(out_diag, status, NULL);
        }
    }
    return status;
}

/* Opens an included file as the next reader of the stack. */
static Status_t open_include(LineReader_t readers[], uint32_t *depth, const char *include_path)
{
    Status_t status = STATUS_NESTING;

    if (*depth < MAX_INCLUDE_DEPTH)
    {
        char full_path[MAX_PATH_LEN];

        status = tu_join_path(readers[*depth - 1U].path, include_path, full_path, MAX_PATH_LEN);
        if (status == STATUS_OK)
        {
            status = tu_open(&readers[*depth], full_path);
        }
        if (status == STATUS_OK)
        {
            (*depth)++;
        }
        else
        {
            status = STATUS_FILE_OPEN;
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

Status_t nl_parse_gate_words(char *const words[], uint32_t count, GateSpec_t *out_spec)
{
    Status_t status = STATUS_NULL_ARG;

    if ((words != NULL) && (out_spec != NULL))
    {
        uint32_t bad_word = 0U;

        status = parse_gate_words(words, count, out_spec, &bad_word);
    }
    return status;
}

Status_t nl_load(Netlist_t *nl, const char *path, Diag_t *out_diag)
{
    Status_t status = STATUS_NULL_ARG;

    if ((nl != NULL) && (path != NULL) && (out_diag != NULL))
    {
        LineReader_t readers[MAX_INCLUDE_DEPTH];
        uint32_t depth = 0U;
        NlParseState_t state;
        Diag_t diag;
        const char *error_file = path;
        uint32_t error_line = 0U;

        clear_tables(nl);
        (void)memset(&state, 0, sizeof(state));
        status = tu_open(&readers[0], path);
        if (status != STATUS_OK)
        {
            set_line_diag(&diag, status, NULL);
        }
        else
        {
            depth = 1U;
        }
        while ((status == STATUS_OK) && (depth > 0U))
        {
            LineReader_t *const top = &readers[depth - 1U];
            char line[MAX_LINE_LEN];
            const Status_t read_status = tu_read_line(top, line, MAX_LINE_LEN);

            if (read_status == STATUS_END_OF_FILE)
            {
                status = end_of_file(nl, &state, &diag);
                if (status != STATUS_OK)
                {
                    error_file = top->path;
                    error_line = top->line_no;
                }
                if ((tu_close(top) != STATUS_OK) && (status == STATUS_OK))
                {
                    status = STATUS_FILE_READ;
                    set_line_diag(&diag, status, NULL);
                    error_file = top->path;
                }
                depth--;
            }
            else if (read_status != STATUS_OK)
            {
                status = read_status;
                set_line_diag(&diag, status, NULL);
            }
            else
            {
                status = nl_parse_line(nl, &state, line, &diag);
                if ((status == STATUS_OK) && state.include_requested)
                {
                    state.include_requested = false;
                    status = open_include(readers, &depth, state.include_path);
                    if (status != STATUS_OK)
                    {
                        set_line_diag(&diag, status, (status == STATUS_NESTING) ? INCLUDE_KEYWORD : state.include_path);
                    }
                }
            }
            if ((status != STATUS_OK) && (read_status != STATUS_END_OF_FILE))
            {
                error_file = top->path;
                error_line = top->line_no;
            }
        }
        while (depth > 0U)
        {
            (void)tu_close(&readers[depth - 1U]);
            depth--;
        }
        if (status == STATUS_OK)
        {
            status = check_circuit(nl, &diag);
        }
        if (status != STATUS_OK)
        {
            (void)tu_copy_text(diag.file, MAX_PATH_LEN, error_file);
            diag.line = error_line;
            *out_diag = diag;
        }
    }
    return status;
}

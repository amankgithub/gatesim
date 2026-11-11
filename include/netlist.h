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

typedef enum
{
    DRV_NONE = 0,
    DRV_INPUT,
    DRV_GATE,
    DRV_CONST
} DriverKind_t;

typedef struct
{
    char         name[MAX_NAME_LEN];
    DriverKind_t driver;
    bool         is_output;
    bool         is_read;
    LogicValue_t init_value;
    LogicValue_t value;
} Net_t;

typedef struct
{
    char         name[MAX_NAME_LEN];
    GateType_t   type;
    uint8_t      in_count;
    uint16_t     in[MAX_GATE_INPUTS];
    uint16_t     out;
    uint16_t     rise;
    uint16_t     fall;
    bool         has_pending;
    LogicValue_t pending;
} Gate_t;

typedef struct
{
    char       name[MAX_NAME_LEN];
    GateType_t type;
    uint16_t   rise;
    uint16_t   fall;
    char       out[MAX_NAME_LEN];
    uint8_t    in_count;
    char       in[MAX_GATE_INPUTS][MAX_NAME_LEN];
} GateSpec_t;

typedef struct
{
    char       name[MAX_NAME_LEN];
    uint8_t    port_count;
    uint8_t    first_output_port;
    char       ports[MAX_BLOCK_PORTS][MAX_NAME_LEN];
    uint8_t    gate_count;
    GateSpec_t gates[MAX_BLOCK_GATES];
} Block_t;

typedef struct
{
    bool    in_block;
    uint8_t block;
    bool    include_requested;
    char    include_path[MAX_PATH_LEN];
} NlParseState_t;

typedef struct
{
    Net_t    nets[MAX_NETS];
    uint16_t net_count;
    Gate_t   gates[MAX_GATES];
    uint16_t gate_count;
    Block_t  blocks[MAX_BLOCKS];
    uint8_t  block_count;
    uint16_t inst_count;
    uint16_t input_count;
    uint16_t output_count;
    uint16_t unused[MAX_WARNINGS];
    uint16_t warning_count;
} Netlist_t;

/* Empties both tables. */
Status_t nl_init(Netlist_t *nl);

/* Finds a net by name (linear search). */
Status_t nl_find_net(const Netlist_t *nl, const char *name, uint16_t *out_index);

/* Finds a gate by name (linear search). */
Status_t nl_find_gate(const Netlist_t *nl, const char *name, uint16_t *out_index);

/* Finds a net by name, or adds it at the end. */
Status_t nl_find_or_add_net(Netlist_t *nl, const char *name, uint16_t *out_index);

/* Declares one input net (found or added) and counts it in input_count. */
Status_t nl_add_input(Netlist_t *nl, const char *name);

/* Reads a delay word: "#d" (the same delay both ways) or "#r,f" (rise r, fall f). */
Status_t nl_parse_delay(const char *word, uint16_t *out_rise, uint16_t *out_fall);

/* Checks the words of a gate line and fills a GateSpec_t. */
Status_t nl_parse_gate_words(char *const words[], uint32_t count, GateSpec_t *out_spec);

/* Adds one gate: its output and input nets are found or added, the inputs are marked as read. */
Status_t nl_add_gate(Netlist_t *nl, const GateSpec_t *spec);

/* Finds a block by name (linear search). */
Status_t nl_find_block(const Netlist_t *nl, const char *name, uint8_t *out_block);

/* Handles one line of a .net file: input, output, gate, subckt, ends, inst or include. */
Status_t nl_parse_line(Netlist_t *nl, NlParseState_t *state, char *line, Diag_t *out_diag);

/* Reads a whole .net file and the file it includes into the tables */
Status_t nl_load(Netlist_t *nl, const char *path, Diag_t *out_diag);

#ifdef __cplusplus
}
#endif

#endif

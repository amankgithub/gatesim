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
    DRV_GATE
} DriverKind_t;

typedef struct
{
    char         name[MAX_NAME_LEN];
    DriverKind_t driver;
    bool         is_output;
    LogicValue_t value;
} Net_t;

typedef struct
{
    char         name[MAX_NAME_LEN];
    GateType_t   type;
    uint8_t      in_count;
    uint16_t     in[MAX_GATE_INPUTS];
    uint16_t     out;
    uint16_t     delay;
    LogicValue_t scheduled;
} Gate_t;

typedef struct
{
    char       name[MAX_NAME_LEN];
    GateType_t type;
    uint16_t   delay;
    char       out[MAX_NAME_LEN];
    uint8_t    in_count;
    char       in[MAX_GATE_INPUTS][MAX_NAME_LEN];
} GateSpec_t;

typedef struct
{
    Net_t    nets[MAX_NETS];
    uint16_t net_count;
    Gate_t   gates[MAX_GATES];
    uint16_t gate_count;
} Netlist_t;

/* Empties both tables. */
Status_t nl_init(Netlist_t *nl);

/* Finds a net by name (linear search). */
Status_t nl_find_net(const Netlist_t *nl, const char *name, uint16_t *out_index);

/* Finds a net by name, or adds it at the end. A new net has no driver and the value 0. */
Status_t nl_find_or_add_net(Netlist_t *nl, const char *name, uint16_t *out_index);

/* Declares one input net (found or added). */
Status_t nl_add_input(Netlist_t *nl, const char *name);

/* Reads a delay word "#d". */
Status_t nl_parse_delay(const char *word, uint16_t *out_delay);

/* Checks the words of a gate line and fills a GateSpec_t. */
Status_t nl_parse_gate_words(char *const words[], uint32_t count, GateSpec_t *out_spec);

/* Adds one gate: its output and input nets are found or added. */
Status_t nl_add_gate(Netlist_t *nl, const GateSpec_t *spec);

/* Handles one line of a .net file: input, output or gate. */
Status_t nl_parse_line(Netlist_t *nl, char *line);

/* Reads a whole .net file into the tables (they are emptied first). */
Status_t nl_load(Netlist_t *nl, const char *path, uint32_t *out_line);

#ifdef __cplusplus
}
#endif

#endif

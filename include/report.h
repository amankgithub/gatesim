#ifndef REPORT_H
#define REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "netlist.h"
#include "sim_kernel.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RW_VCD_CODE_SIZE    3U

typedef struct
{
    FILE            *fp;
    const Netlist_t *nl;
    bool             failed;
    uint32_t         last_time;
} VcdWriter_t;

/* Gives the short VCD code of a net: net i < 94 gets one character (33 + i), the others two characters */
Status_t rw_vcd_code(uint16_t net, char *out, uint32_t out_size);

/* Creates a VCD file and writes its header: version, timescale 1 ns, one $var line per net */
Status_t rw_vcd_open(VcdWriter_t *w, const Netlist_t *nl, const char *path);

/* Gives the kernel listener that writes every net change into the VCD file. */
Status_t rw_vcd_listener(VcdWriter_t *w, SkListener_t *out_listener);

/* Closes the VCD file (safe to call twice). */
Status_t rw_vcd_close(VcdWriter_t *w);

#ifdef __cplusplus
}
#endif

#endif

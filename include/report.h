/**
 * @file    report.h
 * @brief   The VCD waveform writer: <dir>/waves.vcd, written while the kernel runs.
 * @details The VCD writer is the kernel's listener: every net change arrives through on_change
 *          and is written at once, so no change has to be kept in memory. File output uses
 *          <stdio.h>: a recorded MISRA C:2012 Rule 21.6 deviation.
 */
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

/** Size of a buffer for a VCD net code: at most two characters and the '\0'. */
#define RW_VCD_CODE_SIZE    3U

/** @brief An open VCD file being written (owned by the caller, passed by pointer). */
typedef struct
{
    FILE            *fp;          /**< the open file, NULL when closed */
    const Netlist_t *nl;          /**< the circuit of the header (a change of any other net is not written) */
    bool             failed;      /**< a write failed: nothing more is written, close gives FILE_WRITE */
    uint32_t         last_time;   /**< the last "#time" line written, ns */
} VcdWriter_t;

/**
 * @brief   Gives the short VCD code of a net: net i < 94 gets one character (33 + i), the others two characters
 * @details The two characters of net i >= 94 are 33 + j / 94 and 33 + j % 94, with j = i - 94.
 * @param   net       The net index, 0..MAX_NETS-1.
 * @param   out       Out: the code text, written only on STATUS_OK.
 * @param   out_size  Size of out in bytes (RW_VCD_CODE_SIZE is always enough).
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG for a net >= MAX_NETS or an out_size
 *          too small for the code (out unchanged).
 */
Status_t rw_vcd_code(uint16_t net, char *out, uint32_t out_size);

/**
 * @brief   Creates a VCD file and writes its header: version, timescale 1 ns, one $var line per net
 * @details The $var lines are in net order. Then, at time 0, every net with its start value
 *          ($dumpvars: x, or 1 for VCC and 0 for GND).
 * @param   w     Out: the writer, filled only on STATUS_OK.
 * @param   nl    The loaded netlist (kept by pointer).
 * @param   path  The file to create (overwritten if it exists).
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_FILE_OPEN if the file cannot be created
 *          (for example a missing folder); STATUS_FILE_WRITE if the header cannot be written
 *          (the file is closed again).
 */
Status_t rw_vcd_open(VcdWriter_t *w, const Netlist_t *nl, const char *path);

/**
 * @brief   Gives the kernel listener that writes every net change into the VCD file.
 * @details on_change writes "#now" when the time differs from the last time written, then
 *          the new value and the net code ("1!"); on_end writes "#stop_time" when the run stopped
 *          later than the last time written, so the file covers the whole run. After a failed
 *          write nothing more is written.
 * @param   w             The open writer (it is the listener's ctx, so it must stay valid during the run).
 * @param   out_listener  Out: the listener for sk_set_listener, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG.
 */
Status_t rw_vcd_listener(VcdWriter_t *w, SkListener_t *out_listener);

/**
 * @brief   Closes the VCD file (safe to call twice).
 * @param   w  In/out: the writer; fp becomes NULL.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_FILE_WRITE if any write or the close failed.
 */
Status_t rw_vcd_close(VcdWriter_t *w);

#ifdef __cplusplus
}
#endif

#endif /* REPORT_H */

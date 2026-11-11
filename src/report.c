/**
 * @file    report.c
 * @brief   The VCD waveform writer: the header with every net, then each net change as the kernel reports it.
 * @details Uses <stdio.h>: a recorded MISRA C:2012 Rule 21.6 deviation.
 *          Every write to the file is checked; the first failure is remembered (failed) and
 *          reported as FILE_WRITE by rw_vcd_close, because the listener functions return nothing.
 */
#include "report.h"

#include <inttypes.h>
#include <stddef.h>

#include "logic.h"

/**
 * @brief   Gives the code of a net (worker of rw_vcd_code; the caller checked net and out_size).
 * @param   net  The net index, below MAX_NETS.
 * @param   out  Out: at least RW_VCD_CODE_SIZE bytes.
 */
static void vcd_code(uint16_t net, char out[RW_VCD_CODE_SIZE])
{
    const uint32_t i = (uint32_t)net;

    if (i < VCD_CODE_COUNT)
    {
        const uint32_t c = VCD_FIRST_CODE + i;

        out[0] = (char)c;
        out[1] = '\0';
    }
    else
    {
        /* two characters: the first counts the blocks of 94, the second is the place in the block */
        const uint32_t j = i - VCD_CODE_COUNT;
        const uint32_t c1 = VCD_FIRST_CODE + (j / VCD_CODE_COUNT);
        const uint32_t c2 = VCD_FIRST_CODE + (j % VCD_CODE_COUNT);

        out[0] = (char)c1;
        out[1] = (char)c2;
        out[2] = '\0';
    }
}

/**
 * @brief   Number of characters of a net's code.
 * @param   net  The net index.
 * @return  1 or 2.
 */
static uint32_t vcd_code_length(uint16_t net)
{
    return ((uint32_t)net < VCD_CODE_COUNT) ? 1U : 2U;
}

/**
 * @brief   Writes the header, the $var line of every net and every start value at time 0.
 * @param   fp  The open file.
 * @param   nl  The netlist.
 * @return  true if every write succeeded.
 */
static bool write_header(FILE *fp, const Netlist_t *nl)
{
    /* no $date line: that would need <time.h> */
    bool ok = (fprintf(fp, "$version GateSim %s $end\n$timescale 1ns $end\n$scope module gatesim $end\n",
                       GATESIM_VERSION) >= 0);
    uint32_t i = 0U;

    while (ok && (i < (uint32_t)nl->net_count))
    {
        char code[RW_VCD_CODE_SIZE];

        vcd_code((uint16_t)i, code);
        ok = (fprintf(fp, "$var wire 1 %s %s $end\n", code, nl->nets[i].name) >= 0);
        i++;
    }
    if (ok)
    {
        /* the start values belong to time 0 */
        ok = (fputs("$upscope $end\n$enddefinitions $end\n#0\n$dumpvars\n", fp) >= 0);
    }
    /* every net starts with its start value: X until something drives it, 1 for VCC, 0 for GND */
    i = 0U;
    while (ok && (i < (uint32_t)nl->net_count))
    {
        char code[RW_VCD_CODE_SIZE];

        vcd_code((uint16_t)i, code);
        ok = (fprintf(fp, "%c%s\n", lg_value_char(nl->nets[i].init_value), code) >= 0);
        i++;
    }
    if (ok)
    {
        ok = (fputs("$end\n", fp) >= 0);
    }
    return ok;
}

/**
 * @brief   Kernel listener: writes one net change ("#time" first when the time is new).
 * @param   ctx        The VcdWriter_t given by rw_vcd_listener.
 * @param   net        The net that changed.
 * @param   now        The time of the change, ns.
 * @param   old_value  The value before (not needed: a VCD lists only new values).
 * @param   new_value  The new value.
 */
static void vcd_on_change(void *ctx, uint16_t net, uint32_t now, LogicValue_t old_value, LogicValue_t new_value)
{
    /* the kernel hands back the ctx of rw_vcd_listener, which is a VcdWriter_t (MISRA 11.5 deviation) */
    /* cppcheck-suppress misra-c2012-11.5 ; listener context, MISRA deviation */
    VcdWriter_t *const w = (VcdWriter_t *)ctx;

    (void)old_value;
    /* only a net of the circuit in the header has a code in the file */
    if ((w != NULL) && (w->fp != NULL) && !w->failed && (net < w->nl->net_count))
    {
        char code[RW_VCD_CODE_SIZE];

        vcd_code(net, code);
        /* changes at 0 follow the $dumpvars block, which already belongs to #0 */
        if (now != w->last_time)
        {
            if (fprintf(w->fp, "#%" PRIu32 "\n", now) < 0)
            {
                w->failed = true;
            }
            else
            {
                w->last_time = now;
            }
        }
        if (!w->failed && (fprintf(w->fp, "%c%s\n", lg_value_char(new_value), code) < 0))
        {
            w->failed = true;
        }
    }
}

/**
 * @brief   Kernel listener: writes "#stop_time" at the end of a run when it is later than the last time written
 * @details So the waveform covers the whole run: a run that stops by its end time after a quiet
 *          stretch still shows that stretch.
 * @param   ctx     The VcdWriter_t given by rw_vcd_listener.
 * @param   result  The result of the run.
 */
static void vcd_on_end(void *ctx, const SimResult_t *result)
{
    /* the same ctx as vcd_on_change: a VcdWriter_t (MISRA 11.5 deviation) */
    /* cppcheck-suppress misra-c2012-11.5 ; listener context, MISRA deviation */
    VcdWriter_t *const w = (VcdWriter_t *)ctx;

    if ((w != NULL) && (result != NULL) && (w->fp != NULL) && !w->failed && (result->stop_time > w->last_time))
    {
        if (fprintf(w->fp, "#%" PRIu32 "\n", result->stop_time) < 0)
        {
            w->failed = true;
        }
        else
        {
            w->last_time = result->stop_time;
        }
    }
}

Status_t rw_vcd_code(uint16_t net, char *out, uint32_t out_size)
{
    Status_t status = STATUS_OK;

    if (out == NULL)
    {
        status = STATUS_NULL_ARG;
    }
    else if (((uint32_t)net >= MAX_NETS) || (out_size <= vcd_code_length(net)))
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        char code[RW_VCD_CODE_SIZE];
        uint32_t i;

        vcd_code(net, code);
        for (i = 0U; i <= vcd_code_length(net); i++)
        {
            out[i] = code[i];
        }
    }
    return status;
}

Status_t rw_vcd_open(VcdWriter_t *w, const Netlist_t *nl, const char *path)
{
    Status_t status = STATUS_OK;

    if ((w == NULL) || (nl == NULL) || (path == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        FILE *const fp = fopen(path, "w");

        if (fp == NULL)
        {
            status = STATUS_FILE_OPEN;
        }
        else if (!write_header(fp, nl))
        {
            /* the file is useless without its header; the close result changes nothing */
            (void)fclose(fp);
            status = STATUS_FILE_WRITE;
        }
        else
        {
            w->fp = fp;
            w->nl = nl;
            w->failed = false;
            w->last_time = 0U;
        }
    }
    return status;
}

Status_t rw_vcd_listener(VcdWriter_t *w, SkListener_t *out_listener)
{
    Status_t status = STATUS_NULL_ARG;

    if ((w != NULL) && (out_listener != NULL))
    {
        /* the kernel hands w back to both functions as their ctx */
        out_listener->ctx = w;
        out_listener->on_change = vcd_on_change;
        out_listener->on_end = vcd_on_end;
        status = STATUS_OK;
    }
    return status;
}

Status_t rw_vcd_close(VcdWriter_t *w)
{
    Status_t status = STATUS_NULL_ARG;

    if (w != NULL)
    {
        /* fclose also writes what is still buffered, so its failure is a write failure */
        if ((w->fp != NULL) && (fclose(w->fp) != 0))
        {
            w->failed = true;
        }
        w->fp = NULL;
        status = w->failed ? STATUS_FILE_WRITE : STATUS_OK;
    }
    return status;
}

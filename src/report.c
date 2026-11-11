#include "report.h"

#include <inttypes.h>
#include <stddef.h>

#include "logic.h"

/* Gives the code of a net (worker of rw_vcd_code; the caller checked net and out_size). */
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
        const uint32_t j = i - VCD_CODE_COUNT;
        const uint32_t c1 = VCD_FIRST_CODE + (j / VCD_CODE_COUNT);
        const uint32_t c2 = VCD_FIRST_CODE + (j % VCD_CODE_COUNT);

        out[0] = (char)c1;
        out[1] = (char)c2;
        out[2] = '\0';
    }
}

/* Number of characters of a net's code. */
static uint32_t vcd_code_length(uint16_t net)
{
    return ((uint32_t)net < VCD_CODE_COUNT) ? 1U : 2U;
}

/* Writes the header, the $var line of every net and every start value at time 0. */
static bool write_header(FILE *fp, const Netlist_t *nl)
{
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
        ok = (fputs("$upscope $end\n$enddefinitions $end\n#0\n$dumpvars\n", fp) >= 0);
    }
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

/* Kernel listener: writes one net change ("#time" first when the time is new). */
static void vcd_on_change(void *ctx, uint16_t net, uint32_t now, LogicValue_t old_value, LogicValue_t new_value)
{
    VcdWriter_t *const w = (VcdWriter_t *)ctx;

    (void)old_value;
    if ((w != NULL) && (w->fp != NULL) && !w->failed && (net < w->nl->net_count))
    {
        char code[RW_VCD_CODE_SIZE];

        vcd_code(net, code);
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
        out_listener->ctx = w;
        out_listener->on_change = vcd_on_change;
        status = STATUS_OK;
    }
    return status;
}

Status_t rw_vcd_close(VcdWriter_t *w)
{
    Status_t status = STATUS_NULL_ARG;

    if (w != NULL)
    {
        if ((w->fp != NULL) && (fclose(w->fp) != 0))
        {
            w->failed = true;
        }
        w->fp = NULL;
        status = w->failed ? STATUS_FILE_WRITE : STATUS_OK;
    }
    return status;
}

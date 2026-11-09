/**
 * @file    status.c
 * @brief   Readable texts of the Status_t codes.
 */
#include "status.h"

/** Text shown for a code outside the table. */
#define STATUS_UNKNOWN_TEXT "unknown status"

const char *status_text(Status_t status)
{
    /* One text per code, in the order of Status_t (read-only, so it lives in a const table). */
    static const char *const STATUS_TEXTS[STATUS_COUNT] =
    {
        "ok",
        "null pointer argument",
        "argument out of range"
    };
    const char *text = STATUS_UNKNOWN_TEXT;

    if (status < STATUS_COUNT)
    {
        text = STATUS_TEXTS[status];
    }
    return text;
}

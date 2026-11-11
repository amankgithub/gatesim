/**
 * @file    status.h
 * @brief   One status type for every GateSim module, with a readable text per code.
 * @details Every function that can fail returns a Status_t. New codes are always
 *          appended just before STATUS_COUNT, so existing values never change.
 *          A reader that fails also fills a Diag_t, the message shown to the user.
 */
#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of every GateSim function that can fail. */
typedef enum
{
    STATUS_OK = 0,          /**< "ok" */
    STATUS_NULL_ARG,        /**< "null pointer argument" */
    STATUS_BAD_ARG,         /**< "argument out of range" */
    STATUS_UNKNOWN_TYPE,    /**< "unknown gate type" */
    STATUS_INPUT_COUNT,     /**< "wrong number of inputs for this gate type" */
    STATUS_TOO_MANY_WORDS,  /**< "too many words on the line" */
    STATUS_BAD_NUMBER,      /**< "not a valid number" */
    STATUS_BAD_NAME,        /**< "not a valid name" */
    STATUS_FILE_OPEN,       /**< "cannot open file" */
    STATUS_FILE_READ,       /**< "cannot read file" */
    STATUS_END_OF_FILE,     /**< "end of file" */
    STATUS_LINE_TOO_LONG,   /**< "line too long" */
    STATUS_TABLE_FULL,      /**< "table full" */
    STATUS_DUPLICATE,       /**< "name already used" */
    STATUS_NOT_FOUND,       /**< "name not found" */
    STATUS_TWO_DRIVERS,     /**< "net already has a driver" */
    STATUS_SYNTAX,          /**< "syntax error" */
    STATUS_UNKNOWN_KEYWORD, /**< "unknown keyword" */
    STATUS_QUEUE_FULL,      /**< "event list full" */
    STATUS_QUEUE_EMPTY,     /**< "event list empty" */
    STATUS_TIME_OVERFLOW,   /**< "time overflow" */
    STATUS_NOT_INPUT,       /**< "net is not an input" */
    STATUS_NO_END,          /**< "missing end line" */
    STATUS_UNDRIVEN,        /**< "net is read but never driven" */
    STATUS_FILE_WRITE,      /**< "cannot write file" */
    STATUS_COUNT            /**< number of codes, keep last */
} Status_t;

/** @brief One error message for the user: "file:line: text". */
typedef struct
{
    char     file[MAX_PATH_LEN];   /**< the file, "" when not tied to a file */
    uint32_t line;                 /**< the line, 1-based; 0 when not tied to a line */
    char     text[MAX_MSG_LEN];    /**< status text, often followed by the offending word in quotes */
} Diag_t;

/**
 * @brief   Gives the readable text of a status code (for error messages).
 * @param   status  Any value; codes outside 0..STATUS_COUNT-1 are allowed.
 * @return  A fixed phrase such as "ok"; "unknown status" for an out-of-range code.
 *          The text is never NULL and must not be changed by the caller.
 */
const char *status_text(Status_t status);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_H */

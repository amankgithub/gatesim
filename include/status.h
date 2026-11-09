/**
 * @file    status.h
 * @brief   One status type for every GateSim module, with a readable text per code.
 * @details Every function that can fail returns a Status_t. New codes are always
 *          appended just before STATUS_COUNT, so existing values never change.
 */
#ifndef STATUS_H
#define STATUS_H

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
    STATUS_COUNT            /**< number of codes, keep last */
} Status_t;

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

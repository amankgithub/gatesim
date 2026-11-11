#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    FILE     *fp;
    uint32_t  line_no;
} LineReader_t;

/* Opens a text file for reading. */
Status_t tu_open(LineReader_t *reader, const char *path);

/* Reads the next line without its '\n' and a trailing '\r' (CRLF files). */
Status_t tu_read_line(LineReader_t *reader, char *buf, uint32_t buf_size);

/* Closes the file of a reader (safe to call on a closed reader). */
Status_t tu_close(LineReader_t *reader);

/* Cuts the comment off a line: everything from the first '#' that is not followed by a digit is removed */
Status_t tu_cut_comment(char *line);

/* Splits a line into words separated by spaces and tabs, in place. */
Status_t tu_split_words(char *line, char *words[], uint32_t max_words, uint32_t *out_count);

/* Converts a decimal number with a range check (instead of atoi, MISRA 21.7). */
Status_t tu_parse_u32(const char *text, uint32_t min_value, uint32_t max_value, uint32_t *out_value);

/* Checks the length of a name: 1..MAX_NAME_LEN-1 characters. */
Status_t tu_check_name(const char *name);

/* Copies a text into a buffer of known size (bounded, never strcpy). */
Status_t tu_copy_text(char *dst, uint32_t dst_size, const char *src);

/* Fills an error message: text = what, or what + " '" + name + "'". */
Status_t tu_set_diag(Diag_t *diag, const char *file, uint32_t line, const char *what, const char *name);

/* Builds the path of a file in a folder: dir + "/" + file_name ("output/waves.vcd"). */
Status_t tu_join_dir(const char *dir, const char *file_name, char *out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file    text_utils.h
 * @brief   Text helpers: reading the input files (lines, comments, words, numbers, names, PORT=NET words),
 *          error messages, paths.
 * @details The netlist reader only sees words; every number and every name goes
 *          through the one checked function here. File reading uses <stdio.h>:
 *          a recorded MISRA C:2012 Rule 21.6 deviation.
 */
#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An open text file read line by line (owned by the caller). */
typedef struct
{
    FILE     *fp;                  /**< open file, NULL when closed */
    char      path[MAX_PATH_LEN];  /**< the file name, for error messages and as the base of relative include paths */
    uint32_t  line_no;             /**< number of the line last read, 1-based (0 before the first) */
} LineReader_t;

/**
 * @brief   Opens a text file for reading and keeps its path.
 * @param   reader  Out: filled only on STATUS_OK (fp open, path copied, line_no 0).
 * @param   path    Path of the file, relative to the working folder.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if the path does not fit MAX_PATH_LEN;
 *          STATUS_FILE_OPEN if fopen fails (also for an empty path).
 */
Status_t tu_open(LineReader_t *reader, const char *path);

/**
 * @brief   Reads the next line without its '\n' and a trailing '\r' (CRLF files).
 * @param   reader    In/out: an open reader; line_no counts every line read,
 *                    also a line that is too long (for the error message).
 * @param   buf       Out: the line, written only on STATUS_OK.
 * @param   buf_size  Size of buf, 2..MAX_LINE_LEN.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG (reader not open, bad buf_size);
 *          STATUS_END_OF_FILE when no line is left; STATUS_LINE_TOO_LONG if the line
 *          does not fit buf; STATUS_FILE_READ on a read error.
 */
Status_t tu_read_line(LineReader_t *reader, char *buf, uint32_t buf_size);

/**
 * @brief   Closes the file of a reader (safe to call on a closed reader).
 * @param   reader  In/out: fp becomes NULL.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_FILE_READ if fclose fails.
 */
Status_t tu_close(LineReader_t *reader);

/**
 * @brief   Cuts the comment off a line: everything from the first '#' that is not
 *          followed by a digit is removed ("#2" is a delay word, "# x" is a comment).
 * @param   line  The line, changed in place ('\0' written over that '#').
 * @return  STATUS_OK, or STATUS_NULL_ARG.
 */
Status_t tu_cut_comment(char *line);

/**
 * @brief   Splits a line into words separated by spaces and tabs, in place.
 * @param   line       The line; a '\0' is written after each word.
 * @param   words      Out: pointers to the words inside line (max_words entries).
 * @param   max_words  Size of words[], at least 1.
 * @param   out_count  Out: number of words (0 for an empty or blank line).
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if max_words is 0;
 *          STATUS_TOO_MANY_WORDS if the line has more than max_words words
 *          (then line, words and out_count are unchanged).
 */
Status_t tu_split_words(char *line, char *words[], uint32_t max_words, uint32_t *out_count);

/**
 * @brief   Converts a decimal number with a range check (instead of atoi, MISRA 21.7).
 * @param   text       Digits only: no sign, no space, not empty.
 * @param   min_value  Smallest allowed value.
 * @param   max_value  Largest allowed value (>= min_value).
 * @param   out_value  Out: the number, written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if min_value > max_value;
 *          STATUS_BAD_NUMBER for a non-digit, an empty text, an overflow or a value
 *          outside [min_value, max_value].
 */
Status_t tu_parse_u32(const char *text, uint32_t min_value, uint32_t max_value, uint32_t *out_value);

/**
 * @brief   Checks a name: 1..MAX_NAME_LEN-1 characters, the first a letter or '_', the others
 *          letters, digits, '_' or '.' ("FF2.a1"). So every name stays a valid Verilog
 *          identifier once the cross-check maps '.' to '_'.
 * @param   name  The name to check.
 * @return  STATUS_OK, STATUS_NULL_ARG or STATUS_BAD_NAME.
 */
Status_t tu_check_name(const char *name);

/**
 * @brief   Copies a text into a buffer of known size (bounded, never strcpy).
 * @param   dst       Out: the buffer, changed only on STATUS_OK.
 * @param   dst_size  Size of dst in bytes, including room for the '\0'.
 * @param   src       The text to copy.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if dst_size is 0 or src
 *          does not fit (dst unchanged).
 */
Status_t tu_copy_text(char *dst, uint32_t dst_size, const char *src);

/**
 * @brief   Fills an error message: text = what, or what + " '" + name + "'".
 * @details Example: ("x.net", 4, "unknown gate type", "NAN") gives the text
 *          "unknown gate type 'NAN'" for the console line "x.net:4: unknown gate type 'NAN'".
 * @param   diag  Out: the message, written only on STATUS_OK.
 * @param   file  The file, or NULL when not tied to a file (then "").
 * @param   line  The line, 0 when not tied to a line.
 * @param   what  What went wrong (usually status_text of the error).
 * @param   name  The offending word, or NULL for none.
 * @return  STATUS_OK; STATUS_NULL_ARG (diag or what); STATUS_BAD_ARG if the file does not
 *          fit MAX_PATH_LEN or the text does not fit MAX_MSG_LEN (diag unchanged).
 */
Status_t tu_set_diag(Diag_t *diag, const char *file, uint32_t line, const char *what, const char *name);

/**
 * @brief   Builds the path of a file in a folder: dir + "/" + file_name ("output/waves.vcd").
 * @param   dir        The folder (not empty).
 * @param   file_name  The file name.
 * @param   out        Out: the path, written only on STATUS_OK.
 * @param   out_size   Size of out in bytes, including room for the '\0'.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if dir is empty or the path does not fit (out unchanged).
 */
Status_t tu_join_dir(const char *dir, const char *file_name, char *out, uint32_t out_size);

/**
 * @brief   Splits a word "LEFT=RIGHT" in place ("J=Q1" gives "J" and "Q1").
 * @param   word       The word; on STATUS_OK its '=' is replaced by '\0'.
 * @param   out_left   Out: the part before '=', written only on STATUS_OK.
 * @param   out_right  Out: the part after '=', written only on STATUS_OK.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_SYNTAX unless the word has exactly one '='
 *          with text on both sides (word unchanged).
 */
Status_t tu_split_assign(char *word, char **out_left, char **out_right);

/**
 * @brief   Builds the path of a file named relative to another file: the folder of base_file
 *          (up to and including its last '/') + relative. A relative path starting with '/' is
 *          used as it is; a base without '/' (a file in the working folder) gives relative unchanged.
 * @details Example: ("data/counter_0357.net", "../lib/flipflops.net") gives "data/../lib/flipflops.net".
 * @param   base_file  The file that names the other one.
 * @param   relative   The path written in base_file.
 * @param   out        Out: the path, written only on STATUS_OK.
 * @param   out_size   Size of out in bytes, including room for the '\0'.
 * @return  STATUS_OK; STATUS_NULL_ARG; STATUS_BAD_ARG if the path does not fit (out unchanged).
 */
Status_t tu_join_path(const char *base_file, const char *relative, char *out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_UTILS_H */

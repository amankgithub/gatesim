/**
 * @file    text_utils.c
 * @brief   Line reader, comments, words, checked numbers, names, bounded copies, error messages, paths
 *          (output files, relative includes), PORT=NET words.
 * @details Uses <stdio.h> for the line reader (fopen, fgets, ferror, feof, fclose)
 *          and for snprintf (error message, path): a recorded MISRA C:2012 Rule 21.6
 *          deviation. Every stdio result is checked.
 *          The input modules that use these helpers (netlist, stimulus) have no stdio of their own.
 */
#include "text_utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Base of the numbers in the input files. */
#define DECIMAL_BASE    10
/** The character between the two parts of a PORT=NET word. */
#define ASSIGN_CHAR     '='

/**
 * @brief   Tells whether a character separates words (space or tab).
 * @param   c  The character.
 * @return  true for ' ' and '\t'.
 */
static bool is_separator(char c)
{
    return ((c == ' ') || (c == '\t'));
}

/**
 * @brief   Tells whether a character is a decimal digit.
 * @param   c  The character.
 * @return  true for '0'..'9'.
 */
static bool is_digit(char c)
{
    return ((c >= '0') && (c <= '9'));
}

/**
 * @brief   Tells whether a character may start a name: a letter or '_' (as in a Verilog identifier).
 * @param   c  The character.
 * @return  true for 'A'..'Z', 'a'..'z' and '_'.
 */
static bool is_name_start(char c)
{
    return (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || (c == '_'));
}

/**
 * @brief   Tells whether a character may follow in a name: a letter, a digit, '_' or '.'
 *          ('.' separates an instance name from an inner name: "FF2.a1").
 * @param   c  The character.
 * @return  true for those characters.
 */
static bool is_name_char(char c)
{
    return (is_name_start(c) || is_digit(c) || (c == '.'));
}

/**
 * @brief   Counts the words of a line without changing it.
 * @param   line  The line ('\0'-terminated).
 * @return  The number of words.
 */
static uint32_t count_words(const char *line)
{
    uint32_t count = 0U;
    uint32_t i = 0U;
    bool in_word = false;

    while (line[i] != '\0')
    {
        if (is_separator(line[i]))
        {
            in_word = false;
        }
        else if (!in_word)
        {
            in_word = true;
            count++;
        }
        else
        {
            /* still inside the same word */
        }
        i++;
    }
    return count;
}

Status_t tu_cut_comment(char *line)
{
    Status_t status = STATUS_NULL_ARG;

    if (line != NULL)
    {
        uint32_t i = 0U;
        bool found = false;

        while ((line[i] != '\0') && !found)
        {
            /* "#2" is a delay word; '#' followed by anything else (also the end of the line) starts a comment.
               line[i + 1] exists because line[i] is not the '\0'. */
            if ((line[i] == '#') && !is_digit(line[i + 1U]))
            {
                line[i] = '\0';
                found = true;
            }
            else
            {
                i++;
            }
        }
        status = STATUS_OK;
    }
    return status;
}

Status_t tu_split_words(char *line, char *words[], uint32_t max_words, uint32_t *out_count)
{
    Status_t status = STATUS_OK;

    if ((line == NULL) || (words == NULL) || (out_count == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (max_words == 0U)
    {
        status = STATUS_BAD_ARG;
    }
    else if (count_words(line) > max_words)
    {
        /* Checked before anything is written, so a rejected line stays as it was. */
        status = STATUS_TOO_MANY_WORDS;
    }
    else
    {
        uint32_t count = 0U;
        uint32_t i = 0U;
        bool in_word = false;

        while (line[i] != '\0')
        {
            if (is_separator(line[i]))
            {
                if (in_word)
                {
                    line[i] = '\0';
                    in_word = false;
                }
            }
            else if (!in_word)
            {
                words[count] = &line[i];
                count++;
                in_word = true;
            }
            else
            {
                /* still inside the same word */
            }
            i++;
        }
        *out_count = count;
    }
    return status;
}

Status_t tu_parse_u32(const char *text, uint32_t min_value, uint32_t max_value, uint32_t *out_value)
{
    Status_t status = STATUS_OK;

    if ((text == NULL) || (out_value == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if (min_value > max_value)
    {
        status = STATUS_BAD_ARG;
    }
    else if (text[0] == '\0')
    {
        status = STATUS_BAD_NUMBER;
    }
    else
    {
        uint32_t i = 0U;

        /* strtoul alone would accept " 5", "+3" and "-1", so only digits may pass. */
        while ((text[i] != '\0') && (status == STATUS_OK))
        {
            if (!is_digit(text[i]))
            {
                status = STATUS_BAD_NUMBER;
            }
            i++;
        }
        if (status == STATUS_OK)
        {
            unsigned long value;

            errno = 0;
            value = strtoul(text, NULL, DECIMAL_BASE);
            /* Compared as unsigned long BEFORE the cast: on 64-bit Linux "4294967296" does not set ERANGE. */
            if ((errno == ERANGE) || (value < (unsigned long)min_value) || (value > (unsigned long)max_value))
            {
                status = STATUS_BAD_NUMBER;
            }
            else
            {
                *out_value = (uint32_t)value;
            }
        }
    }
    return status;
}

Status_t tu_check_name(const char *name)
{
    Status_t status = STATUS_OK;

    if (name == NULL)
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        uint32_t length = 0U;
        bool valid = is_name_start(name[0]);

        /* Bounded scan: stops at the end of the name or one character past the limit. */
        while ((length < MAX_NAME_LEN) && (name[length] != '\0'))
        {
            if ((length > 0U) && !is_name_char(name[length]))
            {
                valid = false;
            }
            length++;
        }
        /* an empty name fails the first-character test ('\0' is not a letter) */
        if (!valid || (length >= MAX_NAME_LEN))
        {
            status = STATUS_BAD_NAME;
        }
    }
    return status;
}

Status_t tu_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    Status_t status = STATUS_OK;

    if ((dst == NULL) || (src == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if ((dst_size == 0U) || (strlen(src) >= (size_t)dst_size))
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        /* the length was checked above, so the text and its '\0' fit */
        (void)memcpy(dst, src, strlen(src) + 1U);
    }
    return status;
}

Status_t tu_set_diag(Diag_t *diag, const char *file, uint32_t line, const char *what, const char *name)
{
    Status_t status = STATUS_OK;

    if ((diag == NULL) || (what == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        /* built in a local copy, so diag is written only when everything fits */
        Diag_t result;

        if (file != NULL)
        {
            status = tu_copy_text(result.file, MAX_PATH_LEN, file);
        }
        else
        {
            result.file[0] = '\0';
        }
        if (status == STATUS_OK)
        {
            if (name == NULL)
            {
                status = tu_copy_text(result.text, MAX_MSG_LEN, what);
            }
            else
            {
                const int written = snprintf(result.text, MAX_MSG_LEN, "%s '%s'", what, name);

                if ((written < 0) || ((uint32_t)written >= MAX_MSG_LEN))
                {
                    status = STATUS_BAD_ARG;
                }
            }
        }
        if (status == STATUS_OK)
        {
            result.line = line;
            *diag = result;
        }
    }
    return status;
}

Status_t tu_open(LineReader_t *reader, const char *path)
{
    Status_t status = STATUS_OK;

    if ((reader == NULL) || (path == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        char copy[MAX_PATH_LEN];
        FILE *fp = NULL;

        /* the path is kept for error messages and relative includes, so it must fit before the file is opened */
        status = tu_copy_text(copy, MAX_PATH_LEN, path);
        if (status == STATUS_OK)
        {
            fp = fopen(path, "r");
            if (fp == NULL)
            {
                status = STATUS_FILE_OPEN;
            }
        }
        if (status == STATUS_OK)
        {
            reader->fp = fp;
            (void)memcpy(reader->path, copy, MAX_PATH_LEN);
            reader->line_no = 0U;
        }
    }
    return status;
}

Status_t tu_read_line(LineReader_t *reader, char *buf, uint32_t buf_size)
{
    Status_t status = STATUS_OK;

    if ((reader == NULL) || (buf == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if ((reader->fp == NULL) || (buf_size < 2U) || (buf_size > MAX_LINE_LEN))
    {
        status = STATUS_BAD_ARG;
    }
    else
    {
        char line[MAX_LINE_LEN];

        /* Read into a local buffer first: buf is written only when the whole line is good. */
        if (fgets(line, (int)buf_size, reader->fp) == NULL)
        {
            if (ferror(reader->fp) != 0)
            {
                status = STATUS_FILE_READ;
            }
            else
            {
                status = STATUS_END_OF_FILE;
            }
        }
        else
        {
            size_t len = strlen(line);

            reader->line_no++;
            if ((len > 0U) && (line[len - 1U] == '\n'))
            {
                len--;
                line[len] = '\0';
            }
            else if (feof(reader->fp) == 0)
            {
                /* No '\n' and the file goes on: the rest of the line did not fit. */
                status = STATUS_LINE_TOO_LONG;
            }
            else
            {
                /* the last line of a file without a final '\n' */
            }
            if (status == STATUS_OK)
            {
                if ((len > 0U) && (line[len - 1U] == '\r'))
                {
                    len--;
                    line[len] = '\0';
                }
                (void)memcpy(buf, line, len + 1U);
            }
        }
    }
    return status;
}

Status_t tu_close(LineReader_t *reader)
{
    Status_t status = STATUS_OK;

    if (reader == NULL)
    {
        status = STATUS_NULL_ARG;
    }
    else if (reader->fp != NULL)
    {
        if (fclose(reader->fp) != 0)
        {
            status = STATUS_FILE_READ;
        }
        /* The FILE is gone either way, so the reader must not use it again. */
        reader->fp = NULL;
    }
    else
    {
        /* already closed: nothing to do */
    }
    return status;
}

Status_t tu_join_dir(const char *dir, const char *file_name, char *out, uint32_t out_size)
{
    Status_t status = STATUS_OK;

    if ((dir == NULL) || (file_name == NULL) || (out == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else if ((dir[0] == '\0') || ((strlen(dir) + 1U + strlen(file_name)) >= (size_t)out_size))
    {
        /* an empty folder would give "/file", a path from the root */
        status = STATUS_BAD_ARG;
    }
    else
    {
        const int written = snprintf(out, (size_t)out_size, "%s/%s", dir, file_name);

        if ((written < 0) || ((uint32_t)written >= out_size))
        {
            status = STATUS_BAD_ARG;
        }
    }
    return status;
}

Status_t tu_split_assign(char *word, char **out_left, char **out_right)
{
    Status_t status = STATUS_OK;

    if ((word == NULL) || (out_left == NULL) || (out_right == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        char *const assign = strchr(word, (int)ASSIGN_CHAR);

        /* exactly one '=', not first ("=Q1") and not last ("J="); checked before the word is changed */
        if ((assign == NULL) || (assign == word) || (assign[1] == '\0') ||
            (strchr(&assign[1], (int)ASSIGN_CHAR) != NULL))
        {
            status = STATUS_SYNTAX;
        }
        else
        {
            *assign = '\0';
            *out_left = word;
            *out_right = &assign[1];
        }
    }
    return status;
}

Status_t tu_join_path(const char *base_file, const char *relative, char *out, uint32_t out_size)
{
    Status_t status = STATUS_OK;

    if ((base_file == NULL) || (relative == NULL) || (out == NULL))
    {
        status = STATUS_NULL_ARG;
    }
    else
    {
        size_t dir_len = 0U;

        /* the folder part of base_file, up to and including its last '/'; none for an absolute relative path
           or a bare file name */
        if (relative[0] != '/')
        {
            size_t i;

            for (i = 0U; base_file[i] != '\0'; i++)
            {
                if (base_file[i] == '/')
                {
                    dir_len = i + 1U;
                }
            }
        }
        if ((dir_len + strlen(relative)) >= (size_t)out_size)
        {
            status = STATUS_BAD_ARG;
        }
        else
        {
            const int written = snprintf(out, (size_t)out_size, "%.*s%s", (int)dir_len, base_file, relative);

            if ((written < 0) || ((uint32_t)written >= out_size))
            {
                status = STATUS_BAD_ARG;
            }
        }
    }
    return status;
}

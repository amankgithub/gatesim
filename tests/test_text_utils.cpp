#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "text_utils.h"

namespace
{
constexpr uint32_t SENTINEL = 77U;
}

TEST(CutComment, RemovesTrailingComment)
{
    char line[] = "set A 1 @0   # pulse";

    ASSERT_EQ(tu_cut_comment(line), STATUS_OK);
    EXPECT_STREQ(line, "set A 1 @0   ");
}

TEST(CutComment, KeepsDelayWord)
{
    char gate_line[] = "gate g1 NOT #3 Y = A # inv";
    char delay_only[] = "#2,5";
    char letter_after[] = "a #b";

    ASSERT_EQ(tu_cut_comment(gate_line), STATUS_OK);
    EXPECT_STREQ(gate_line, "gate g1 NOT #3 Y = A ");
    ASSERT_EQ(tu_cut_comment(delay_only), STATUS_OK);
    EXPECT_STREQ(delay_only, "#2,5");
    ASSERT_EQ(tu_cut_comment(letter_after), STATUS_OK);
    EXPECT_STREQ(letter_after, "a ");
}

TEST(CutComment, WholeLineComment)
{
    char line[] = "   # title";

    ASSERT_EQ(tu_cut_comment(line), STATUS_OK);
    EXPECT_STREQ(line, "   ");
}

TEST(SplitWords, SplitsOnSpacesAndTabs)
{
    char line[] = "gate  m1\tNAND #2";
    char *words[8] = {};
    uint32_t count = SENTINEL;

    ASSERT_EQ(tu_split_words(line, words, 8U, &count), STATUS_OK);
    ASSERT_EQ(count, 4U);
    EXPECT_STREQ(words[0], "gate");
    EXPECT_STREQ(words[1], "m1");
    EXPECT_STREQ(words[2], "NAND");
    EXPECT_STREQ(words[3], "#2");
}

TEST(SplitWords, EmptyLineHasNoWords)
{
    char empty[] = "";
    char blank[] = "  \t ";
    char *words[4] = {};
    uint32_t count = SENTINEL;

    ASSERT_EQ(tu_split_words(empty, words, 4U, &count), STATUS_OK);
    EXPECT_EQ(count, 0U);
    count = SENTINEL;
    ASSERT_EQ(tu_split_words(blank, words, 4U, &count), STATUS_OK);
    EXPECT_EQ(count, 0U);
}

TEST(SplitWords, RejectsTooManyWords)
{
    char line[] = "a b c d";
    char *words[3] = {};
    uint32_t count = SENTINEL;

    EXPECT_EQ(tu_split_words(line, words, 3U, &count), STATUS_TOO_MANY_WORDS);
    EXPECT_EQ(count, SENTINEL);
    EXPECT_STREQ(line, "a b c d");
}

TEST(ParseU32, AcceptsRangeAndBoundaries)
{
    uint32_t value = SENTINEL;

    ASSERT_EQ(tu_parse_u32("400", 0U, 1000U, &value), STATUS_OK);
    EXPECT_EQ(value, 400U);
    ASSERT_EQ(tu_parse_u32("1", 1U, 1000U, &value), STATUS_OK);
    EXPECT_EQ(value, 1U);
    ASSERT_EQ(tu_parse_u32("1000", 1U, 1000U, &value), STATUS_OK);
    EXPECT_EQ(value, 1000U);
}

TEST(ParseU32, RejectsNonDigits)
{
    const char *const bad[] = {"12a", "-1", " 5", "", "+3"};

    for (const char *text : bad)
    {
        SCOPED_TRACE(std::string("text \"") + text + "\"");
        uint32_t value = SENTINEL;
        EXPECT_EQ(tu_parse_u32(text, 0U, 1000U, &value), STATUS_BAD_NUMBER);
        EXPECT_EQ(value, SENTINEL);
    }
}

TEST(ParseU32, RejectsOutOfRange)
{
    uint32_t value = SENTINEL;

    EXPECT_EQ(tu_parse_u32("1001", 0U, 1000U, &value), STATUS_BAD_NUMBER);
    EXPECT_EQ(tu_parse_u32("0", 1U, 1000U, &value), STATUS_BAD_NUMBER);
    EXPECT_EQ(tu_parse_u32("4294967296", 0U, 0xFFFFFFFFU, &value), STATUS_BAD_NUMBER);
    EXPECT_EQ(tu_parse_u32("99999999999999999999", 0U, 0xFFFFFFFFU, &value), STATUS_BAD_NUMBER);
    EXPECT_EQ(value, SENTINEL);
}

TEST(CheckName, AcceptsLettersDigitsUnderscoreDot)
{
    const std::string longest(MAX_NAME_LEN - 1U, 'a');

    EXPECT_EQ(tu_check_name("FF2.a1"), STATUS_OK);
    EXPECT_EQ(tu_check_name("Q_0"), STATUS_OK);
    EXPECT_EQ(tu_check_name("_t"), STATUS_OK);
    EXPECT_EQ(tu_check_name(longest.c_str()), STATUS_OK);
}

TEST(CheckName, RejectsBadNames)
{
    const std::string too_long(MAX_NAME_LEN, 'a');

    EXPECT_EQ(tu_check_name(""), STATUS_BAD_NAME);
    EXPECT_EQ(tu_check_name(too_long.c_str()), STATUS_BAD_NAME);
    EXPECT_EQ(tu_check_name("a-b"), STATUS_BAD_NAME);
    EXPECT_EQ(tu_check_name("a=b"), STATUS_BAD_NAME);
    EXPECT_EQ(tu_check_name("2a"), STATUS_BAD_NAME);
    EXPECT_EQ(tu_check_name(".a"), STATUS_BAD_NAME);
}

TEST(ReadLine, ReadsLinesAndCountsThem)
{
    const char *const expected[] = {"first line", "second line with words", "", "fourth line after a blank one"};
    LineReader_t reader;
    char buf[MAX_LINE_LEN];

    ASSERT_EQ(tu_open(&reader, "tests/data/lines_ok.txt"), STATUS_OK);
    EXPECT_EQ(reader.line_no, 0U);
    EXPECT_STREQ(reader.path, "tests/data/lines_ok.txt");
    for (const char *text : expected)
    {
        ASSERT_EQ(tu_read_line(&reader, buf, MAX_LINE_LEN), STATUS_OK);
        EXPECT_STREQ(buf, text);
    }
    EXPECT_EQ(reader.line_no, 4U);
    EXPECT_EQ(tu_read_line(&reader, buf, MAX_LINE_LEN), STATUS_END_OF_FILE);
    EXPECT_EQ(reader.line_no, 4U);
    EXPECT_EQ(tu_close(&reader), STATUS_OK);
}

TEST(ReadLine, StripsCarriageReturn)
{
    /* Guard: the data file must really end its lines with \r\n (Git must not convert it). */
    std::ifstream raw("tests/data/lines_crlf.txt", std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
    ASSERT_NE(bytes.find("\r\n"), std::string::npos) << "tests/data/lines_crlf.txt has lost its CRLF line ends";

    LineReader_t reader;
    char buf[MAX_LINE_LEN];

    ASSERT_EQ(tu_open(&reader, "tests/data/lines_crlf.txt"), STATUS_OK);
    ASSERT_EQ(tu_read_line(&reader, buf, MAX_LINE_LEN), STATUS_OK);
    EXPECT_STREQ(buf, "first line");
    ASSERT_EQ(tu_read_line(&reader, buf, MAX_LINE_LEN), STATUS_OK);
    EXPECT_STREQ(buf, "second line");
    EXPECT_EQ(tu_read_line(&reader, buf, MAX_LINE_LEN), STATUS_END_OF_FILE);
    EXPECT_EQ(tu_close(&reader), STATUS_OK);
}

TEST(ReadLine, RejectsTooLongLine)
{
    LineReader_t reader;
    char buf[MAX_LINE_LEN];

    ASSERT_EQ(tu_open(&reader, "tests/data/lines_long.txt"), STATUS_OK);
    ASSERT_EQ(tu_read_line(&reader, buf, MAX_LINE_LEN), STATUS_OK);
    EXPECT_STREQ(buf, "short line");
    EXPECT_EQ(tu_read_line(&reader, buf, MAX_LINE_LEN), STATUS_LINE_TOO_LONG);
    EXPECT_EQ(reader.line_no, 2U);
    EXPECT_STREQ(buf, "short line");
    EXPECT_EQ(tu_close(&reader), STATUS_OK);
    EXPECT_EQ(reader.fp, nullptr);
}

TEST(OpenFile, MissingFileFails)
{
    LineReader_t reader;
    reader.fp = nullptr;
    reader.line_no = SENTINEL;

    EXPECT_EQ(tu_open(&reader, "tests/data/no_such_file.txt"), STATUS_FILE_OPEN);
    EXPECT_EQ(reader.fp, nullptr);
    EXPECT_EQ(reader.line_no, SENTINEL);

    /* a path that the reader cannot keep is rejected before the file is opened */
    const std::string too_long(MAX_PATH_LEN, 'a');
    EXPECT_EQ(tu_open(&reader, too_long.c_str()), STATUS_BAD_ARG);
    EXPECT_EQ(reader.fp, nullptr);
    EXPECT_EQ(reader.line_no, SENTINEL);
}

TEST(SetDiag, FormatsWhatAndName)
{
    Diag_t diag = {};

    ASSERT_EQ(tu_set_diag(&diag, "x.net", 4U, "unknown gate type", "NAN"), STATUS_OK);
    EXPECT_STREQ(diag.file, "x.net");
    EXPECT_EQ(diag.line, 4U);
    EXPECT_STREQ(diag.text, "unknown gate type 'NAN'");

    ASSERT_EQ(tu_set_diag(&diag, nullptr, 0U, "missing end line", nullptr), STATUS_OK);
    EXPECT_STREQ(diag.file, "");
    EXPECT_EQ(diag.line, 0U);
    EXPECT_STREQ(diag.text, "missing end line");

    /* a text that does not fit MAX_MSG_LEN: BAD_ARG, the message unchanged */
    const std::string long_name(MAX_MSG_LEN, 'a');
    EXPECT_EQ(tu_set_diag(&diag, "x.net", 4U, "not a valid name", long_name.c_str()), STATUS_BAD_ARG);
    EXPECT_STREQ(diag.text, "missing end line");
}

TEST(JoinDir, AddsSlash)
{
    char path[MAX_PATH_LEN] = "unchanged";

    ASSERT_EQ(tu_join_dir("output", "waves.vcd", path, MAX_PATH_LEN), STATUS_OK);
    EXPECT_STREQ(path, "output/waves.vcd");

    char small[16] = "unchanged";
    EXPECT_EQ(tu_join_dir("output", "waves.vcd", small, sizeof(small)), STATUS_BAD_ARG);
    EXPECT_STREQ(small, "unchanged");
    EXPECT_EQ(tu_join_dir("", "waves.vcd", path, MAX_PATH_LEN), STATUS_BAD_ARG);
    EXPECT_STREQ(path, "output/waves.vcd");
}

TEST(SplitAssign, SplitsPortAndNet)
{
    char word[] = "J=Q1";
    char *left = nullptr;
    char *right = nullptr;

    ASSERT_EQ(tu_split_assign(word, &left, &right), STATUS_OK);
    EXPECT_STREQ(left, "J");
    EXPECT_STREQ(right, "Q1");

    const char *const bad[] = {"J=", "=Q1", "JQ1", "J=Q=1"};
    for (const char *text : bad)
    {
        SCOPED_TRACE(std::string("word \"") + text + "\"");
        std::string copy(text);
        std::vector<char> buf(copy.begin(), copy.end());
        buf.push_back('\0');
        char *l = nullptr;
        char *r = nullptr;
        EXPECT_EQ(tu_split_assign(buf.data(), &l, &r), STATUS_SYNTAX);
        EXPECT_STREQ(buf.data(), text);
        EXPECT_EQ(l, nullptr);
        EXPECT_EQ(r, nullptr);
    }
}

TEST(JoinPath, UsesDirectoryOfBaseFile)
{
    char path[MAX_PATH_LEN] = "unchanged";

    ASSERT_EQ(tu_join_path("data/counter_0357.net", "../lib/flipflops.net", path, MAX_PATH_LEN), STATUS_OK);
    EXPECT_STREQ(path, "data/../lib/flipflops.net");
    ASSERT_EQ(tu_join_path("x.net", "lib/a.net", path, MAX_PATH_LEN), STATUS_OK);
    EXPECT_STREQ(path, "lib/a.net");
    ASSERT_EQ(tu_join_path("data/x.net", "/abs/a.net", path, MAX_PATH_LEN), STATUS_OK);
    EXPECT_STREQ(path, "/abs/a.net");

    char small[10] = "unchanged";
    EXPECT_EQ(tu_join_path("data/x.net", "a.net", small, sizeof(small)), STATUS_BAD_ARG);
    EXPECT_STREQ(small, "unchanged");
}

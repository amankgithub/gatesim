#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "event_queue.h"
#include "netlist.h"
#include "report.h"
#include "sim_kernel.h"
#include "stimulus.h"

namespace
{
const char *const RING_VCD_PATH = "build/test_ring5.vcd";
const char *const JK_MS_VCD_PATH = "build/test_jk_ms.vcd";
}

TEST(VcdCode, OneAndTwoCharacterCodes)
{
    char code[RW_VCD_CODE_SIZE] = "";

    ASSERT_EQ(rw_vcd_code(0U, code, RW_VCD_CODE_SIZE), STATUS_OK);
    EXPECT_STREQ(code, "!");
    ASSERT_EQ(rw_vcd_code(1U, code, RW_VCD_CODE_SIZE), STATUS_OK);
    EXPECT_STREQ(code, "\"");
    ASSERT_EQ(rw_vcd_code(93U, code, RW_VCD_CODE_SIZE), STATUS_OK);
    EXPECT_STREQ(code, "~");
    ASSERT_EQ(rw_vcd_code(94U, code, RW_VCD_CODE_SIZE), STATUS_OK);
    EXPECT_STREQ(code, "!!");
    ASSERT_EQ(rw_vcd_code(95U, code, RW_VCD_CODE_SIZE), STATUS_OK);
    EXPECT_STREQ(code, "!\"");

    EXPECT_EQ(rw_vcd_code(94U, code, 2U), STATUS_BAD_ARG);
    EXPECT_EQ(rw_vcd_code(static_cast<uint16_t>(MAX_NETS), code, RW_VCD_CODE_SIZE), STATUS_BAD_ARG);
    EXPECT_STREQ(code, "!\"");
}

class Vcd : public ::testing::Test
{
protected:
    static Netlist_t nl;
    static Stimulus_t st;
    static EventQueue_t queue;
    static SimKernel_t k;
    static VcdWriter_t w;

    void TearDown() override
    {
        (void)std::remove(RING_VCD_PATH);
        (void)std::remove(JK_MS_VCD_PATH);
    }

    /* The last "#time" line of a file's lines ("" if there is none). */
    static std::string LastTimeLine(const std::vector<std::string> &lines)
    {
        std::string last_time;
        for (const std::string &line : lines)
        {
            if ((!line.empty()) && (line[0] == '#'))
            {
                last_time = line;
            }
        }
        return last_time;
    }

    /* Every line of a text file. */
    static std::vector<std::string> ReadLines(const char *path)
    {
        std::vector<std::string> lines;
        std::ifstream file(path);
        std::string line;

        EXPECT_TRUE(file.is_open()) << path;
        while (std::getline(file, line))
        {
            lines.push_back(line);
        }
        return lines;
    }
};

Netlist_t Vcd::nl;
Stimulus_t Vcd::st;
EventQueue_t Vcd::queue;
SimKernel_t Vcd::k;
VcdWriter_t Vcd::w;

TEST_F(Vcd, RingFileHeaderAndChanges)
{
    Diag_t diag = {};
    SkListener_t listener = {};
    SimResult_t result = {};

    ASSERT_EQ(nl_load(&nl, "data/ring5.net", &diag), STATUS_OK) << diag.text;
    ASSERT_EQ(st_load(&st, &nl, "data/ring5.stim", &diag), STATUS_OK) << diag.text;
    ASSERT_EQ(eq_init(&queue), STATUS_OK);
    ASSERT_EQ(st_seed(&st, &queue), STATUS_OK);
    ASSERT_EQ(sk_init(&k, &nl, &queue, st.end_time), STATUS_OK);
    ASSERT_EQ(rw_vcd_open(&w, &nl, RING_VCD_PATH), STATUS_OK);
    ASSERT_EQ(rw_vcd_listener(&w, &listener), STATUS_OK);
    ASSERT_EQ(sk_set_listener(&k, &listener), STATUS_OK);
    ASSERT_EQ(sk_run(&k, &result), STATUS_OK);
    ASSERT_EQ(rw_vcd_close(&w), STATUS_OK);

    const std::vector<std::string> lines = ReadLines(RING_VCD_PATH);
    uint32_t var_lines = 0U;
    size_t dumpvars_end = 0U;
    for (size_t i = 0U; i < lines.size(); i++)
    {
        if (lines[i].rfind("$var wire 1 ", 0) == 0)
        {
            var_lines++;
        }
        if ((dumpvars_end == 0U) && (lines[i] == "$dumpvars"))
        {
            dumpvars_end = i + 1U + nl.net_count;
        }
    }
    EXPECT_EQ(var_lines, 6U);
    EXPECT_NE(std::find(lines.begin(), lines.end(), "$timescale 1ns $end"), lines.end());
    EXPECT_NE(std::find(lines.begin(), lines.end(), "$var wire 1 ! EN $end"), lines.end());
    ASSERT_GT(dumpvars_end, 0U);
    ASSERT_LT(dumpvars_end + 5U, lines.size());
    EXPECT_EQ(lines[dumpvars_end - 1U], "x&");
    EXPECT_EQ(lines[dumpvars_end], "$end");

    /* EN = 0 at 0 (no extra #0), n0 = 1 at 2, n1 = 0 at 4 */
    const std::vector<std::string> first_changes(lines.begin() + static_cast<long>(dumpvars_end) + 1,
                                                 lines.begin() + static_cast<long>(dumpvars_end) + 6);
    const std::vector<std::string> expected = {"0!", "#2", "1\"", "#4", "0#"};
    EXPECT_EQ(first_changes, expected);

    EXPECT_EQ(LastTimeLine(lines), "#100");

    ASSERT_EQ(nl_load(&nl, "data/jk_ms.net", &diag), STATUS_OK) << diag.text;
    ASSERT_EQ(st_load(&st, &nl, "data/jk_ms.stim", &diag), STATUS_OK) << diag.text;
    char late_set[] = "set J 1 @100";
    ASSERT_EQ(st_parse_line(&st, &nl, late_set, &diag), STATUS_OK) << diag.text;
    ASSERT_EQ(eq_init(&queue), STATUS_OK);
    ASSERT_EQ(st_seed(&st, &queue), STATUS_OK);
    ASSERT_EQ(sk_init(&k, &nl, &queue, 95U), STATUS_OK);
    ASSERT_EQ(sk_set_source(&k, &st, st_expand), STATUS_OK);
    ASSERT_EQ(rw_vcd_open(&w, &nl, JK_MS_VCD_PATH), STATUS_OK);
    ASSERT_EQ(rw_vcd_listener(&w, &listener), STATUS_OK);
    ASSERT_EQ(sk_set_listener(&k, &listener), STATUS_OK);
    ASSERT_EQ(sk_run(&k, &result), STATUS_OK);
    ASSERT_EQ(rw_vcd_close(&w), STATUS_OK);
    EXPECT_EQ(result.reason, STOP_END_TIME);
    EXPECT_EQ(result.stop_time, 95U);

    const std::vector<std::string> jk_lines = ReadLines(JK_MS_VCD_PATH);
    ASSERT_FALSE(jk_lines.empty());
    EXPECT_EQ(jk_lines.back(), "#95");
    EXPECT_NE(std::find(jk_lines.begin(), jk_lines.end(), "#87"), jk_lines.end());
}

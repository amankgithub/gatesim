#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "event_queue.h"
#include "netlist.h"
#include "stimulus.h"

namespace
{
constexpr uint32_t SENTINEL = 77U;
}

class StimulusTest : public ::testing::Test
{
protected:
    static Netlist_t nl;
    static Stimulus_t st;
    static EventQueue_t queue;
    static Diag_t diag;

    void SetUp() override
    {
        ASSERT_EQ(st_init(&st), STATUS_OK);
        ASSERT_EQ(eq_init(&queue), STATUS_OK);
        diag = Diag_t{};
    }

    /* Loads a netlist from data/ or tests/data/. */
    static void LoadNet(const char *path)
    {
        ASSERT_EQ(nl_load(&nl, path, &diag), STATUS_OK) << diag.file << ":" << diag.line << ": " << diag.text;
    }

    /* Parses one stimulus line (st_parse_line changes its line, so it gets a copy). */
    static Status_t Parse(const char *text)
    {
        std::vector<char> line(text, text + std::strlen(text) + 1U);
        return st_parse_line(&st, &nl, line.data(), &diag);
    }
};

Netlist_t StimulusTest::nl;
Stimulus_t StimulusTest::st;
EventQueue_t StimulusTest::queue;
Diag_t StimulusTest::diag;

class StimSet : public StimulusTest
{
};
class StimEnd : public StimulusTest
{
};
class StimSeed : public StimulusTest
{
};

TEST_F(StimSet, ParsesAndRejects)
{
    LoadNet("data/sr_nor.net");
    ASSERT_EQ(Parse("set R 1 @0   # reset"), STATUS_OK);
    ASSERT_EQ(st.set_count, 1U);
    EXPECT_EQ(st.sets[0].net, 1U);
    EXPECT_EQ(st.sets[0].value, LV_1);
    EXPECT_EQ(st.sets[0].time, 0U);

    EXPECT_EQ(Parse("set Q 1 @5"), STATUS_NOT_INPUT);
    EXPECT_EQ(Parse("set T 1 @5"), STATUS_NOT_FOUND);
    EXPECT_EQ(Parse("set S 2 @5"), STATUS_BAD_NUMBER);
    EXPECT_EQ(Parse("set S 1 @x"), STATUS_BAD_NUMBER);
    EXPECT_EQ(Parse("set S 1 5"), STATUS_SYNTAX);
    EXPECT_EQ(Parse("set S 1"), STATUS_SYNTAX);
    EXPECT_EQ(Parse("wait 5"), STATUS_UNKNOWN_KEYWORD);
    EXPECT_EQ(st.set_count, 1U);
}

TEST_F(StimEnd, RequiredExactlyOnce)
{
    LoadNet("data/sr_nor.net");
    diag.line = SENTINEL;
    EXPECT_EQ(st_load(&st, &nl, "tests/data/no_end.stim", &diag), STATUS_NO_END);
    EXPECT_EQ(diag.line, 0U);
    EXPECT_STREQ(diag.file, "tests/data/no_end.stim");
    EXPECT_STREQ(diag.text, "missing end line");

    ASSERT_EQ(st_init(&st), STATUS_OK);
    ASSERT_EQ(Parse("end 80"), STATUS_OK);
    EXPECT_TRUE(st.has_end);
    EXPECT_EQ(st.end_time, 80U);
    EXPECT_EQ(Parse("end 90"), STATUS_DUPLICATE);
    EXPECT_EQ(st.end_time, 80U);
}

TEST_F(StimSeed, SkipsEventsAfterEnd)
{
    const char *const lines[] = {"set A 0 @0",  "set A 1 @10", "set A 0 @11", "set A 1 @20", "set A 0 @22",
                                 "set A 1 @30", "set A 0 @33", "set A 1 @40", "set A 0 @44", "end 30"};

    LoadNet("data/inv_pulse.net");
    for (const char *text : lines)
    {
        ASSERT_EQ(Parse(text), STATUS_OK) << text;
    }
    ASSERT_EQ(st_seed(&st, &queue), STATUS_OK);
    ASSERT_EQ(queue.count, 6U);
    EXPECT_EQ(queue.rows[5].time, 30U);
    EXPECT_EQ(queue.rows[5].kind, EV_SET);
    EXPECT_EQ(queue.rows[5].value, LV_1);
}

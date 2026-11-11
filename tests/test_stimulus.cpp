#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "event_queue.h"
#include "netlist.h"
#include "sim_kernel.h"
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
class StimClock : public StimulusTest
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

TEST_F(StimClock, ParsesAndRejects)
{
    LoadNet("data/jk_level.net");
    ASSERT_EQ(Parse("clock CLK period 40 high 20 start 20"), STATUS_OK);
    ASSERT_EQ(st.clock_count, 1U);
    EXPECT_EQ(st.clocks[0].net, 2U);
    EXPECT_EQ(st.clocks[0].period, 40U);
    EXPECT_EQ(st.clocks[0].high, 20U);
    EXPECT_EQ(st.clocks[0].start, 20U);

    EXPECT_EQ(Parse("clock J period 40 high 40 start 0"), STATUS_BAD_NUMBER);
    EXPECT_STREQ(diag.text, "not a valid number '40'");
    EXPECT_EQ(Parse("clock J period 1 high 1 start 0"), STATUS_BAD_NUMBER);
    EXPECT_EQ(Parse("clock J period 1000000001 high 1 start 0"), STATUS_BAD_NUMBER);
    EXPECT_EQ(Parse("clock J period 40 high 0 start 0"), STATUS_BAD_NUMBER);
    EXPECT_EQ(Parse("clock J period 40 low 20 start 0"), STATUS_SYNTAX);
    EXPECT_STREQ(diag.text, "syntax error 'low'");
    EXPECT_EQ(Parse("clock J period 40 high 20"), STATUS_SYNTAX);
    EXPECT_EQ(Parse("clock Q period 40 high 20 start 0"), STATUS_NOT_INPUT);
    EXPECT_EQ(Parse("set CLK 1 @0"), STATUS_TWO_DRIVERS);
    EXPECT_STREQ(diag.text, "net already has a driver 'CLK'");
    EXPECT_EQ(Parse("clock CLK period 10 high 5 start 0"), STATUS_TWO_DRIVERS);
    ASSERT_EQ(Parse("set J 1 @0"), STATUS_OK);
    EXPECT_EQ(Parse("clock J period 10 high 5 start 0"), STATUS_TWO_DRIVERS);
    EXPECT_EQ(st.clock_count, 1U);

    ASSERT_EQ(Parse("clock K period 10 high 5 start 0"), STATUS_OK);
    ASSERT_EQ(Parse("clock PRE_N period 10 high 5 start 0"), STATUS_OK);
    ASSERT_EQ(Parse("clock CLR_N period 10 high 5 start 0"), STATUS_OK);
    EXPECT_EQ(st.clock_count, MAX_CLOCKS);
    ASSERT_EQ(st_init(&st), STATUS_OK);
    for (const char *text : {"clock J period 10 high 5 start 0", "clock K period 10 high 5 start 0",
                             "clock PRE_N period 10 high 5 start 0", "clock CLR_N period 10 high 5 start 0"})
    {
        ASSERT_EQ(Parse(text), STATUS_OK) << text;
    }
    EXPECT_EQ(Parse("clock CLK period 10 high 5 start 0"), STATUS_TABLE_FULL);

    uint8_t clock = static_cast<uint8_t>(SENTINEL);
    EXPECT_EQ(st_find_clock(&st, 1U, &clock), STATUS_OK);
    EXPECT_EQ(clock, 1U);
    clock = static_cast<uint8_t>(SENTINEL);
    EXPECT_EQ(st_find_clock(&st, 2U, &clock), STATUS_NOT_FOUND);
    EXPECT_EQ(clock, SENTINEL);
}

TEST_F(StimClock, ExpandGivesEdgeChain)
{
    LoadNet("data/jk_level.net");
    ASSERT_EQ(Parse("clock CLK period 40 high 20 start 20"), STATUS_OK);
    ASSERT_EQ(Parse("end 50"), STATUS_OK);

    ASSERT_EQ(st_seed(&st, &queue), STATUS_OK);
    ASSERT_EQ(queue.count, 1U);
    EXPECT_EQ(queue.rows[0].kind, EV_CLOCK);
    EXPECT_EQ(queue.rows[0].time, 0U);
    EXPECT_EQ(queue.rows[0].index, 0U);
    EXPECT_EQ(queue.rows[0].value, LV_0);

    struct Step
    {
        uint32_t     time;
        LogicValue_t value;
        uint32_t     next_time;
        LogicValue_t next_value;
    };
    const Step chain[] = {{0U, LV_0, 20U, LV_1}, {20U, LV_1, 40U, LV_0}, {40U, LV_0, 60U, LV_1}};
    for (const Step &step : chain)
    {
        const Event_t event = {step.time, 0U, EV_CLOCK, 0U, step.value};
        SkExpansion_t x = {};
        ASSERT_EQ(st_expand(&st, &event, 100U, &x), STATUS_OK);
        EXPECT_EQ(x.net, 2U);
        EXPECT_EQ(x.value, step.value);
        EXPECT_TRUE(x.has_next);
        EXPECT_EQ(x.next_time, step.next_time) << step.time;
        EXPECT_EQ(x.next_value, step.next_value) << step.time;
    }

    const Event_t fall = {40U, 0U, EV_CLOCK, 0U, LV_0};
    SkExpansion_t x = {};
    ASSERT_EQ(st_expand(&st, &fall, st.end_time, &x), STATUS_OK);
    EXPECT_FALSE(x.has_next);

    const Event_t set = {0U, 0U, EV_SET, 2U, LV_1};
    const Event_t unknown = {0U, 0U, EV_CLOCK, 1U, LV_1};
    x.net = static_cast<uint16_t>(SENTINEL);
    EXPECT_EQ(st_expand(&st, &set, 100U, &x), STATUS_BAD_ARG);
    EXPECT_EQ(st_expand(&st, &unknown, 100U, &x), STATUS_BAD_ARG);
    EXPECT_EQ(x.net, SENTINEL);
}

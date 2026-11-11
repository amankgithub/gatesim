#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "event_queue.h"
#include "netlist.h"
#include "sim_kernel.h"
#include "stimulus.h"

namespace
{
using Change = std::pair<uint32_t, LogicValue_t>;

struct SetEvent
{
    const char  *net;
    LogicValue_t value;
    uint32_t     time;
};

constexpr uint32_t MAX_TEST_STEPS = 10000U;

/* Reads a trace file: lines starting with '#' and empty lines are skipped */
std::vector<Change> ReadTrace(const char *path)
{
    std::vector<Change> out;
    std::ifstream file(path);
    std::string line;

    EXPECT_TRUE(file.is_open()) << path;
    while (std::getline(file, line))
    {
        if (line.empty() || (line[0] == '#'))
        {
            continue;
        }
        std::istringstream words(line);
        uint32_t time = 0U;
        char value = '?';
        if (!(words >> time >> value) || (std::strchr("01x", value) == nullptr))
        {
            ADD_FAILURE() << path << ": bad line \"" << line << "\"";
            continue;
        }
        out.push_back({time, (value == '0') ? LV_0 : ((value == '1') ? LV_1 : LV_X)});
    }
    return out;
}
}

class Kernel : public ::testing::Test
{
protected:
    static Netlist_t nl;
    static EventQueue_t queue;
    static SimKernel_t k;
    static Stimulus_t st;
    std::vector<std::pair<uint16_t, Change>> changes;

    /* Loads a netlist and empties the event list. */
    void Load(const char *path)
    {
        Diag_t diag = {};
        ASSERT_EQ(nl_load(&nl, path, &diag), STATUS_OK) << diag.file << ":" << diag.line << ": " << diag.text;
        ASSERT_EQ(eq_init(&queue), STATUS_OK);
    }

    /* Loads a stimulus file and adds its events; returns its end time. */
    uint32_t Seed(const char *path)
    {
        Diag_t diag = {};
        EXPECT_EQ(st_load(&st, &nl, path, &diag), STATUS_OK) << diag.file << ":" << diag.line << ": " << diag.text;
        EXPECT_EQ(st_seed(&st, &queue), STATUS_OK);
        return st.end_time;
    }

    /* Reads stimulus lines with st_parse_line (they must all be accepted) and adds their events. */
    uint32_t SeedLines(const std::vector<std::string> &lines)
    {
        Diag_t diag = {};
        EXPECT_EQ(st_init(&st), STATUS_OK);
        for (const std::string &text : lines)
        {
            std::vector<char> line(text.begin(), text.end());
            line.push_back('\0');
            EXPECT_EQ(st_parse_line(&st, &nl, line.data(), &diag), STATUS_OK) << text << ": " << diag.text;
        }
        EXPECT_EQ(st_seed(&st, &queue), STATUS_OK);
        return st.end_time;
    }

    /* Adds one EV_SET event per entry (net found by name). */
    void AddSets(const std::vector<SetEvent> &sets)
    {
        for (const SetEvent &s : sets)
        {
            uint16_t net = 0U;
            ASSERT_EQ(nl_find_net(&nl, s.net, &net), STATUS_OK) << s.net;
            ASSERT_EQ(eq_add(&queue, s.time, EV_SET, net, s.value), STATUS_OK);
        }
    }

    /* Runs to the end; after each step that did not stop the run, records the changed nets. */
    void RunToEnd(uint32_t end_time)
    {
        bool done = false;
        uint32_t steps = 0U;

        ASSERT_EQ(sk_init(&k, &nl, &queue, end_time), STATUS_OK);
        while (!done && (steps < MAX_TEST_STEPS))
        {
            ASSERT_EQ(sk_step(&k, &done), STATUS_OK);
            if (!done)
            {
                for (uint32_t i = 0U; i < k.changed_count; i++)
                {
                    const uint16_t net = k.changed[i];
                    changes.push_back({net, {k.now, nl.nets[net].value}});
                }
            }
            else
            {
                EXPECT_EQ(k.changed_count, 0U);
            }
            steps++;
        }
        ASSERT_TRUE(done) << "run did not stop";
    }

    /* Every recorded change of one net, in order. */
    std::vector<Change> ChangesOf(const char *name) const
    {
        std::vector<Change> out;
        uint16_t net = 0U;
        EXPECT_EQ(nl_find_net(&nl, name, &net), STATUS_OK) << name;
        for (const auto &c : changes)
        {
            if (c.first == net)
            {
                out.push_back(c.second);
            }
        }
        return out;
    }
};

Netlist_t Kernel::nl;
EventQueue_t Kernel::queue;
SimKernel_t Kernel::k;
Stimulus_t Kernel::st;

TEST_F(Kernel, InertialExampleMatchesTrace)
{
    Load("data/inv_pulse.net");
    AddSets({{"A", LV_1, 0U}, {"A", LV_0, 20U}, {"A", LV_1, 22U}, {"A", LV_0, 30U}, {"A", LV_1, 34U}});
    RunToEnd(45U);

    const std::vector<Change> expected = {{3U, LV_0}, {33U, LV_1}, {37U, LV_0}};
    EXPECT_EQ(ChangesOf("Y"), expected);
    EXPECT_EQ(k.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(k.now, 37U);
}

TEST_F(Kernel, PulsesNarrowerThanDelaySwallowed)
{
    Load("data/inv_pulse.net");
    RunToEnd(Seed("data/inv_pulse.stim"));

    EXPECT_EQ(ChangesOf("Y"), ReadTrace("tests/data/inv_pulse_Y.trace"));
    EXPECT_EQ(k.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(k.now, 47U);
    EXPECT_EQ(k.events_applied, 14U);
}

TEST_F(Kernel, RiseFallDelaysShapePulse)
{
    Load("data/inv_risefall.net");
    RunToEnd(Seed("data/inv_risefall.stim"));

    const std::vector<Change> expected = {{2U, LV_1}, {35U, LV_0}, {38U, LV_1}};
    EXPECT_EQ(ChangesOf("Y"), expected);
    EXPECT_EQ(k.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(k.events_applied, 10U);

    const Gate_t &g = nl.gates[0];
    uint16_t delay = 0U;
    ASSERT_EQ(sk_gate_delay(&g, LV_1, &delay), STATUS_OK);
    EXPECT_EQ(delay, 2U);
    ASSERT_EQ(sk_gate_delay(&g, LV_0, &delay), STATUS_OK);
    EXPECT_EQ(delay, 5U);
    ASSERT_EQ(sk_gate_delay(&g, LV_X, &delay), STATUS_OK);
    EXPECT_EQ(delay, 2U);
}

TEST_F(Kernel, SrLatchMatchesTrace)
{
    Load("data/sr_nor.net");
    RunToEnd(Seed("data/sr_nor.stim"));

    EXPECT_EQ(ChangesOf("Q"), ReadTrace("tests/data/sr_nor_Q.trace"));
    EXPECT_EQ(k.reason, STOP_END_TIME);
}

TEST_F(Kernel, SkewedReleaseSettlesInReset)
{
    Load("data/sr_nor.net");
    RunToEnd(Seed("data/sr_nor_skew.stim"));

    const std::vector<Change> q = ChangesOf("Q");
    const std::vector<Change> qn = ChangesOf("QN");
    ASSERT_FALSE(q.empty());
    ASSERT_FALSE(qn.empty());
    EXPECT_EQ(q.back(), Change(42U, LV_0));
    EXPECT_EQ(qn.back(), Change(52U, LV_1));
    EXPECT_EQ(k.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(k.now, 52U);
}

TEST_F(Kernel, RingPeriodIsTwenty)
{
    Load("data/ring5.net");
    RunToEnd(Seed("data/ring5.stim"));

    std::vector<uint32_t> rises;
    for (const Change &c : ChangesOf("n0"))
    {
        if ((c.first > 10U) && (c.second == LV_1))
        {
            rises.push_back(c.first);
        }
    }
    const std::vector<uint32_t> expected = {22U, 42U, 62U, 82U};
    EXPECT_EQ(rises, expected);
    EXPECT_EQ(k.reason, STOP_END_TIME);
}

TEST_F(Kernel, RaceAroundPeriodEight)
{
    Load("data/jk_level.net");
    RunToEnd(SeedLines({"set CLK 0 @0", "set CLK 1 @20", "set CLK 0 @40", "set J 1 @0", "set K 1 @0",
                        "set PRE_N 1 @0", "set CLR_N 0 @0", "set CLR_N 1 @5", "end 50"}));

    std::vector<Change> after_rise;
    for (const Change &c : ChangesOf("Q"))
    {
        if (c.first > 20U)
        {
            after_rise.push_back(c);
        }
    }
    const std::vector<Change> expected = {{24U, LV_1}, {30U, LV_0}, {32U, LV_1}, {38U, LV_0}, {40U, LV_1}};
    EXPECT_EQ(after_rise, expected);
    EXPECT_EQ(k.reason, STOP_QUEUE_EMPTY);
}

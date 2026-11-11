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

struct Reported
{
    uint16_t     net;
    uint32_t     time;
    LogicValue_t old_value;
    LogicValue_t new_value;
};

/* The listener of the tests: on_change appends to the vector given as ctx. */
void RecordChange(void *ctx, uint16_t net, uint32_t now, LogicValue_t old_value, LogicValue_t new_value)
{
    static_cast<std::vector<Reported> *>(ctx)->push_back({net, now, old_value, new_value});
}

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
    std::vector<Reported> changes;
    SimResult_t result = {};

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

    /* Runs the whole simulation with the Recorder listener; the result goes to `result`. */
    void RunToEnd(uint32_t end_time)
    {
        const SkListener_t recorder = {&changes, RecordChange};

        ASSERT_EQ(sk_init(&k, &nl, &queue, end_time), STATUS_OK);
        ASSERT_EQ(sk_set_listener(&k, &recorder), STATUS_OK);
        ASSERT_EQ(sk_run(&k, &result), STATUS_OK);
        EXPECT_EQ(k.changed_count, 0U);
    }

    /* Every recorded change of one net, in order. */
    std::vector<Change> ChangesOf(const char *name) const
    {
        std::vector<Change> out;
        uint16_t net = 0U;
        EXPECT_EQ(nl_find_net(&nl, name, &net), STATUS_OK) << name;
        for (const Reported &c : changes)
        {
            if (c.net == net)
            {
                out.push_back({c.time, c.new_value});
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
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.stop_time, 37U);

    ASSERT_FALSE(changes.empty());
    EXPECT_EQ(changes[0].time, 0U);
    EXPECT_EQ(changes[0].old_value, LV_X);
    EXPECT_EQ(changes[0].new_value, LV_1);
}

TEST_F(Kernel, PulsesNarrowerThanDelaySwallowed)
{
    Load("data/inv_pulse.net");
    RunToEnd(Seed("data/inv_pulse.stim"));

    EXPECT_EQ(ChangesOf("Y"), ReadTrace("tests/data/inv_pulse_Y.trace"));
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.stop_time, 47U);
    EXPECT_EQ(result.events_applied, 14U);
}

TEST_F(Kernel, RiseFallDelaysShapePulse)
{
    Load("data/inv_risefall.net");
    RunToEnd(Seed("data/inv_risefall.stim"));

    const std::vector<Change> expected = {{2U, LV_1}, {35U, LV_0}, {38U, LV_1}};
    EXPECT_EQ(ChangesOf("Y"), expected);
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.events_applied, 10U);

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
    EXPECT_EQ(result.reason, STOP_END_TIME);
    EXPECT_EQ(result.stop_time, 80U);
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
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.stop_time, 52U);
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
    EXPECT_EQ(result.reason, STOP_END_TIME);
    EXPECT_EQ(result.stop_time, 100U);
    EXPECT_EQ(result.events_applied, 52U);
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
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
}

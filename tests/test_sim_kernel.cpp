/**
 * @file    test_sim_kernel.cpp
 * @brief   Unit tests for sim_kernel (UT-61..UT-70).
 * @details Load reads a netlist and empties the event list; Seed (a stimulus file), SeedLines (stimulus lines
 *          read with st_parse_line) or AddSets (single EV_SET events) add the events. RunToEnd prepares the
 *          run with sk_init, runs sk_run with the stimulus as the source of clock edges (st_expand)
 *          and a recorder listener (RecordChange) that stores every change it is told about
 *          (net, time, old value, new value), and keeps the SimResult_t.
 *          The netlists and stimuli come from data/; the inverter example (InertialExampleMatchesTrace) is added
 *          with eq_add and the race-around stimulus (RaceAroundPeriodEight) with st_parse_line, because they are
 *          not data files. Expected changes of a net can come from a trace file in tests/data/
 *          (ReadTrace).
 */
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
/** One recorded change: (time, new value). */
using Change = std::pair<uint32_t, LogicValue_t>;

/** One stimulus event: net name, value, time. */
struct SetEvent
{
    const char  *net;
    LogicValue_t value;
    uint32_t     time;
};

/** One change as the kernel reports it to its listener. */
struct Reported
{
    uint16_t     net;
    uint32_t     time;
    LogicValue_t old_value;
    LogicValue_t new_value;
};

/** The listener of the tests: on_change appends to the vector given as ctx. */
void RecordChange(void *ctx, uint16_t net, uint32_t now, LogicValue_t old_value, LogicValue_t new_value)
{
    static_cast<std::vector<Reported> *>(ctx)->push_back({net, now, old_value, new_value});
}

/**
 * Reads a trace file: lines starting with '#' and empty lines are skipped, every other line is
 * "time value" with value 0, 1 or x. A line that does not read fails the test.
 */
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
}  // namespace

/** Fixture: netlist, event list, kernel and stimulus are static objects (about 100 KB together). */
class Kernel : public ::testing::Test
{
protected:
    static Netlist_t nl;
    static EventQueue_t queue;
    static SimKernel_t k;
    static Stimulus_t st;
    std::vector<Reported> changes;   /**< every on_change call, in the order reported */
    SimResult_t result = {};         /**< the result of the last RunToEnd */

    /** Loads a netlist and empties the event list. */
    void Load(const char *path)
    {
        Diag_t diag = {};
        ASSERT_EQ(nl_load(&nl, path, &diag), STATUS_OK) << diag.file << ":" << diag.line << ": " << diag.text;
        ASSERT_EQ(eq_init(&queue), STATUS_OK);
    }

    /** Loads a stimulus file and adds its events; returns its end time. */
    uint32_t Seed(const char *path)
    {
        Diag_t diag = {};
        EXPECT_EQ(st_load(&st, &nl, path, &diag), STATUS_OK) << diag.file << ":" << diag.line << ": " << diag.text;
        EXPECT_EQ(st_seed(&st, &queue), STATUS_OK);
        return st.end_time;
    }

    /** Reads stimulus lines with st_parse_line (they must all be accepted) and adds their events. */
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

    /** Adds one EV_SET event per entry (net found by name). */
    void AddSets(const std::vector<SetEvent> &sets)
    {
        for (const SetEvent &s : sets)
        {
            uint16_t net = 0U;
            ASSERT_EQ(nl_find_net(&nl, s.net, &net), STATUS_OK) << s.net;
            ASSERT_EQ(eq_add(&queue, s.time, EV_SET, net, s.value), STATUS_OK);
        }
    }

    /** Runs the whole simulation with the Recorder listener; the result goes to `result`. */
    void RunToEnd(uint32_t end_time)
    {
        const SkListener_t recorder = {&changes, RecordChange, nullptr};

        /* sk_init comes after the events are added: it does not touch the event list */
        ASSERT_EQ(sk_init(&k, &nl, &queue, end_time), STATUS_OK);
        ASSERT_EQ(sk_set_source(&k, &st, st_expand), STATUS_OK);
        ASSERT_EQ(sk_set_listener(&k, &recorder), STATUS_OK);
        ASSERT_EQ(sk_run(&k, &result), STATUS_OK);
        EXPECT_EQ(k.changed_count, 0U);   /* the scratch data of the last step is cleared */
    }

    /** Every recorded change of one net, in order. */
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

/* UT-61 FR-06 Positive */
TEST_F(Kernel, InertialExampleMatchesTrace)
{
    /* A changes at 0, 20, 22, 30, 34 on an inverter with delay 3 */
    Load("data/inv_pulse.net");
    AddSets({{"A", LV_1, 0U}, {"A", LV_0, 20U}, {"A", LV_1, 22U}, {"A", LV_0, 30U}, {"A", LV_1, 34U}});
    RunToEnd(45U);

    /* Y leaves X at 3; the 2-ns pulse at 20..22 is shorter than the delay, so its change is cancelled at 22 */
    const std::vector<Change> expected = {{3U, LV_0}, {33U, LV_1}, {37U, LV_0}};
    EXPECT_EQ(ChangesOf("Y"), expected);
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.stop_time, 37U);

    /* the listener also gets the old value: A's first change is from the power-up X */
    ASSERT_FALSE(changes.empty());
    EXPECT_EQ(changes[0].time, 0U);
    EXPECT_EQ(changes[0].old_value, LV_X);
    EXPECT_EQ(changes[0].new_value, LV_1);
}

/* UT-62 FR-06 Boundary */
TEST_F(Kernel, PulsesNarrowerThanDelaySwallowed)
{
    /* pulses of width 1, 2, 3, 4 at 10, 20, 30, 40; end 60: only those of width 3 and 4 pass */
    Load("data/inv_pulse.net");
    RunToEnd(Seed("data/inv_pulse.stim"));

    EXPECT_EQ(ChangesOf("Y"), ReadTrace("tests/data/inv_pulse_Y.trace"));
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.stop_time, 47U);
    EXPECT_EQ(result.events_applied, 14U);
}

/* UT-63 FR-06 Positive */
TEST_F(Kernel, RiseFallDelaysShapePulse)
{
    /* inverter #2,5: pulses of width 3, 4, 6 at 10, 20, 30; Y falls 5 ns after A rises, rises 2 ns after A falls */
    Load("data/inv_risefall.net");
    RunToEnd(Seed("data/inv_risefall.stim"));

    /* the 3-ns and 4-ns pulses are narrower than the fall delay, so only the 6-ns pulse reaches Y */
    const std::vector<Change> expected = {{2U, LV_1}, {35U, LV_0}, {38U, LV_1}};
    EXPECT_EQ(ChangesOf("Y"), expected);
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.events_applied, 10U);

    /* the delay choice: to 1 rise, to 0 fall, to X the shorter one */
    const Gate_t &g = nl.gates[0];
    uint16_t delay = 0U;
    ASSERT_EQ(sk_gate_delay(&g, LV_1, &delay), STATUS_OK);
    EXPECT_EQ(delay, 2U);
    ASSERT_EQ(sk_gate_delay(&g, LV_0, &delay), STATUS_OK);
    EXPECT_EQ(delay, 5U);
    ASSERT_EQ(sk_gate_delay(&g, LV_X, &delay), STATUS_OK);
    EXPECT_EQ(delay, 2U);
}

/* UT-64 FR-06, FR-07 Positive */
TEST_F(Kernel, SrLatchMatchesTrace)
{
    /* reset, set, then S = R = 1 released together at 50: Q and QN oscillate with period 4 */
    Load("data/sr_nor.net");
    RunToEnd(Seed("data/sr_nor.stim"));

    EXPECT_EQ(ChangesOf("Q"), ReadTrace("tests/data/sr_nor_Q.trace"));
    EXPECT_EQ(result.reason, STOP_END_TIME);
    EXPECT_EQ(result.stop_time, 80U);
}

/* UT-65 FR-06, FR-07 Positive */
TEST_F(Kernel, SkewedReleaseSettlesInReset)
{
    /* R is released 1 ns after S: R still holds Q at 0, so QN rises once and the latch is stable */
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

/* UT-66 FR-06, FR-07 Positive */
TEST_F(Kernel, RingPeriodIsTwenty)
{
    /* EN 0 at 0, 1 at 10; end 100 */
    Load("data/ring5.net");
    RunToEnd(Seed("data/ring5.stim"));

    /* after the enable at 10, n0 rises every 20 ns (5 gates, 2 ns each, two passes round the ring) */
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

/* UT-67 FR-06 Positive */
TEST_F(Kernel, RaceAroundPeriodEight)
{
    /* J = K = 1, cleared at 0..5, CLK high from 20 to 40: Q toggles every 2 + 2 + 2 + 2 ns while CLK is high */
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

/* UT-68 FR-05, FR-06 Positive */
TEST_F(Kernel, ClockLineDrivesRaceAround)
{
    /* the same run as RaceAroundPeriodEight, but CLK comes from "clock CLK period 40 high 20 start 20" (end 50) */
    Load("data/jk_level.net");
    RunToEnd(Seed("data/jk_level.stim"));

    /* CLK is 0 from 0 (start 20), rises at 20 and falls at 40; the next rise (60) is after the end */
    const std::vector<Change> clk = {{0U, LV_0}, {20U, LV_1}, {40U, LV_0}};
    EXPECT_EQ(ChangesOf("CLK"), clk);
    const std::vector<Change> q = {{4U, LV_0}, {24U, LV_1}, {30U, LV_0}, {32U, LV_1}, {38U, LV_0}, {40U, LV_1}};
    EXPECT_EQ(ChangesOf("Q"), q);
    const std::vector<Change> qn = {{2U, LV_1}, {26U, LV_0}, {28U, LV_1}, {34U, LV_0}, {36U, LV_1}, {42U, LV_0}};
    EXPECT_EQ(ChangesOf("QN"), qn);
    EXPECT_EQ(result.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(result.stop_time, 42U);
    EXPECT_EQ(result.events_applied, 32U);
}

/* UT-69 FR-05, FR-06, FR-07 Boundary */
TEST_F(Kernel, NarrowPulseLeavesLatchUndecided)
{
    /* preset to Q = 1, then one clock pulse only 2 ns wide at 60: both NAND inputs see it, and the
       latch is left oscillating with period 4 until the end at 90 */
    Load("data/jk_level.net");
    RunToEnd(Seed("data/jk_level_narrow.stim"));

    EXPECT_EQ(ChangesOf("Q"), ReadTrace("tests/data/jk_level_narrow_Q.trace"));
    const std::vector<Change> clk = {{0U, LV_0}, {60U, LV_1}, {62U, LV_0}};
    EXPECT_EQ(ChangesOf("CLK"), clk);
    EXPECT_EQ(result.reason, STOP_END_TIME);
    EXPECT_EQ(result.stop_time, 90U);
    EXPECT_EQ(result.events_applied, 41U);
}

/* UT-70 FR-03, FR-06 Positive */
TEST_F(Kernel, MasterSlaveTogglesOncePerClock)
{
    /* the library's JK_MS with J = K = 1: Q toggles once per clock, when CLK falls (no race-around) */
    Load("data/jk_ms.net");
    RunToEnd(Seed("data/jk_ms.stim"));

    const std::vector<Change> q = {{8U, LV_0}, {45U, LV_1}, {87U, LV_0}};
    EXPECT_EQ(ChangesOf("Q"), q);
    const std::vector<Change> qn = {{2U, LV_1}, {47U, LV_0}, {85U, LV_1}};
    EXPECT_EQ(ChangesOf("QN"), qn);
    EXPECT_EQ(result.reason, STOP_END_TIME);
    EXPECT_EQ(result.stop_time, 100U);
    EXPECT_EQ(result.events_applied, 40U);
}

#include <gtest/gtest.h>

#include <cstdint>
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
        uint32_t line = 0U;
        ASSERT_EQ(nl_load(&nl, path, &line), STATUS_OK) << path << " line " << line;
        ASSERT_EQ(eq_init(&queue), STATUS_OK);
    }

    /* Loads a stimulus file and adds its events; returns its end time. */
    uint32_t Seed(const char *path)
    {
        uint32_t line = 0U;
        EXPECT_EQ(st_load(&st, &nl, path, &line), STATUS_OK) << path << " line " << line;
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

TEST_F(Kernel, TransportInverterExample)
{
    Load("data/inv_pulse.net");
    AddSets({{"A", LV_1, 0U}, {"A", LV_0, 20U}, {"A", LV_1, 22U}, {"A", LV_0, 30U}, {"A", LV_1, 34U}});
    RunToEnd(45U);

    const std::vector<Change> expected = {{23U, LV_1}, {25U, LV_0}, {33U, LV_1}, {37U, LV_0}};
    EXPECT_EQ(ChangesOf("Y"), expected);
}

TEST_F(Kernel, TransportKeepsEveryPulse)
{
    Load("data/inv_pulse.net");
    RunToEnd(Seed("data/inv_pulse.stim"));

    const std::vector<Change> expected = {{3U, LV_1},  {13U, LV_0}, {14U, LV_1}, {23U, LV_0}, {25U, LV_1},
                                          {33U, LV_0}, {36U, LV_1}, {43U, LV_0}, {47U, LV_1}};
    EXPECT_EQ(ChangesOf("Y"), expected);
    EXPECT_EQ(k.reason, STOP_QUEUE_EMPTY);
    EXPECT_EQ(k.now, 47U);
    EXPECT_EQ(k.events_applied, 18U);
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

/**
 * @file    test_event_queue.cpp
 * @brief   Unit tests for event_queue.
 * @details One fixture (EventQueue) with an event list that is emptied before every test. The tests cover the
 *          min-finder (earliest time, ties broken by seq, not by row position), removal (last row into the
 *          hole), a full list, an empty list and the search for a gate's event.
 */
#include <gtest/gtest.h>

#include <cstdint>

#include "event_queue.h"

namespace
{
/** Value written into an output before a call that must fail, to see that it stays unchanged. */
constexpr uint16_t SENTINEL = 77U;
}  // namespace

/** Fixture: one event list (a static object, about 16 KB), emptied before every test. */
class EventQueue : public ::testing::Test
{
protected:
    static EventQueue_t q;

    void SetUp() override
    {
        ASSERT_EQ(eq_init(&q), STATUS_OK);
    }
};

EventQueue_t EventQueue::q;

TEST_F(EventQueue, EarliestByTimeExample)
{
    /* times 34, 20, 22, 30, 3 added in this order (seq 0..4) */
    const uint32_t times[] = {34U, 20U, 22U, 30U, 3U};
    uint16_t row = SENTINEL;

    for (uint16_t i = 0U; i < 5U; i++)
    {
        ASSERT_EQ(eq_add(&q, times[i], EV_GATE, i, LV_1), STATUS_OK);
    }
    ASSERT_EQ(q.count, 5U);
    ASSERT_EQ(eq_find_earliest(&q, &row), STATUS_OK);
    EXPECT_EQ(row, 4U);
    EXPECT_EQ(q.rows[row].time, 3U);
    EXPECT_EQ(q.rows[row].seq, 4U);
    EXPECT_EQ(q.rows[row].index, 4U);
}

TEST_F(EventQueue, TieBrokenBySeq)
{
    uint16_t row = SENTINEL;
    Event_t event;

    ASSERT_EQ(eq_add(&q, 10U, EV_SET, 7U, LV_0), STATUS_OK);    /* seq 0 */
    ASSERT_EQ(eq_add(&q, 10U, EV_SET, 8U, LV_1), STATUS_OK);    /* seq 1 */
    ASSERT_EQ(eq_find_earliest(&q, &row), STATUS_OK);
    EXPECT_EQ(q.rows[row].seq, 0U);
    EXPECT_EQ(q.rows[row].index, 7U);

    /* the row position does not decide: after a removal the later-added event sits in row 0 */
    ASSERT_EQ(eq_init(&q), STATUS_OK);
    ASSERT_EQ(eq_add(&q, 20U, EV_SET, 1U, LV_0), STATUS_OK);    /* seq 0, row 0 */
    ASSERT_EQ(eq_add(&q, 10U, EV_SET, 2U, LV_0), STATUS_OK);    /* seq 1, row 1 */
    ASSERT_EQ(eq_add(&q, 10U, EV_SET, 3U, LV_0), STATUS_OK);    /* seq 2, row 2 */
    ASSERT_EQ(eq_remove(&q, 0U, &event), STATUS_OK);            /* row 0 now holds seq 2 */
    ASSERT_EQ(eq_find_earliest(&q, &row), STATUS_OK);
    EXPECT_EQ(row, 1U);
    EXPECT_EQ(q.rows[row].seq, 1U);
}

TEST_F(EventQueue, RemoveMovesLastIntoHole)
{
    Event_t event;

    for (uint16_t i = 0U; i < 4U; i++)
    {
        ASSERT_EQ(eq_add(&q, 100U + i, EV_GATE, i, LV_0), STATUS_OK);
    }
    ASSERT_EQ(eq_remove(&q, 1U, &event), STATUS_OK);
    EXPECT_EQ(event.time, 101U);    /* the removed event is handed back */
    EXPECT_EQ(event.index, 1U);
    EXPECT_EQ(q.count, 3U);
    EXPECT_EQ(q.rows[1].time, 103U);    /* the old row 3 */
    EXPECT_EQ(q.rows[1].seq, 3U);

    /* a row past the end */
    event.time = SENTINEL;
    EXPECT_EQ(eq_remove(&q, 3U, &event), STATUS_BAD_ARG);
    EXPECT_EQ(event.time, SENTINEL);
    EXPECT_EQ(q.count, 3U);
}

TEST_F(EventQueue, FullListRejectsAdd)
{
    for (uint32_t i = 0U; i < MAX_EVENTS; i++)
    {
        ASSERT_EQ(eq_add(&q, i, EV_GATE, 0U, LV_1), STATUS_OK) << i;
    }
    EXPECT_EQ(eq_add(&q, 5000U, EV_GATE, 0U, LV_1), STATUS_QUEUE_FULL);
    EXPECT_EQ(q.count, MAX_EVENTS);
    EXPECT_EQ(q.next_seq, MAX_EVENTS);

    /* a bad value is rejected before the count is looked at */
    EXPECT_EQ(eq_add(&q, 1U, EV_SET, 0U, (LogicValue_t)LV_COUNT), STATUS_BAD_ARG);
}

TEST_F(EventQueue, EmptyListHasNoEarliest)
{
    uint16_t row = SENTINEL;

    EXPECT_EQ(eq_find_earliest(&q, &row), STATUS_QUEUE_EMPTY);
    EXPECT_EQ(row, SENTINEL);
    EXPECT_EQ(eq_find_earliest(nullptr, &row), STATUS_NULL_ARG);
}

TEST_F(EventQueue, FindsGateEvent)
{
    uint16_t row = SENTINEL;

    ASSERT_EQ(eq_add(&q, 10U, EV_SET, 2U, LV_1), STATUS_OK);    /* index 2, but a net: not a gate event */
    ASSERT_EQ(eq_add(&q, 12U, EV_GATE, 1U, LV_0), STATUS_OK);
    ASSERT_EQ(eq_add(&q, 15U, EV_GATE, 2U, LV_1), STATUS_OK);
    ASSERT_EQ(eq_find_gate_event(&q, 2U, &row), STATUS_OK);
    EXPECT_EQ(row, 2U);
    EXPECT_EQ(q.rows[row].time, 15U);

    /* a gate without an event */
    row = SENTINEL;
    EXPECT_EQ(eq_find_gate_event(&q, 5U, &row), STATUS_NOT_FOUND);
    EXPECT_EQ(row, SENTINEL);
}

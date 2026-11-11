/**
 * @file    test_status.cpp
 * @brief   Unit tests for status (UT-01..UT-03).
 */
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "status.h"

/* UT-01 FR-10 Positive */
TEST(StatusText, OkHasText)
{
    EXPECT_STREQ(status_text(STATUS_OK), "ok");
}

/* UT-02 FR-10 Positive */
TEST(StatusText, EveryCodeHasDistinctText)
{
    for (int i = 0; i < static_cast<int>(STATUS_COUNT); i++)
    {
        SCOPED_TRACE("code " + std::to_string(i));
        const char *text_i = status_text(static_cast<Status_t>(i));
        ASSERT_NE(text_i, nullptr);
        EXPECT_GT(std::strlen(text_i), 0U);
        EXPECT_STRNE(text_i, "unknown status");
        for (int j = i + 1; j < static_cast<int>(STATUS_COUNT); j++)
        {
            EXPECT_STRNE(text_i, status_text(static_cast<Status_t>(j))) << "codes " << i << " and " << j;
        }
    }
}

/* UT-03 FR-10 Boundary */
TEST(StatusText, OutOfRangeGivesUnknown)
{
    EXPECT_STREQ(status_text(STATUS_COUNT), "unknown status");
    EXPECT_STREQ(status_text(static_cast<Status_t>(static_cast<int>(STATUS_COUNT) + 1)), "unknown status");
}

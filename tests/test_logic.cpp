#include <gtest/gtest.h>

#include "logic.h"

namespace
{
constexpr LogicValue_t SENTINEL = 77U;

/* Calculates a two-input gate and checks that the call succeeds. */
LogicValue_t Eval2(GateType_t type, LogicValue_t a, LogicValue_t b)
{
    const LogicValue_t inputs[2] = {a, b};
    LogicValue_t out = SENTINEL;
    EXPECT_EQ(lg_eval(type, inputs, 2U, &out), STATUS_OK);
    return out;
}
}

TEST(LogicAnd, MatchesTruthTable)
{
    EXPECT_EQ(Eval2(GT_AND, LV_0, LV_0), LV_0);
    EXPECT_EQ(Eval2(GT_AND, LV_0, LV_1), LV_0);
    EXPECT_EQ(Eval2(GT_AND, LV_1, LV_0), LV_0);
    EXPECT_EQ(Eval2(GT_AND, LV_1, LV_1), LV_1);
}

TEST(LogicOr, MatchesTruthTable)
{
    EXPECT_EQ(Eval2(GT_OR, LV_0, LV_0), LV_0);
    EXPECT_EQ(Eval2(GT_OR, LV_0, LV_1), LV_1);
    EXPECT_EQ(Eval2(GT_OR, LV_1, LV_0), LV_1);
    EXPECT_EQ(Eval2(GT_OR, LV_1, LV_1), LV_1);
}

TEST(LogicNot, InvertsBothValues)
{
    const LogicValue_t in_0[1] = {LV_0};
    const LogicValue_t in_1[1] = {LV_1};
    LogicValue_t out = SENTINEL;

    ASSERT_EQ(lg_eval(GT_NOT, in_0, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_1);
    ASSERT_EQ(lg_eval(GT_NOT, in_1, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_0);
}

TEST(LogicEval, RejectsWrongInputCount)
{
    const LogicValue_t inputs[3] = {LV_1, LV_1, LV_1};
    LogicValue_t out = SENTINEL;

    EXPECT_EQ(lg_eval(GT_NOT, inputs, 2U, &out), STATUS_INPUT_COUNT);
    EXPECT_EQ(lg_eval(GT_AND, inputs, 0U, &out), STATUS_INPUT_COUNT);
    EXPECT_EQ(lg_eval(GT_AND, inputs, 3U, &out), STATUS_INPUT_COUNT);
    EXPECT_EQ(out, SENTINEL);
}

TEST(LogicEval, RejectsInvalidValue)
{
    const LogicValue_t inputs[2] = {LV_1, 2U};
    LogicValue_t out = SENTINEL;

    EXPECT_EQ(lg_eval(GT_AND, inputs, 2U, &out), STATUS_BAD_ARG);
    EXPECT_EQ(out, SENTINEL);
}

TEST(LogicType, ParsesNamesAndRejectsUnknown)
{
    GateType_t type = GT_AND;

    ASSERT_EQ(lg_type_from_name("NAND", &type), STATUS_OK);
    EXPECT_EQ(type, GT_NAND);

    type = GT_OR;
    EXPECT_EQ(lg_type_from_name("NAN", &type), STATUS_UNKNOWN_TYPE);
    EXPECT_EQ(lg_type_from_name("nand", &type), STATUS_UNKNOWN_TYPE);
    EXPECT_EQ(lg_type_from_name("", &type), STATUS_UNKNOWN_TYPE);
    EXPECT_EQ(type, GT_OR);
}

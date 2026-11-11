#include <gtest/gtest.h>

#include <cstdint>

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

const LogicValue_t VALUES[LV_COUNT] = {LV_0, LV_1, LV_X};

/* Checks a two-input gate against an expected table, indexed [a][b] in the order 0, 1, X. */
void ExpectTable(GateType_t type, const LogicValue_t (&expected)[LV_COUNT][LV_COUNT])
{
    for (uint32_t a = 0U; a < LV_COUNT; a++)
    {
        for (uint32_t b = 0U; b < LV_COUNT; b++)
        {
            EXPECT_EQ(Eval2(type, VALUES[a], VALUES[b]), expected[a][b]) << "a=" << a << " b=" << b;
        }
    }
}
}

TEST(LogicAnd, MatchesTruthTable)
{
    const LogicValue_t expected[LV_COUNT][LV_COUNT] = {
        {LV_0, LV_0, LV_0},
        {LV_0, LV_1, LV_X},
        {LV_0, LV_X, LV_X},
    };
    ExpectTable(GT_AND, expected);
}

TEST(LogicOr, MatchesTruthTable)
{
    const LogicValue_t expected[LV_COUNT][LV_COUNT] = {
        {LV_0, LV_1, LV_X},
        {LV_1, LV_1, LV_1},
        {LV_X, LV_1, LV_X},
    };
    ExpectTable(GT_OR, expected);
}

TEST(LogicNand, ZeroDecidesEvenWithX)
{
    const LogicValue_t inputs[3] = {LV_1, LV_X, LV_0};
    LogicValue_t out = SENTINEL;

    ASSERT_EQ(lg_eval(GT_NAND, inputs, 3U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_1);
}

TEST(LogicNand, XWithoutZeroGivesX)
{
    const LogicValue_t inputs[3] = {LV_1, LV_X, LV_1};
    LogicValue_t out = SENTINEL;

    ASSERT_EQ(lg_eval(GT_NAND, inputs, 3U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_X);
}

TEST(LogicNotBuf, NotInvertsBufCopies)
{
    const LogicValue_t in_0[1] = {LV_0};
    const LogicValue_t in_1[1] = {LV_1};
    const LogicValue_t in_x[1] = {LV_X};
    LogicValue_t out = SENTINEL;

    ASSERT_EQ(lg_eval(GT_NOT, in_0, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_1);
    ASSERT_EQ(lg_eval(GT_NOT, in_1, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_0);
    ASSERT_EQ(lg_eval(GT_NOT, in_x, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_X);
    ASSERT_EQ(lg_eval(GT_BUF, in_0, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_0);
    ASSERT_EQ(lg_eval(GT_BUF, in_1, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_1);
    ASSERT_EQ(lg_eval(GT_BUF, in_x, 1U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_X);
}

TEST(LogicEval, RejectsWrongInputCount)
{
    const LogicValue_t inputs[5] = {LV_1, LV_1, LV_1, LV_1, LV_1};
    LogicValue_t out = SENTINEL;

    EXPECT_EQ(lg_eval(GT_NOT, inputs, 2U, &out), STATUS_INPUT_COUNT);
    EXPECT_EQ(lg_eval(GT_AND, inputs, 0U, &out), STATUS_INPUT_COUNT);
    EXPECT_EQ(lg_eval(GT_AND, inputs, 5U, &out), STATUS_INPUT_COUNT);
    EXPECT_EQ(out, SENTINEL);
}

TEST(LogicEval, RejectsInvalidValue)
{
    const LogicValue_t inputs[2] = {LV_1, 3U};
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

TEST(LogicXor, MatchesTruthTable)
{
    const LogicValue_t expected[LV_COUNT][LV_COUNT] = {
        {LV_0, LV_1, LV_X},
        {LV_1, LV_0, LV_X},
        {LV_X, LV_X, LV_X},
    };
    ExpectTable(GT_XOR, expected);

    const LogicValue_t inputs[4] = {LV_1, LV_1, LV_1, LV_0};
    LogicValue_t out = SENTINEL;
    ASSERT_EQ(lg_eval(GT_XOR, inputs, 4U, &out), STATUS_OK);
    EXPECT_EQ(out, LV_1);
}

TEST(LogicXnor, IsNotOfXor)
{
    for (LogicValue_t a : VALUES)
    {
        for (LogicValue_t b : VALUES)
        {
            SCOPED_TRACE(testing::Message() << "a=" << int(a) << " b=" << int(b));
            const LogicValue_t xor_value = Eval2(GT_XOR, a, b);
            const LogicValue_t not_in[1] = {xor_value};
            LogicValue_t not_xor = SENTINEL;
            ASSERT_EQ(lg_eval(GT_NOT, not_in, 1U, &not_xor), STATUS_OK);
            EXPECT_EQ(Eval2(GT_XNOR, a, b), not_xor);
        }
    }
}

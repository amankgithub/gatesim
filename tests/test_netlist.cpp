#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "netlist.h"
#include "text_utils.h"

namespace
{
constexpr uint16_t SENTINEL = 77U;

const char *const SR_LATCH_LINES[] = {
    "input  S R",
    "output Q QN",
    "gate n1 NOR #2  Q  = R QN",
    "gate n2 NOR #2  QN = S Q",
};
}

class NetlistTest : public ::testing::Test
{
protected:
    static Netlist_t nl;
    static Diag_t diag;

    void SetUp() override
    {
        ASSERT_EQ(nl_init(&nl), STATUS_OK);
        diag = Diag_t{};
    }

    /* Parses one line of text (nl_parse_line changes its line, so it gets a copy). */
    static Status_t Parse(const char *text)
    {
        std::vector<char> line(text, text + std::strlen(text) + 1U);
        return nl_parse_line(&nl, line.data(), &diag);
    }

    /* Parses the SR latch lines; every one must be accepted. */
    static void LoadSrLatch()
    {
        for (const char *text : SR_LATCH_LINES)
        {
            ASSERT_EQ(Parse(text), STATUS_OK) << text;
        }
    }

    /* Splits a gate line and fills a GateSpec_t with nl_parse_gate_words. */
    static Status_t SpecFromLine(const char *text, GateSpec_t *spec)
    {
        std::vector<char> line(text, text + std::strlen(text) + 1U);
        char *words[MAX_WORDS] = {};
        uint32_t count = 0U;
        Status_t status = tu_split_words(line.data(), words, MAX_WORDS, &count);
        if (status == STATUS_OK)
        {
            status = nl_parse_gate_words(words, count, spec);
        }
        return status;
    }

    /* Name of the net in row index. */
    static std::string NetName(uint16_t index)
    {
        return std::string(nl.nets[index].name);
    }
};

Netlist_t NetlistTest::nl;
Diag_t NetlistTest::diag;

class FindOrAdd : public NetlistTest
{
};
class GateLine : public NetlistTest
{
};
class ParseLine : public NetlistTest
{
};
class LoadNetlist : public NetlistTest
{
};
class CheckNetlist : public NetlistTest
{
};

TEST_F(FindOrAdd, ReturnsExistingIndex)
{
    uint16_t index = SENTINEL;

    LoadSrLatch();
    ASSERT_EQ(nl_find_or_add_net(&nl, "QN", &index), STATUS_OK);
    EXPECT_EQ(index, 3U);
    EXPECT_EQ(nl.net_count, 4U);
}

TEST_F(FindOrAdd, AddsNewNetAtEnd)
{
    uint16_t index = SENTINEL;

    LoadSrLatch();
    ASSERT_EQ(nl_find_or_add_net(&nl, "EN", &index), STATUS_OK);
    EXPECT_EQ(index, 4U);
    EXPECT_EQ(nl.net_count, 5U);
    EXPECT_EQ(NetName(4U), "EN");
    EXPECT_EQ(nl.nets[4].driver, DRV_NONE);
    EXPECT_FALSE(nl.nets[4].is_output);
    EXPECT_EQ(nl.nets[4].value, LV_X);
}

TEST_F(FindOrAdd, RejectsWhenTableFull)
{
    uint16_t index = SENTINEL;

    for (uint32_t i = 0U; i < MAX_NETS; i++)
    {
        const std::string name = "n" + std::to_string(i);
        ASSERT_EQ(nl_find_or_add_net(&nl, name.c_str(), &index), STATUS_OK) << name;
    }
    EXPECT_EQ(nl.net_count, MAX_NETS);
    index = SENTINEL;
    EXPECT_EQ(nl_find_or_add_net(&nl, "one_more", &index), STATUS_TABLE_FULL);
    EXPECT_EQ(index, SENTINEL);
    EXPECT_EQ(nl.net_count, MAX_NETS);

    ASSERT_EQ(nl_init(&nl), STATUS_OK);
    for (uint32_t i = 0U; i < (MAX_NETS - 1U); i++)
    {
        const std::string name = "n" + std::to_string(i);
        ASSERT_EQ(nl_find_or_add_net(&nl, name.c_str(), &index), STATUS_OK) << name;
    }
    GateSpec_t spec;
    ASSERT_EQ(SpecFromLine("gate g1 AND #1 y = n0 z", &spec), STATUS_OK);
    EXPECT_EQ(nl_add_gate(&nl, &spec), STATUS_TABLE_FULL);
    EXPECT_EQ(nl.net_count, MAX_NETS - 1U);
    EXPECT_EQ(nl.gate_count, 0U);

    ASSERT_EQ(SpecFromLine("gate g1 AND #1 y = n0 n1", &spec), STATUS_OK);
    EXPECT_EQ(nl_add_gate(&nl, &spec), STATUS_OK);
    EXPECT_EQ(nl.net_count, MAX_NETS);
    EXPECT_EQ(nl.gate_count, 1U);
}

TEST(ParseDelay, SingleAndRiseFall)
{
    uint16_t rise = SENTINEL;
    uint16_t fall = SENTINEL;

    ASSERT_EQ(nl_parse_delay("#2", &rise, &fall), STATUS_OK);
    EXPECT_EQ(rise, 2U);
    EXPECT_EQ(fall, 2U);
    ASSERT_EQ(nl_parse_delay("#2,5", &rise, &fall), STATUS_OK);
    EXPECT_EQ(rise, 2U);
    EXPECT_EQ(fall, 5U);
    ASSERT_EQ(nl_parse_delay("#1", &rise, &fall), STATUS_OK);
    EXPECT_EQ(rise, 1U);
    EXPECT_EQ(fall, 1U);
    ASSERT_EQ(nl_parse_delay("#1000", &rise, &fall), STATUS_OK);
    EXPECT_EQ(rise, 1000U);
    EXPECT_EQ(fall, 1000U);
}

TEST(ParseDelay, RejectsBadDelays)
{
    const char *const bad[] = {"#0", "#1001", "#a", "#2,", "#2,0", "#,5", "#2,5,6"};

    for (const char *word : bad)
    {
        SCOPED_TRACE(std::string("word \"") + word + "\"");
        uint16_t rise = SENTINEL;
        uint16_t fall = SENTINEL;
        EXPECT_EQ(nl_parse_delay(word, &rise, &fall), STATUS_BAD_NUMBER);
        EXPECT_EQ(rise, SENTINEL);
        EXPECT_EQ(fall, SENTINEL);
    }
    uint16_t rise = SENTINEL;
    uint16_t fall = SENTINEL;
    EXPECT_EQ(nl_parse_delay("2", &rise, &fall), STATUS_SYNTAX);
    EXPECT_EQ(rise, SENTINEL);
    EXPECT_EQ(fall, SENTINEL);
}

TEST_F(GateLine, ValidLineAddsGate)
{
    ASSERT_EQ(Parse("gate m1 NAND #2  a1 = J CLK QN CLR"), STATUS_OK);
    ASSERT_EQ(nl.gate_count, 1U);

    const Gate_t &g = nl.gates[0];
    EXPECT_STREQ(g.name, "m1");
    EXPECT_EQ(g.type, GT_NAND);
    ASSERT_EQ(g.in_count, 4U);
    EXPECT_EQ(NetName(g.in[0]), "J");
    EXPECT_EQ(NetName(g.in[1]), "CLK");
    EXPECT_EQ(NetName(g.in[2]), "QN");
    EXPECT_EQ(NetName(g.in[3]), "CLR");
    EXPECT_EQ(NetName(g.out), "a1");
    EXPECT_EQ(nl.nets[g.out].driver, DRV_GATE);
    EXPECT_EQ(g.rise, 2U);
    EXPECT_EQ(g.fall, 2U);
}

TEST_F(GateLine, RejectsUnknownTypeAndBadCounts)
{
    EXPECT_EQ(Parse("gate g1 NAN #2 y = a b"), STATUS_UNKNOWN_TYPE);
    EXPECT_EQ(Parse("gate g1 NOT #2 y = a b"), STATUS_INPUT_COUNT);
    EXPECT_EQ(Parse("gate g1 NAND #2 y = a b c d e"), STATUS_INPUT_COUNT);
    EXPECT_EQ(Parse("gate g1 NAND #2 y : a b"), STATUS_SYNTAX);
    EXPECT_EQ(nl.net_count, 0U);
    EXPECT_EQ(nl.gate_count, 0U);
}

TEST_F(GateLine, RejectsDuplicateNameAndSecondDriver)
{
    LoadSrLatch();
    EXPECT_EQ(Parse("gate n1 NOT #1 T = S"), STATUS_DUPLICATE);
    EXPECT_STREQ(diag.text, "name already used 'n1'");
    EXPECT_EQ(Parse("gate n3 NOT #1 Q = S"), STATUS_TWO_DRIVERS);
    EXPECT_EQ(Parse("gate n4 NOT #1 S = R"), STATUS_TWO_DRIVERS);
    EXPECT_EQ(Parse("input T Q"), STATUS_TWO_DRIVERS);
    EXPECT_EQ(Parse("input T S"), STATUS_DUPLICATE);
    EXPECT_EQ(Parse("output T Q"), STATUS_DUPLICATE);
    EXPECT_EQ(Parse("output T T"), STATUS_DUPLICATE);
    EXPECT_EQ(nl.net_count, 4U);
    EXPECT_EQ(nl.gate_count, 2U);
    EXPECT_EQ(nl.input_count, 2U);
    EXPECT_EQ(nl.output_count, 2U);
}

TEST_F(ParseLine, SrLatchTablesAsExpected)
{
    const char *const net_names[] = {"S", "R", "Q", "QN"};

    LoadSrLatch();
    ASSERT_EQ(nl.net_count, 4U);
    for (uint16_t i = 0U; i < 4U; i++)
    {
        EXPECT_EQ(NetName(i), net_names[i]);
    }
    EXPECT_EQ(nl.nets[0].driver, DRV_INPUT);
    EXPECT_TRUE(nl.nets[2].is_output);
    ASSERT_EQ(nl.gate_count, 2U);
    EXPECT_EQ(nl.gates[0].in[0], 1U);
    EXPECT_EQ(nl.gates[0].in[1], 3U);
    EXPECT_EQ(nl.gates[0].out, 2U);
    EXPECT_EQ(nl.gates[1].in[0], 0U);
    EXPECT_EQ(nl.gates[1].in[1], 2U);
    EXPECT_EQ(nl.gates[1].out, 3U);
}

TEST_F(ParseLine, RejectsUnknownKeyword)
{
    EXPECT_EQ(Parse("wire a"), STATUS_UNKNOWN_KEYWORD);
    EXPECT_STREQ(diag.text, "unknown keyword 'wire'");
    EXPECT_EQ(nl.net_count, 0U);
}

TEST_F(LoadNetlist, Ring5FromFile)
{
    diag.line = SENTINEL;
    ASSERT_EQ(nl_load(&nl, "data/ring5.net", &diag), STATUS_OK) << diag.text;
    EXPECT_EQ(diag.line, SENTINEL);
    EXPECT_EQ(nl.gate_count, 5U);
    EXPECT_EQ(nl.net_count, 6U);
    EXPECT_EQ(nl.input_count, 1U);
    EXPECT_EQ(nl.output_count, 5U);
    EXPECT_EQ(nl.warning_count, 0U);
    EXPECT_EQ(NetName(0U), "EN");
    EXPECT_EQ(nl.gates[0].type, GT_NAND);
}

TEST_F(LoadNetlist, ErrorHasFileAndLine)
{
    EXPECT_EQ(nl_load(&nl, "tests/data/bad_type.net", &diag), STATUS_UNKNOWN_TYPE);
    EXPECT_STREQ(diag.file, "tests/data/bad_type.net");
    EXPECT_EQ(diag.line, 3U);
    EXPECT_STREQ(diag.text, "unknown gate type 'NAN'");

    diag.line = SENTINEL;
    EXPECT_EQ(nl_load(&nl, "tests/data/no_such_file.net", &diag), STATUS_FILE_OPEN);
    EXPECT_STREQ(diag.file, "tests/data/no_such_file.net");
    EXPECT_EQ(diag.line, 0U);
    EXPECT_STREQ(diag.text, "cannot open file");
}

TEST_F(CheckNetlist, RejectsUndrivenNet)
{
    EXPECT_EQ(nl_load(&nl, "tests/data/bad_undriven.net", &diag), STATUS_UNDRIVEN);
    EXPECT_STREQ(diag.file, "tests/data/bad_undriven.net");
    EXPECT_EQ(diag.line, 0U);
    EXPECT_NE(std::string(diag.text).find("'b'"), std::string::npos) << diag.text;
}

TEST_F(CheckNetlist, WarnsUnusedGateOutput)
{
    ASSERT_EQ(nl_load(&nl, "tests/data/warn_unused.net", &diag), STATUS_OK) << diag.text;
    ASSERT_EQ(nl.warning_count, 1U);
    EXPECT_EQ(NetName(nl.unused[0]), "t");
}

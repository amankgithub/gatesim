/**
 * @file    test_netlist.cpp
 * @brief   Unit tests for netlist.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "netlist.h"
#include "text_utils.h"

namespace
{
/** Value written into an output before a call that must fail, to see that it stays unchanged. */
constexpr uint16_t SENTINEL = 77U;

/** The SR latch, one line per entry. */
const char *const SR_LATCH_LINES[] = {
    "input  S R",
    "output Q QN",
    "gate n1 NOR #2  Q  = R QN",
    "gate n2 NOR #2  QN = S Q",
};

/** The master-slave JK flip-flop block, as in lib/flipflops.net. */
const char *const JK_MS_LINES[] = {
    "subckt JK_MS J K CLK PRE CLR -> Q QN",
    "gate m1 NAND #2  a1 = J CLK QN CLR",
    "gate m2 NAND #2  a2 = K CLK Q PRE",
    "gate m3 NAND #2  y  = a1 yn PRE",
    "gate m4 NAND #2  yn = a2 y CLR",
    "gate i1 NOT  #1  cn = CLK",
    "gate s1 NAND #2  b1 = y cn",
    "gate s2 NAND #2  b2 = yn cn",
    "gate s3 NAND #2  Q  = b1 QN PRE",
    "gate s4 NAND #2  QN = b2 Q CLR",
    "ends",
};
}  // namespace

/** Fixture: one netlist (a static object, about 70 KB), emptied before every test. */
class NetlistTest : public ::testing::Test
{
protected:
    static Netlist_t nl;
    static NlParseState_t state;   /**< the parse state kept from one Parse to the next */
    static Diag_t diag;            /**< message of the last failed Parse or nl_load */

    void SetUp() override
    {
        ASSERT_EQ(nl_init(&nl), STATUS_OK);
        state = NlParseState_t{};
        diag = Diag_t{};
    }

    /** Parses one line of text (nl_parse_line changes its line, so it gets a copy). */
    static Status_t Parse(const char *text)
    {
        std::vector<char> line(text, text + std::strlen(text) + 1U);
        return nl_parse_line(&nl, &state, line.data(), &diag);
    }

    /** Parses the JK_MS block lines; every one must be accepted. */
    static void LoadJkMsBlock()
    {
        for (const char *text : JK_MS_LINES)
        {
            ASSERT_EQ(Parse(text), STATUS_OK) << text << ": " << diag.text;
        }
    }

    /** The gate with this name (the test fails if there is none). */
    static const Gate_t &GateNamed(const char *name)
    {
        uint16_t index = 0U;
        EXPECT_EQ(nl_find_gate(&nl, name, &index), STATUS_OK) << name;
        return nl.gates[index];
    }

    /** Parses the SR latch lines; every one must be accepted. */
    static void LoadSrLatch()
    {
        for (const char *text : SR_LATCH_LINES)
        {
            ASSERT_EQ(Parse(text), STATUS_OK) << text;
        }
    }

    /** Splits a gate line and fills a GateSpec_t with nl_parse_gate_words. */
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

    /** Name of the net in row index. */
    static std::string NetName(uint16_t index)
    {
        return std::string(nl.nets[index].name);
    }
};

Netlist_t NetlistTest::nl;
NlParseState_t NetlistTest::state;
Diag_t NetlistTest::diag;

/* One fixture name per group of tests (FindOrAdd.*, GateLine.*, ...), all sharing NetlistTest. */
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
class Block : public NetlistTest
{
};
class Inst : public NetlistTest
{
};
class Subckt : public NetlistTest
{
};
class Include : public NetlistTest
{
};
class Library : public NetlistTest
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

    /* one free slot, a gate with two new nets (y, z): rejected before anything is added */
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

    /* one new net fits */
    ASSERT_EQ(SpecFromLine("gate g1 AND #1 y = n0 n1", &spec), STATUS_OK);
    EXPECT_EQ(nl_add_gate(&nl, &spec), STATUS_OK);
    EXPECT_EQ(nl.net_count, MAX_NETS);
    EXPECT_EQ(nl.gate_count, 1U);
}

TEST_F(FindOrAdd, VccAndGndAreConstants)
{
    uint16_t vcc = SENTINEL;
    uint16_t gnd = SENTINEL;
    uint16_t other = SENTINEL;

    /* created on first mention as constants: driven, with their value from the start */
    ASSERT_EQ(nl_find_or_add_net(&nl, "VCC", &vcc), STATUS_OK);
    ASSERT_EQ(nl_find_or_add_net(&nl, "GND", &gnd), STATUS_OK);
    ASSERT_EQ(nl_find_or_add_net(&nl, "A", &other), STATUS_OK);
    EXPECT_EQ(nl.nets[vcc].driver, DRV_CONST);
    EXPECT_EQ(nl.nets[vcc].init_value, LV_1);
    EXPECT_EQ(nl.nets[gnd].driver, DRV_CONST);
    EXPECT_EQ(nl.nets[gnd].init_value, LV_0);
    EXPECT_EQ(nl.nets[other].driver, DRV_NONE);
    EXPECT_EQ(nl.nets[other].init_value, LV_X);
    /* only the exact names: "vcc" is an ordinary net */
    ASSERT_EQ(nl_find_or_add_net(&nl, "vcc", &other), STATUS_OK);
    EXPECT_EQ(nl.nets[other].driver, DRV_NONE);
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
    EXPECT_EQ(Parse("gate n1 NOT #1 T = S"), STATUS_DUPLICATE);     /* the name n1 is taken */
    EXPECT_STREQ(diag.text, "name already used 'n1'");
    EXPECT_EQ(Parse("gate n3 NOT #1 Q = S"), STATUS_TWO_DRIVERS);   /* Q is driven by n1 */
    EXPECT_EQ(Parse("gate n4 NOT #1 S = R"), STATUS_TWO_DRIVERS);   /* S is an input */
    EXPECT_EQ(Parse("gate n5 NOT #1 VCC = R"), STATUS_TWO_DRIVERS); /* VCC is a constant */
    EXPECT_STREQ(diag.text, "net already has a driver 'VCC'");
    EXPECT_EQ(Parse("input GND"), STATUS_TWO_DRIVERS);
    /* an input line is checked completely before any name is added */
    EXPECT_EQ(Parse("input T Q"), STATUS_TWO_DRIVERS);
    EXPECT_EQ(Parse("input T S"), STATUS_DUPLICATE);
    /* an output is declared once */
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
    EXPECT_EQ(diag.line, SENTINEL);   /* the message is written only on an error */
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
    /* g1 reads b, but no input line and no gate drives it */
    EXPECT_EQ(nl_load(&nl, "tests/data/bad_undriven.net", &diag), STATUS_UNDRIVEN);
    EXPECT_STREQ(diag.file, "tests/data/bad_undriven.net");
    EXPECT_EQ(diag.line, 0U);   /* the whole circuit is checked after the last line */
    EXPECT_NE(std::string(diag.text).find("'b'"), std::string::npos) << diag.text;
}

TEST_F(CheckNetlist, WarnsUnusedGateOutput)
{
    /* g2 drives t, which no gate reads and which is not an output */
    ASSERT_EQ(nl_load(&nl, "tests/data/warn_unused.net", &diag), STATUS_OK) << diag.text;
    ASSERT_EQ(nl.warning_count, 1U);
    EXPECT_EQ(NetName(nl.unused[0]), "t");
}

TEST_F(Block, StoresPortsAndGates)
{
    const char *const ports[] = {"J", "K", "CLK", "PRE", "CLR", "Q", "QN"};

    ASSERT_EQ(Parse(JK_MS_LINES[0]), STATUS_OK) << diag.text;
    EXPECT_TRUE(state.in_block);
    EXPECT_EQ(state.block, 0U);
    for (size_t i = 1U; i < (sizeof(JK_MS_LINES) / sizeof(JK_MS_LINES[0])); i++)
    {
        ASSERT_EQ(Parse(JK_MS_LINES[i]), STATUS_OK) << JK_MS_LINES[i] << ": " << diag.text;
    }
    EXPECT_FALSE(state.in_block);

    ASSERT_EQ(nl.block_count, 1U);
    const Block_t &b = nl.blocks[0];
    EXPECT_STREQ(b.name, "JK_MS");
    ASSERT_EQ(b.port_count, 7U);
    for (uint32_t i = 0U; i < 7U; i++)
    {
        EXPECT_STREQ(b.ports[i], ports[i]);
    }
    EXPECT_EQ(b.first_output_port, 5U);   /* Q and QN come after "->" */
    ASSERT_EQ(b.gate_count, 9U);
    EXPECT_STREQ(b.gates[0].name, "m1");
    EXPECT_STREQ(b.gates[0].out, "a1");
    EXPECT_EQ(b.gates[0].in_count, 4U);
    EXPECT_STREQ(b.gates[0].in[3], "CLR");
    EXPECT_EQ(b.gates[4].type, GT_NOT);
    /* a block is only stored: no gate and no net of the circuit yet */
    EXPECT_EQ(nl.gate_count, 0U);
    EXPECT_EQ(nl.net_count, 0U);
    uint8_t found = static_cast<uint8_t>(SENTINEL);
    ASSERT_EQ(nl_find_block(&nl, "JK_MS", &found), STATUS_OK);
    EXPECT_EQ(found, 0U);
}

TEST_F(Inst, ExpandsNamesWithPrefix)
{
    LoadJkMsBlock();
    ASSERT_EQ(Parse("inst FF2 JK_MS J=Q1 K=Q1 CLK=CLK PRE=P2 CLR=C2 Q=Q2 QN=Q2N"), STATUS_OK) << diag.text;
    EXPECT_EQ(nl.gate_count, 9U);
    EXPECT_EQ(nl.inst_count, 1U);

    /* a port becomes the connected net, an inner net INST.name, a gate INST.gate */
    const Gate_t &m1 = GateNamed("FF2.m1");
    ASSERT_EQ(m1.in_count, 4U);
    EXPECT_EQ(NetName(m1.in[0]), "Q1");
    EXPECT_EQ(NetName(m1.in[1]), "CLK");
    EXPECT_EQ(NetName(m1.in[2]), "Q2N");
    EXPECT_EQ(NetName(m1.in[3]), "C2");
    EXPECT_EQ(NetName(m1.out), "FF2.a1");

    const Gate_t &s3 = GateNamed("FF2.s3");
    ASSERT_EQ(s3.in_count, 3U);
    EXPECT_EQ(NetName(s3.in[0]), "FF2.b1");
    EXPECT_EQ(NetName(s3.in[1]), "Q2N");
    EXPECT_EQ(NetName(s3.in[2]), "P2");
    EXPECT_EQ(NetName(s3.out), "Q2");
    EXPECT_EQ(nl.nets[s3.out].driver, DRV_GATE);

    const Gate_t &i1 = GateNamed("FF2.i1");
    EXPECT_EQ(i1.rise, 1U);
    EXPECT_EQ(i1.fall, 1U);

    /* VCC inside a block is the circuit's VCC, never renamed (P1.VCC would be an undriven net) */
    ASSERT_EQ(Parse("subckt TIE A -> Y"), STATUS_OK);
    ASSERT_EQ(Parse("gate g AND #1 Y = A VCC"), STATUS_OK);
    ASSERT_EQ(Parse("ends"), STATUS_OK);
    ASSERT_EQ(Parse("inst P1 TIE A=a Y=y"), STATUS_OK) << diag.text;
    const Gate_t &g = GateNamed("P1.g");
    ASSERT_EQ(g.in_count, 2U);
    EXPECT_EQ(NetName(g.in[0]), "a");
    EXPECT_EQ(NetName(g.in[1]), "VCC");
    EXPECT_EQ(nl.nets[g.in[1]].driver, DRV_CONST);
    uint16_t index = SENTINEL;
    EXPECT_EQ(nl_find_net(&nl, "P1.VCC", &index), STATUS_NOT_FOUND);
}

TEST_F(Inst, RejectsBadPorts)
{
    LoadJkMsBlock();
    EXPECT_EQ(Parse("inst FF JK_MS J=a K=b CLK=c PRE=d Q=e QN=f"), STATUS_BAD_PORT);   /* CLR missing */
    EXPECT_STREQ(diag.text, "port missing, unknown or connected twice 'CLR'");
    EXPECT_EQ(Parse("inst FF JK_MS J=a K=b CLK=c PRE=d CLR=g Q=e QN=f X=Y"), STATUS_BAD_PORT);
    EXPECT_STREQ(diag.text, "port missing, unknown or connected twice 'X'");
    EXPECT_EQ(Parse("inst FF JK_MS J=a J=b K=b CLK=c PRE=d CLR=g Q=e QN=f"), STATUS_BAD_PORT);
    EXPECT_EQ(Parse("inst FF JK_MS J K=b CLK=c PRE=d CLR=g Q=e QN=f"), STATUS_SYNTAX);
    EXPECT_STREQ(diag.text, "syntax error 'J'");
    EXPECT_EQ(Parse("inst FF JK_XX J=a K=b CLK=c PRE=d CLR=g Q=e QN=f"), STATUS_NOT_FOUND);
    EXPECT_STREQ(diag.text, "name not found 'JK_XX'");
    EXPECT_EQ(Parse("inst FF JK_MS"), STATUS_SYNTAX);
    /* the ports are checked before any gate is added */
    EXPECT_EQ(nl.gate_count, 0U);
    EXPECT_EQ(nl.net_count, 0U);
    EXPECT_EQ(nl.inst_count, 0U);
}

TEST_F(Subckt, RejectsNestingAndUnclosedBlock)
{
    ASSERT_EQ(Parse("subckt INV A -> Y"), STATUS_OK);
    ASSERT_EQ(Parse("gate g1 NOT #1  Y = A"), STATUS_OK);
    /* a block holds only gate lines */
    EXPECT_EQ(Parse("inst U1 INV A=a Y=y"), STATUS_NESTING);
    EXPECT_STREQ(diag.text, "not allowed here 'inst'");
    EXPECT_EQ(Parse("subckt BUF2 A -> Y"), STATUS_NESTING);
    EXPECT_EQ(Parse("input a"), STATUS_NESTING);
    ASSERT_EQ(Parse("ends"), STATUS_OK);
    EXPECT_EQ(Parse("ends"), STATUS_NESTING);   /* no open block */
    EXPECT_EQ(nl.block_count, 1U);
    EXPECT_EQ(nl.net_count, 0U);

    /* the file ends inside a block */
    EXPECT_EQ(nl_load(&nl, "tests/data/unclosed_block.net", &diag), STATUS_NESTING);
    EXPECT_STREQ(diag.file, "tests/data/unclosed_block.net");
    EXPECT_EQ(diag.line, 3U);
    EXPECT_STREQ(diag.text, "not allowed here 'subckt INV not closed'");
}

TEST_F(Include, LoadsLibraryRelativeToFile)
{
    /* data/jk_ms.net has "include ../lib/flipflops.net": relative to data/, not to the working folder */
    ASSERT_EQ(nl_load(&nl, "data/jk_ms.net", &diag), STATUS_OK) << diag.file << ":" << diag.line << ": " << diag.text;
    EXPECT_EQ(nl.block_count, 4U);   /* JK_MS, JK_LEVEL, SR_NOR, D_LATCH */
    EXPECT_EQ(nl.gate_count, 9U);
    EXPECT_EQ(nl.inst_count, 1U);
    uint16_t index = SENTINEL;
    EXPECT_EQ(nl_find_net(&nl, "FF.y", &index), STATUS_OK);   /* an inner net of the instance FF */
    EXPECT_EQ(nl_find_net(&nl, "Q", &index), STATUS_OK);
    EXPECT_EQ(nl.nets[index].driver, DRV_GATE);
}

TEST_F(Include, RejectsNestedInclude)
{
    /* nested_include.net includes include_again.net, whose line 2 is another include */
    EXPECT_EQ(nl_load(&nl, "tests/data/nested_include.net", &diag), STATUS_NESTING);
    EXPECT_STREQ(diag.file, "tests/data/include_again.net");
    EXPECT_EQ(diag.line, 2U);
    EXPECT_STREQ(diag.text, "not allowed here 'include'");
}

TEST_F(Library, CounterAndAdderSizes)
{
    /* the 0-3-5-7 counter: 3 x 9 gates + K0, and VCC created on first mention */
    ASSERT_EQ(nl_load(&nl, "data/counter_0357.net", &diag), STATUS_OK) << diag.text;
    EXPECT_EQ(nl.gate_count, 28U);
    EXPECT_EQ(nl.net_count, 36U);
    uint16_t vcc = SENTINEL;
    ASSERT_EQ(nl_find_net(&nl, "VCC", &vcc), STATUS_OK);
    EXPECT_EQ(nl.nets[vcc].driver, DRV_CONST);

    ASSERT_EQ(nl_load(&nl, "data/adder4.net", &diag), STATUS_OK) << diag.text;
    EXPECT_EQ(nl.gate_count, 20U);
    EXPECT_EQ(nl.net_count, 29U);
    EXPECT_EQ(nl.block_count, 2U);
    EXPECT_EQ(nl.inst_count, 4U);

    ASSERT_EQ(nl_load(&nl, "data/mod6_ripple.net", &diag), STATUS_OK) << diag.text;
    EXPECT_EQ(nl.gate_count, 29U);
    EXPECT_EQ(nl.net_count, 32U);
}

#!/usr/bin/env python3
"""net2v.py : turn a GateSim netlist + stimulus into a Verilog testbench for Icarus Verilog.

Usage: python3 tools/net2v.py NETLIST STIMULUS -o OUT.v [--vcd ICARUS.vcd]

The netlist is read with GateSim's grammar: the first '#' not followed by a digit starts a comment,
`include` is relative to the including file, a block (`subckt` ... `ends`) is copied by `inst` with
the same renaming (a port becomes the connected net, VCC/GND stay, any other name becomes INST.name).
The stimulus may have `set`, `clock` and `end` lines; any other keyword is an error.

The testbench has one Verilog primitive per gate with GateSim's rise and fall delays
(`nand #(2,2) g1(out, in1, in2);`), so Icarus uses the same inertial delay, and one initial block
with every input change in time order, up to the end time. Names are made Verilog-safe: every
character outside [A-Za-z0-9_] becomes '_' (GateSim names start with a letter or '_').

Exit codes: 0 written; 2 usage, file or format error, or two names that map to the same Verilog name.
"""
import argparse
import os
import re
import sys

# GateSim gate type -> Verilog primitive
PRIMITIVES = {"AND": "and", "OR": "or", "NAND": "nand", "NOR": "nor",
              "XOR": "xor", "XNOR": "xnor", "NOT": "not", "BUF": "buf"}
# the constant nets and their values
CONSTANTS = {"VCC": "1", "GND": "0"}
# Verilog words that a safe name must not be
VERILOG_KEYWORDS = {"always", "and", "assign", "begin", "buf", "case", "default", "else", "end", "endcase",
                    "endmodule", "for", "if", "initial", "inout", "input", "integer", "module", "nand", "nor",
                    "not", "or", "output", "reg", "time", "wire", "xnor", "xor"}


class InputError(Exception):
    """A problem with the input files or the names (exit code 2)."""


def cut_comment(line):
    """Cut the comment: the first '#' that is not followed by a digit (GateSim's rule)."""
    for i, ch in enumerate(line):
        if ch == "#" and not (i + 1 < len(line) and line[i + 1].isdigit()):
            return line[:i]
    return line


def words_of(path):
    """Yield (line number, words) for every non-empty line of a file."""
    try:
        with open(path) as f:
            for number, raw in enumerate(f, start=1):
                words = cut_comment(raw.rstrip("\r\n")).split()
                if words:
                    yield number, words
    except OSError as err:
        raise InputError("%s: cannot open file (%s)" % (path, err.strerror))


def safe_name(name):
    """GateSim name -> Verilog identifier: every character outside [A-Za-z0-9_] becomes '_'."""
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


class Netlist:
    """Inputs, outputs and gates after every inst line is expanded."""

    def __init__(self):
        self.inputs = []     # input nets in declaration order
        self.nets = []       # every net in order of first mention
        self.gates = []      # (name, type, rise, fall, out, [ins])
        self.blocks = {}     # block name -> (ports, [gate tuples with inner names])

    def mention(self, name):
        if name not in self.nets:
            self.nets.append(name)

    def add_gate(self, name, typ, rise, fall, out, ins):
        self.mention(out)
        for n in ins:
            self.mention(n)
        self.gates.append((name, typ, rise, fall, out, ins))


def parse_gate(words, where):
    """gate NAME TYPE #d OUT = IN ... -> (name, type, rise, fall, out, [ins])."""
    if len(words) < 7 or words[5] != "=" or not words[3].startswith("#") or words[2] not in PRIMITIVES:
        raise InputError("%s: not a gate line GateSim accepts: %s" % (where, " ".join(words)))
    delays = words[3][1:].split(",")
    rise, fall = int(delays[0]), int(delays[-1])
    return (words[1], words[2], rise, fall, words[4], words[6:])


def read_netlist(path, nl, depth=0):
    """Read one .net file into nl (an include is read where it appears)."""
    block = None
    for number, words in words_of(path):
        where = "%s:%d" % (path, number)
        key = words[0]
        if key == "include":
            if depth > 0:
                raise InputError("%s: an included file may not include another one" % where)
            read_netlist(os.path.join(os.path.dirname(path), words[1]), nl, depth + 1)
        elif key == "input":
            for n in words[1:]:
                nl.inputs.append(n)
                nl.mention(n)
        elif key == "output":
            for n in words[1:]:
                nl.mention(n)
        elif key == "subckt":
            block = (words[1], [p for p in words[2:] if p != "->"], [])
            nl.blocks[words[1]] = (block[1], block[2])
        elif key == "ends":
            block = None
        elif key == "gate":
            gate = parse_gate(words, where)
            if block is not None:
                block[2].append(gate)
            else:
                nl.add_gate(*gate)
        elif key == "inst":
            inst, block_name = words[1], words[2]
            if block_name not in nl.blocks:
                raise InputError("%s: unknown block %s" % (where, block_name))
            ports, gates = nl.blocks[block_name]
            connected = dict(w.split("=", 1) for w in words[3:])

            def rename(n):
                if n in connected:
                    return connected[n]
                if n in CONSTANTS:
                    return n
                return inst + "." + n
            for (name, typ, rise, fall, out, ins) in gates:
                nl.add_gate(inst + "." + name, typ, rise, fall, rename(out), [rename(n) for n in ins])
        else:
            raise InputError("%s: unknown keyword %s" % (where, key))
    return nl


def read_stimulus(path):
    """set / clock / end lines -> (sets [(time, net, value)], clocks [(net, period, high, start)], end)."""
    sets, clocks, end = [], [], None
    for number, words in words_of(path):
        where = "%s:%d" % (path, number)
        key = words[0]
        if key == "set" and len(words) == 4 and words[3].startswith("@"):
            sets.append((int(words[3][1:]), words[1], words[2]))
        elif key == "clock" and len(words) == 8:
            clocks.append((words[1], int(words[3]), int(words[5]), int(words[7])))
        elif key == "end" and len(words) == 2:
            end = int(words[1])
        else:
            raise InputError("%s: net2v.py does not support this stimulus line: %s" % (where, " ".join(words)))
    if end is None:
        raise InputError("%s: missing end line" % path)
    return sets, clocks, end


def input_changes(sets, clocks, end):
    """Every input change up to the end time, in time order. Same-time changes keep GateSim's order
    (set lines first, then clocks), so the last one wins in both simulators."""
    changes = [(t, n, v) for (t, n, v) in sets if t <= end]
    for (net, period, high, start) in clocks:
        if start > 0:
            changes.append((0, net, "0"))      # driven low until the first rise
        rise = start
        while rise <= end:
            changes.append((rise, net, "1"))
            if rise + high <= end:
                changes.append((rise + high, net, "0"))
            rise += period
    return sorted(changes, key=lambda change: change[0])   # stable: same-time order is kept


def check_names(nl):
    """Every net and gate needs its own Verilog name; a collision is an error (never merged)."""
    seen = {}
    for kind, names in (("net", nl.nets), ("gate", [g[0] for g in nl.gates])):
        for name in names:
            safe = safe_name(name)
            if safe in VERILOG_KEYWORDS:
                raise InputError("name is a Verilog keyword: %s" % name)
            if safe in seen and seen[safe] != (kind, name):
                raise InputError("name collision: %s and %s" % (seen[safe][1], name))
            seen[safe] = (kind, name)


def testbench(nl, changes, end, vcd_path):
    """The Verilog text."""
    regs = [n for n in nl.inputs] + [n for n in nl.nets if n in CONSTANTS]
    lines = ["`timescale 1ns/1ns", "module tb;"]
    for n in regs:
        lines.append("  reg %s;" % safe_name(n))
    for n in nl.nets:
        if n not in regs:
            lines.append("  wire %s;" % safe_name(n))
    for (name, typ, rise, fall, out, ins) in nl.gates:
        pins = ", ".join(safe_name(n) for n in [out] + ins)
        lines.append("  %s #(%d,%d) %s(%s);" % (PRIMITIVES[typ], rise, fall, safe_name(name), pins))
    lines.append("  initial begin $dumpfile(\"%s\"); $dumpvars(0, tb); end" % vcd_path)
    lines.append("  initial begin")
    for n in nl.nets:
        if n in CONSTANTS:
            lines.append("    %s = 1'b%s;" % (n, CONSTANTS[n]))
    now = 0
    for (t, n, v) in changes:
        if t > now:
            lines.append("    #%d;" % (t - now))
            now = t
        lines.append("    %s = 1'b%s;" % (safe_name(n), v))
    lines.append("    #%d $finish;" % (end - now + 1))
    lines.append("  end")
    lines.append("endmodule")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description="GateSim netlist + stimulus -> Verilog testbench")
    parser.add_argument("netlist")
    parser.add_argument("stimulus")
    parser.add_argument("-o", dest="out", required=True, help="the Verilog file to write")
    parser.add_argument("--vcd", default="icarus.vcd", help="the VCD file the testbench writes")
    args = parser.parse_args()   # a usage error exits with 2
    code = 0
    try:
        nl = read_netlist(args.netlist, Netlist())
        for n in nl.inputs:
            if n in CONSTANTS:
                raise InputError("%s: %s cannot be an input" % (args.netlist, n))
        sets, clocks, end = read_stimulus(args.stimulus)
        check_names(nl)
        text = testbench(nl, input_changes(sets, clocks, end), end, args.vcd)
        with open(args.out, "w") as f:
            f.write(text)
    except (InputError, ValueError) as err:
        print("net2v.py: %s" % err, file=sys.stderr)
        code = 2
    except OSError as err:
        print("net2v.py: %s: %s" % (args.out, err.strerror), file=sys.stderr)
        code = 2
    return code


if __name__ == "__main__":
    sys.exit(main())

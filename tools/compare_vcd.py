#!/usr/bin/env python3
"""compare_vcd.py : compare GateSim's waveform with Icarus Verilog's, net by net.

Usage: python3 tools/compare_vcd.py GATESIM_VCD ICARUS_VCD [--end T] [--name LABEL]

Both files are read the same way: $var lines (codes of any length), the $dumpvars values, "#t"
lines and scalar changes (0 1 x z). Each net gets a history of (time, value): several changes at
one time keep the last one, and a value equal to the one before is dropped. GateSim names are
mapped to Verilog names with net2v.py's rule (every character outside [A-Za-z0-9_] becomes '_').
Every GateSim net is compared up to T (default: the last "#t" of the GateSim file, which is
where GateSim stopped).

Prints one "DIFF <net>" block per difference and a summary line.
Exit codes: 0 all nets match; 1 some differ; 2 usage or file error, or two GateSim names that map
to the same Verilog name.
"""
import argparse
import re
import sys


class InputError(Exception):
    """A problem with the files or the names (exit code 2)."""


def safe_name(name):
    """GateSim name -> Verilog identifier (the rule of net2v.py)."""
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def read_vcd(path):
    """Return ({name: [(time, value)]}, last time seen)."""
    names_of_code = {}
    history = {}
    now = 0
    try:
        with open(path) as f:
            for raw in f:
                line = raw.strip()
                if line.startswith("$var"):
                    words = line.split()          # $var wire 1 <code> <name> $end
                    if len(words) < 5:
                        raise InputError("%s: bad $var line: %s" % (path, line))
                    names_of_code.setdefault(words[3], []).append(words[4])
                elif line.startswith("#"):
                    now = int(line[1:])
                elif line and line[0] in "01xzXZ" and len(line) > 1:
                    value, code = line[0].lower(), line[1:]
                    for name in names_of_code.get(code, []):
                        h = history.setdefault(name, [])
                        if h and h[-1][0] == now:
                            h[-1] = (now, value)      # same time: the last change wins
                        else:
                            h.append((now, value))
    except OSError as err:
        raise InputError("%s: cannot open file (%s)" % (path, err.strerror))
    except ValueError:
        raise InputError("%s: bad time line" % path)
    clean = {}
    for name, h in history.items():
        kept = []
        for (t, v) in h:
            if not kept or kept[-1][1] != v:
                kept.append((t, v))                   # a repeated value is no change
        clean[name] = kept
    return clean, now


def upto(history, end):
    return [(t, v) for (t, v) in history if t <= end]


def text_of(history):
    return " ".join("%d:%s" % (t, v) for (t, v) in history) or "(no value)"


def main():
    parser = argparse.ArgumentParser(description="compare a GateSim VCD with an Icarus VCD")
    parser.add_argument("gatesim_vcd")
    parser.add_argument("icarus_vcd")
    parser.add_argument("--end", type=int, default=None, help="compare up to this time, ns")
    parser.add_argument("--name", default=None, help="label of the summary line")
    args = parser.parse_args()   # a usage error exits with 2
    label = args.name if args.name is not None else args.gatesim_vcd
    code = 0
    try:
        gatesim, last_time = read_vcd(args.gatesim_vcd)
        icarus, _ = read_vcd(args.icarus_vcd)
        end = last_time if args.end is None else args.end
        seen = {}
        for name in gatesim:
            safe = safe_name(name)
            if safe in seen:
                raise InputError("name collision: %s and %s" % (seen[safe], name))
            seen[safe] = name
        differ = 0
        for name in sorted(gatesim):
            mine = upto(gatesim[name], end)
            theirs = upto(icarus.get(safe_name(name), []), end)
            if mine != theirs:
                differ += 1
                print("DIFF %s" % name)
                print("  gatesim: %s" % text_of(mine))
                print("  icarus : %s" % text_of(theirs))
        if differ == 0:
            print("%s: %d nets compared up to %d ns, all match" % (label, len(gatesim), end))
        else:
            print("%s: %d nets compared up to %d ns, %d differ" % (label, len(gatesim), end, differ))
            code = 1
    except InputError as err:
        print("compare_vcd.py: %s" % err, file=sys.stderr)
        code = 2
    return code


if __name__ == "__main__":
    sys.exit(main())

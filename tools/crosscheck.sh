#!/bin/sh
# crosscheck.sh : run every demo circuit in GateSim and in Icarus Verilog and compare the waveforms.
#
# Usage (from the repo root): sh tools/crosscheck.sh [GATESIM_BIN]     (make crosscheck)
# Each pair is "netlist stimulus" (data/<netlist>.net + data/<stimulus>.stim). Per pair:
#   build/crosscheck/<stimulus>__<netlist>/  waves.vcd (GateSim), tb.v + icarus.vcd (Icarus)
# Prints MATCH or DIFF per pair. Exit codes: 0 every pair matches; 1 a pair differs or fails;
# 2 usage error or iverilog/vvp/python3 missing.
# POSIX sh only (make runs it with sh, which is dash on Ubuntu): no arrays, no [[ ]].

GATESIM=${1:-build/release/gatesim}
OUT_ROOT=build/crosscheck

PAIRS="inv_pulse:inv_pulse
inv_risefall:inv_risefall
sr_nor:sr_nor
sr_nor:sr_nor_skew
ring5:ring5
jk_level:jk_level
jk_level:jk_level_narrow
jk_ms:jk_ms
mux_hazard:mux_hazard
mux_fast:mux_hazard
mux_consensus:mux_hazard
mod6_ripple:mod6_ripple
adder4:adder4
counter_0357:counter_s0
counter_0357:counter_s1
counter_0357:counter_s2
counter_0357:counter_s3
counter_0357:counter_s4
counter_0357:counter_s5
counter_0357:counter_s6
counter_0357:counter_s7"

if [ ! -x "$GATESIM" ]; then
    echo "crosscheck: $GATESIM not found (run make all first)" >&2
    exit 2
fi
for tool in iverilog vvp python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "crosscheck: $tool is missing; on Ubuntu: sudo apt install iverilog python3" >&2
        exit 2
    fi
done

status=0
for pair in $PAIRS; do
    net=${pair%%:*}
    stim=${pair#*:}
    dir="$OUT_ROOT/${stim}__${net}"
    result=MATCH
    # the C99 program cannot create folders
    if ! mkdir -p "$dir"; then
        result=DIFF
    fi
    if [ "$result" = MATCH ]; then
        "$GATESIM" "data/$net.net" "data/$stim.stim" --out "$dir" >"$dir/gatesim.txt" 2>&1
        code=$?
        if [ "$code" -ne 0 ]; then
            echo "  gatesim exited with $code (see $dir/gatesim.txt)"
            result=DIFF
        fi
    fi
    if [ "$result" = MATCH ]; then
        if ! python3 tools/net2v.py "data/$net.net" "data/$stim.stim" -o "$dir/tb.v" --vcd "$dir/icarus.vcd"; then
            result=DIFF
        fi
    fi
    if [ "$result" = MATCH ]; then
        if ! iverilog -o "$dir/tb.vvp" "$dir/tb.v"; then
            result=DIFF
        fi
    fi
    if [ "$result" = MATCH ]; then
        if ! vvp -n "$dir/tb.vvp" >"$dir/vvp.txt" 2>&1; then
            echo "  vvp failed (see $dir/vvp.txt)"
            result=DIFF
        fi
    fi
    if [ "$result" = MATCH ]; then
        if ! python3 tools/compare_vcd.py "$dir/waves.vcd" "$dir/icarus.vcd" --name "$net + $stim" \
                >"$dir/compare.txt"; then
            cat "$dir/compare.txt"
            result=DIFF
        fi
    fi
    if [ "$result" = MATCH ]; then
        echo "MATCH $net + $stim: $(tail -n 1 "$dir/compare.txt" | sed 's/^.*: //')"
    else
        echo "DIFF  $net + $stim"
        status=1
    fi
done
exit $status

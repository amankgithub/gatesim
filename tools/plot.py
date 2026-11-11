#!/usr/bin/env python3
"""plot.py : draw a GateSim waveform (waves.vcd) as a picture, one row per net.

Usage: python3 tools/plot.py VCD [NET ...] [--number NET ...] [-o PNG]

NET ...           the nets to draw, top to bottom (default: every net in the file, in file order)
--number NET ...  add a last row that reads these nets as one binary number (first net = most
                  significant bit) and writes the value on each stretch where it holds; X if a bit is X
-o PNG            the picture to write (default: the VCD's folder, same name with .png)

Example (counter): python3 tools/plot.py output/waves.vcd CLK Q2 Q1 Q0 --number Q2 Q1 Q0

A value is drawn as a step line: 0 low, 1 high, X as a grey band in the middle. The picture
shows what happened; judging it (settled, oscillating, which values it runs through) is left
to the reader. The file is read with compare_vcd.py's reader (same folder).
Exit codes: 0 picture written; 2 usage or file error, or a net that is not in the file.
"""
import argparse
import os
import sys

import matplotlib
matplotlib.use("Agg")              # write a file, no window needed
import matplotlib.pyplot as plt

from compare_vcd import InputError, read_vcd

LABEL_MIN_SHARE = 0.02             # a number is written only on stretches wider than 2 % of the plot


def number_history(histories, end):
    """[(time, text)] of the nets read as one binary number, first net = most significant bit."""
    times = sorted({t for h in histories for (t, _) in h})
    result = []
    for now in times:
        bits = ""
        for h in histories:
            past = [v for (t, v) in h if t <= now]
            bits += past[-1] if past else "x"
        text = "X" if ("x" in bits or "z" in bits) else str(int(bits, 2))
        if not result or result[-1][1] != text:
            result.append((now, text))
    return [(t, v) for (t, v) in result if t <= end]


def draw_bit(ax, history, end):
    """Step line of one net; X and Z stretches as grey bands."""
    for i, (t, v) in enumerate(history):
        t_next = history[i + 1][0] if i + 1 < len(history) else end
        if v in "01":
            y = int(v)
            ax.plot([t, t_next], [y, y], color="tab:blue", linewidth=1.5)
        else:
            ax.fill_between([t, t_next], 0.25, 0.75, color="0.75")
        if i + 1 < len(history):
            ax.plot([t_next, t_next], [0, 1], color="tab:blue", linewidth=0.6)
    ax.set_ylim(-0.3, 1.3)
    ax.set_yticks([])


def draw_number(ax, history, end):
    """One box per stretch where the number holds, with the value written in it."""
    for i, (t, v) in enumerate(history):
        t_next = history[i + 1][0] if i + 1 < len(history) else end
        ax.plot([t, t_next], [0, 0], color="tab:green", linewidth=0.8)
        ax.plot([t, t_next], [1, 1], color="tab:green", linewidth=0.8)
        ax.plot([t, t], [0, 1], color="tab:green", linewidth=0.8)
        if end > 0 and (t_next - t) >= LABEL_MIN_SHARE * end:
            ax.text((t + t_next) / 2.0, 0.5, v, ha="center", va="center", fontsize=8)
    ax.set_ylim(-0.3, 1.3)
    ax.set_yticks([])


def main():
    parser = argparse.ArgumentParser(description="draw a GateSim waveform as a picture")
    parser.add_argument("vcd")
    parser.add_argument("nets", nargs="*", help="nets to draw (default: all)")
    parser.add_argument("--number", nargs="+", default=[], metavar="NET", help="add these nets read as one binary number")
    parser.add_argument("-o", dest="png", default=None, help="picture file (default: VCD name with .png)")
    args = parser.parse_args()   # a usage error exits with 2
    code = 0
    try:
        histories, end = read_vcd(args.vcd)
        names = args.nets if args.nets else list(histories)
        for name in names + args.number:
            if name not in histories:
                raise InputError("%s: no net named %s" % (args.vcd, name))
        png = args.png if args.png is not None else os.path.splitext(args.vcd)[0] + ".png"
        rows = len(names) + (1 if args.number else 0)
        fig, axes = plt.subplots(rows, 1, sharex=True, squeeze=False, figsize=(12, 0.6 * rows + 1.0))
        for i, name in enumerate(names):
            draw_bit(axes[i][0], histories[name], end)
            axes[i][0].set_ylabel(name, rotation=0, ha="right", va="center")
        if args.number:
            ax = axes[rows - 1][0]
            draw_number(ax, number_history([histories[n] for n in args.number], end), end)
            ax.set_ylabel(" ".join(args.number), rotation=0, ha="right", va="center")
        axes[rows - 1][0].set_xlim(0, max(end, 1))
        axes[rows - 1][0].set_xlabel("time (ns)")
        fig.tight_layout()
        fig.savefig(png, dpi=100)
        print("picture: %s" % png)
    except InputError as err:
        print("plot.py: %s" % err, file=sys.stderr)
        code = 2
    except OSError as err:
        print("plot.py: cannot write the picture (%s)" % err.strerror, file=sys.stderr)
        code = 2
    return code


if __name__ == "__main__":
    sys.exit(main())

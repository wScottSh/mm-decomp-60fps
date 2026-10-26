#!/usr/bin/env python3
"""Compare the load of two run.py result directories tick by tick.

For each scene and each load_ticks.tsv column, over the ticks both windows hold, prints the mean, min and
max of CAND - BASE in ms. Ticks pair by tick number, so the comparison is meaningful only when both builds
play the same game (fidelity.py tells). Also prints the change in each summary statistic of load.tsv.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import load  # noqa: E402

UNIT = {"rsp": "rsp", "rsp_audio": "rsp", "rsp_gfx": "rsp", "rdp_pipe": "rclk", "rdp_buf": "rclk"}
SKIP = {"tick", "start", "switches", "clock_reads"}


def tsv(path):
    lines = Path(path).read_text().splitlines()
    header = lines[0].split("\t")
    return [dict(zip(header, l.split("\t"))) for l in lines[1:]]


def signed(v):
    return v - load.U32 if v >= load.U32 // 2 else v


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("base", type=Path)
    p.add_argument("cand", type=Path)
    p.add_argument("--columns", help="comma-separated load_ticks.tsv columns (default: all times)")
    args = p.parse_args()
    scenes = [r["scene"] for r in tsv(args.base / "load.tsv")]
    for scene in scenes:
        base = {int(r["tick"]): r for r in tsv(args.base / scene / "load_ticks.tsv")}
        cand_path = args.cand / scene / "load_ticks.tsv"
        if not cand_path.exists():
            continue
        cand = {int(r["tick"]): r for r in tsv(cand_path)}
        common = sorted(set(base) & set(cand))
        print(f"{scene}: {len(common)} common ticks of {len(base)} base, {len(cand)} candidate")
        if not common:
            continue
        cols = args.columns.split(",") if args.columns else [c for c in base[common[0]] if c not in SKIP]
        for c in cols:
            ms = load.UNIT_MS[UNIT.get(c, "pclk")]
            d = [signed((int(cand[t][c]) - int(base[t][c])) % load.U32) * ms for t in common]
            print(f"  {c:20s} mean {sum(d) / len(d):+9.4f}  min {min(d):+9.4f}  max {max(d):+9.4f} ms")


if __name__ == "__main__":
    main()

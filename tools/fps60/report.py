#!/usr/bin/env python3
"""Print a run.py result directory as Markdown tables: per-scene presentation, tick cadence and wall time."""

import argparse
import json
from pathlib import Path


def tsv(path):
    lines = Path(path).read_text().splitlines()
    header = lines[0].split("\t")
    return [dict(zip(header, l.split("\t"))) for l in lines[1:]]


def table(cols, rows):
    out = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    out += ["| " + " | ".join(str(r[c]) for c in cols) + " |" for r in rows]
    return "\n".join(out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("results", type=Path)
    args = p.parse_args()
    meta = json.loads((args.results / "meta.json").read_text())
    summary = tsv(args.results / "summary.tsv")
    wall = {r["run"]: r for r in tsv(args.results / "wall.tsv")}

    print(f"ROM sha256 `{meta['rom_sha256'][:16]}`, runner sha256 `{meta['runner_sha256'][:16]}`, "
          f"keyframe symbol {'present' if meta['keyframe_symbol'] else 'absent'}, "
          f"noise pin {meta.get('noise_pin_mode', 'off')}.\n")
    print("Presentation (fields per displayed image, complete images in the window):\n")
    print(table(["scene", "fields", "images", "img_n1", "img_n2", "img_n3", "img_n4", "img_n5", "img_n6plus",
                 "image_share_at_1", "image_fields_max"], summary))
    print("\nTick cadence (fields per logic tick):\n")
    print(table(["scene", "divisors", "ticks", "tick_n1", "tick_n2", "tick_n3", "tick_n4", "tick_n5", "tick_n6plus",
                 "lag_ticks", "skipped_ticks", "tick_fields_max", "settle_tick", "window_start_frame"], summary))
    if meta.get("noise_pin"):
        rows = []
        for r in summary:
            sj = json.loads((args.results / r["scene"] / "summary.json").read_text())
            rows.append({"scene": r["scene"], **sj["noise_totals"],
                         "noise_cmds_per_keyframe_max": sj["noise_cmds_per_keyframe_max"],
                         "noise_patched_in_window": sj["noise_patched_in_window"]})
        print("\nNoise pin (totals since boot; per-tick values are in keyframes.tsv):\n")
        print(table(["scene", "walks", "visited", "commands", "patched", "unresolved", "unknown", "overflow",
                     "mismatch", "noise_cmds_per_keyframe_max", "noise_patched_in_window"], rows))
    print("\nWall time (host, not deterministic):\n")
    rows = [{"run": n, **wall[n]} for n in [s["scene"] for s in summary] + ["total"]]
    print(table(["run", "wall_s", "jobs", "load_before", "load_after"], rows))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Print a run.py result directory as Markdown tables: per-scene presentation, tick cadence, load and wall time."""

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


def print_load(results, scenes):
    loads = {n: json.loads((results / n / "load_summary.json").read_text()) for n in scenes}
    resources = ["cpu", "rsp", "rsp_gfx", "rsp_audio", "rdp_pipe", "rdp_buf"]
    for scope, title in (("per_field", "per field"), ("per_tick", "per logic tick")):
        print(f"\nLoad {title}: max / p99 / p50 in ms (share of one field):\n")
        rows = []
        for n in scenes:
            row = {"scene": n}
            for r in resources:
                v = loads[n][scope][r]
                row[r] = (f"{v['max_ms']:.2f} / {v['p99_ms']:.2f} / {v['p50_ms']:.2f} "
                          f"({v['max_share']:.3f} / {v['p99_share']:.3f} / {v['p50_share']:.3f})")
            rows.append(row)
        print(table(["scene"] + resources, rows))
    systems = list(loads[scenes[0]]["tick_system"])
    print("\nGraph thread CPU per tick by system, p50 / max ms:\n")
    rows = [{"scene": n, **{k: f"{v['p50_ms']:.2f} / {v['max_ms']:.2f}" for k, v in loads[n]["tick_system"].items()}}
            for n in scenes]
    print(table(["scene"] + systems, rows))
    phases = list(loads[scenes[0]]["tick_phase_wall"])
    print("\nGraph_Update phases per tick, p50 wall / p50 CPU ms:\n")
    rows = [{"scene": n, **{p: f"{loads[n]['tick_phase_wall'][p]['p50_ms']:.2f} / "
                              f"{loads[n]['tick_phase_cpu'][p]['p50_ms']:.2f}" for p in phases}} for n in scenes]
    print(table(["scene"] + phases, rows))
    threads = list(loads[scenes[0]]["tick_cpu"])
    print("\nCPU per tick by thread, p50 / max ms, and hook cost:\n")
    rows = [{"scene": n, **{k: f"{v['p50_ms']:.2f} / {v['max_ms']:.2f}" for k, v in loads[n]["tick_cpu"].items()},
             "hook_ms_p50": f"{loads[n]['hook_ms_per_tick']['p50_ms']:.3f}",
             "clock_reads_p50": loads[n]["hook_clock_reads_per_tick"]["p50"]} for n in scenes]
    print(table(["scene"] + threads + ["hook_ms_p50", "clock_reads_p50"], rows))
    print("\nTop actors by update + draw, ms per tick (update / draw):\n")
    ranks = [f"#{i + 1}" for i in range(5)]
    rows = []
    for n in scenes:
        top = [f"{a['actor'].removeprefix('ACTOR_')} {a['update_ms_per_tick']:.2f} / {a['draw_ms_per_tick']:.2f}"
               for a in loads[n]["top_actors"][:5]]
        rows.append({"scene": n, **dict(zip(ranks, top + [""] * (5 - len(top))))})
    print(table(["scene"] + ranks, rows))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("results", type=Path)
    args = p.parse_args()
    meta = json.loads((args.results / "meta.json").read_text())
    summary = tsv(args.results / "summary.tsv")
    wall = {r["run"]: r for r in tsv(args.results / "wall.tsv")}

    print(f"ROM sha256 `{meta['rom_sha256'][:16]}`, runner sha256 `{meta['runner_sha256'][:16]}`, "
          f"keyframe symbol {'present' if meta['keyframe_symbol'] else 'absent'}.\n")
    print("Presentation (fields per displayed image, complete images in the window):\n")
    print(table(["scene", "fields", "images", "img_n1", "img_n2", "img_n3", "img_n4", "img_n5", "img_n6plus",
                 "image_share_at_1", "image_fields_max"], summary))
    print("\nTick cadence (fields per logic tick):\n")
    print(table(["scene", "divisors", "ticks", "tick_n1", "tick_n2", "tick_n3", "tick_n4", "tick_n5", "tick_n6plus",
                 "lag_ticks", "skipped_ticks", "tick_fields_max", "settle_tick", "window_start_frame"], summary))
    print_load(args.results, [r["scene"] for r in summary])
    print("\nWall time (host, not deterministic):\n")
    rows = [{"run": n, **wall[n]} for n in [s["scene"] for s in summary] + ["total"]]
    print(table(["run", "wall_s", "jobs", "load_before", "load_after"], rows))


if __name__ == "__main__":
    main()

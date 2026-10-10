#!/usr/bin/env python3
"""
Summarize bench results, or compare two result sets.

    ~/.venvs/n64perf/bin/python tools/bench/analyze.py bench-results/<dir>
    ~/.venvs/n64perf/bin/python tools/bench/analyze.py bench-results/<A> bench-results/<B>

Single: writes summary.md and per-scene plots into the result dir and prints the summary.
Compare: B - A per scene, using paired per-frame differences (frames align because game logic is
deterministic under the fixed seed) with a moving-block bootstrap 95% CI.
See tools/bench/README.md.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd

# Display list buffer capacities in bytes (GfxPool, include/gfx.h)
CAPACITY = {"opa_bytes": 0x3380 * 8, "xlu_bytes": 0x800 * 8, "ovl_bytes": 0x400 * 8}
# The DPC counters count RCP clocks (n64brew, "Reality Display Processor/Interface", DPC_CLOCK)
DPC_HZ = 62_500_000

# Metrics compared between result sets: column -> (label, unit scale function name)
COMPARE_METRICS = [
    ("game_ms", "game logic + DL build (ms)"),
    ("cpu_ms", "Graph_Update incl. waits (ms)"),
    ("rsp_gfx_ms", "RSP gfx (ms)"),
    ("rsp_aud_ms", "RSP audio (ms)"),
    ("rdp_ms", "gfx task -> DP full sync (ms)"),
    ("vi", "VIs per frame"),
    ("opa_bytes", "POLY_OPA DL bytes"),
    ("ic_miss", "I-cache misses"),
    ("dc_miss", "D-cache misses"),
    ("rdram_total", "RDRAM bytes"),
]


def load(result_dir: Path) -> tuple[pd.DataFrame, dict]:
    meta = json.loads((result_dir / "meta.json").read_text())
    frames = []
    for csv in sorted(result_dir.glob("*.csv")):
        frames.append(pd.read_csv(csv))
    if not frames:
        sys.exit(f"error: no CSVs in {result_dir}")
    df = pd.concat(frames, ignore_index=True)

    # Clock info from any run's START line
    start = next(r["start"] for r in meta["results"] if r["start"])
    counter_hz = int(re.search(r"counter_hz=(\d+)", start).group(1))
    retrace_ticks = int(re.search(r"retrace_ticks=(\d+)", start).group(1))
    meta["counter_hz"] = counter_hz
    meta["vi_ms"] = retrace_ticks / counter_hz * 1000

    ms = 1000 / counter_hz
    for col in ["pre", "game", "cpu", "period", "rsp_gfx", "rsp_aud", "rdp", "bench"]:
        df[f"{col}_ms"] = df[f"{col}_ticks"] * ms
    # emux 0x0200 counts RSP busy time and 0x0201 RSP halted time; together they are the RSP's elapsed time.
    rsp_elapsed = df.rsp_cycles + df.rsp_halted
    df["rsp_busy_pct"] = np.where(rsp_elapsed > 0, 100 * df.rsp_cycles / rsp_elapsed.clip(lower=1), np.nan)
    if "dpc_pipebusy" in df:
        df["rdp_busy_ms"] = df.dpc_pipebusy / DPC_HZ * 1000
    df["lag"] = df.vi > df.divisor
    return df, meta


def pct(s: pd.Series, q: float) -> float:
    return float(np.percentile(s, q))


def summarize_scene(d: pd.DataFrame, vi_ms: float) -> dict:
    per_run_game_med = d.groupby("run").game_ms.median()
    vi_hz = 1000 / vi_ms
    return {
        "runs": d.run.nunique(),
        "frames/run": len(d) // d.run.nunique(),
        "divisor": int(d.divisor.mode()[0]),
        "fps": vi_hz / d.vi.mean(),
        "lag frames %": 100 * d.lag.mean(),
        "game ms med": d.game_ms.median(),
        "game ms p95": pct(d.game_ms, 95),
        "game ms p99": pct(d.game_ms, 99),
        "game ms max": d.game_ms.max(),
        "game ms run-to-run CV %": 100 * per_run_game_med.std(ddof=1) / per_run_game_med.mean()
        if len(per_run_game_med) > 1
        else float("nan"),
        "60fps headroom ms (VI - game p95)": vi_ms - pct(d.game_ms, 95),
        "pre (input+audio cmds) ms med": d.pre_ms.median(),
        "cpu ms med": d.cpu_ms.median(),
        "rsp gfx ms med": d.rsp_gfx_ms.median(),
        "rsp gfx ms p95": pct(d.rsp_gfx_ms, 95),
        "rsp aud ms med": d.rsp_aud_ms.median(),
        "rdp ms med": d.rdp_ms.median(),
        "rdp ms p95": pct(d.rdp_ms, 95),
        "rdp busy ms med (DPC_PIPEBUSY)": d.rdp_busy_ms.median() if "rdp_busy_ms" in d else float("nan"),
        "rsp busy % med": d.rsp_busy_pct.median(),
        "actors med": d.actors.median(),
        "actors max": int(d.actors.max()),
        "opa KiB max": d.opa_bytes.max() / 1024,
        "opa % cap max": 100 * d.opa_bytes.max() / CAPACITY["opa_bytes"],
        "xlu % cap max": 100 * d.xlu_bytes.max() / CAPACITY["xlu_bytes"],
        "ovl % cap max": 100 * d.ovl_bytes.max() / CAPACITY["ovl_bytes"],
        "zelda arena free KiB min": d.zelda_free.min() / 1024,
        "zelda arena max block KiB min": d.zelda_max_free.min() / 1024,
        "icache miss/frame med": d.ic_miss.median(),
        "dcache miss/frame med": d.dc_miss.median(),
        "rdram MiB/frame med": d.rdram_total.median() / 2**20,
        "rdram cpu MiB/frame med": d.rdram_cpu.median() / 2**20,
        "rdram rdp MiB/frame med": d.rdram_dp.median() / 2**20,
        "bench overhead ms med": d.bench_ms.median(),
    }


def fmt(v) -> str:
    if isinstance(v, (int, np.integer)):
        return str(v)
    if isinstance(v, float) and np.isnan(v):
        return "n/a"
    if abs(v) >= 100:
        return f"{v:.0f}"
    if abs(v) >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def plot_scene(d: pd.DataFrame, scene: str, out: Path, vi_ms: float) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    m = d.groupby("frame")[["game_ms", "rsp_gfx_ms", "rsp_aud_ms", "rdp_ms"]].median()
    fig, ax = plt.subplots(figsize=(12, 4))
    for col in m.columns:
        ax.plot(m.index, m[col], label=col, lw=0.8)
    ax.axhline(vi_ms, color="red", ls="--", lw=0.8, label="1 VI (60fps budget)")
    ax.axhline(vi_ms * 3, color="gray", ls=":", lw=0.8, label="3 VI (20fps)")
    ax.set_xlabel("frame")
    ax.set_ylabel("ms (median over runs)")
    ax.set_title(scene)
    ax.legend(loc="upper right", fontsize=7)
    fig.tight_layout()
    fig.savefig(out, dpi=100)
    plt.close(fig)


def single(result_dir: Path) -> None:
    df, meta = load(result_dir)
    scenes = list(dict.fromkeys(df.scene))
    rows = {s: summarize_scene(df[df.scene == s], meta["vi_ms"]) for s in scenes}
    table = pd.DataFrame(rows)

    lines = [
        f"# Bench summary: {meta['label']} ({meta['timestamp']})",
        "",
        f"- ROM md5 `{meta['rom_md5']}`, git `{meta['git_rev'][:10]}`{' (dirty)' if meta['git_dirty'] else ''}",
        f"- ares fork `{meta['ares_fork'] or meta['runner']}`, warmup {meta['warmup']}, frames {meta['frames']}, "
        f"seed {meta['seed']:#x}, runs {meta['runs']}, jobs {meta['jobs']}",
        f"- 1 VI = {meta['vi_ms']:.3f} ms",
        "",
        "| metric | " + " | ".join(scenes) + " |",
        "|---|" + "---|" * len(scenes),
    ]
    for metric, row in table.iterrows():
        lines.append(f"| {metric} | " + " | ".join(fmt(row[s]) for s in scenes) + " |")
    text = "\n".join(lines) + "\n"
    (result_dir / "summary.md").write_text(text)
    print(text)

    for s in scenes:
        plot_scene(df[df.scene == s], s, result_dir / f"{s}.png", meta["vi_ms"])


def block_bootstrap_ci(diff: np.ndarray, block: int = 30, n: int = 2000, seed: int = 0) -> tuple[float, float]:
    rng = np.random.default_rng(seed)
    nblocks = int(np.ceil(len(diff) / block))
    starts_max = len(diff) - block
    meds = np.empty(n)
    for i in range(n):
        starts = rng.integers(0, starts_max + 1, nblocks)
        sample = np.concatenate([diff[s : s + block] for s in starts])[: len(diff)]
        meds[i] = np.median(sample)
    return float(np.percentile(meds, 2.5)), float(np.percentile(meds, 97.5))


def compare(dir_a: Path, dir_b: Path) -> None:
    from scipy.stats import wilcoxon

    a, meta_a = load(dir_a)
    b, meta_b = load(dir_b)
    print(f"# Compare: B={meta_b['label']} minus A={meta_a['label']}\n")
    print("Paired per-frame differences of per-frame medians over runs; 95% CI by moving-block bootstrap (block 30).\n")
    print("| scene | metric | A med | B med | Δ med | 95% CI | Δ % | Wilcoxon p | A p95 | B p95 |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for scene in [s for s in dict.fromkeys(a.scene) if s in set(b.scene)]:
        sa = a[a.scene == scene].groupby("frame").median(numeric_only=True)
        sb = b[b.scene == scene].groupby("frame").median(numeric_only=True)
        idx = sa.index.intersection(sb.index)
        for col, label in COMPARE_METRICS:
            xa, xb = sa.loc[idx, col].to_numpy(float), sb.loc[idx, col].to_numpy(float)
            diff = xb - xa
            lo, hi = block_bootstrap_ci(diff)
            p = wilcoxon(diff).pvalue if np.any(diff != 0) else 1.0
            ma, mb = np.median(xa), np.median(xb)
            rel = 100 * (mb - ma) / ma if ma else float("nan")
            print(
                f"| {scene} | {label} | {fmt(ma)} | {fmt(mb)} | {fmt(np.median(diff))} | [{fmt(lo)}, {fmt(hi)}] "
                f"| {fmt(rel)} | {p:.2g} | {fmt(np.percentile(xa, 95))} | {fmt(np.percentile(xb, 95))} |"
            )
        lag_a, lag_b = 100 * a[a.scene == scene].lag.mean(), 100 * b[b.scene == scene].lag.mean()
        print(f"| {scene} | lag frames % | {fmt(lag_a)} | {fmt(lag_b)} | {fmt(lag_b - lag_a)} | | | | | |")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("dirs", nargs="+", type=Path)
    args = p.parse_args()
    if len(args.dirs) == 1:
        single(args.dirs[0])
    elif len(args.dirs) == 2:
        compare(*args.dirs)
    else:
        sys.exit("error: give one result dir (summary) or two (compare A B)")


if __name__ == "__main__":
    main()

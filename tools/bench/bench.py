#!/usr/bin/env python3
"""
Run the BENCH=1 ROM on the ares timing-model fork and collect per-frame records.

    tools/bench/bench.py --scene all --runs 5 --label baseline

Each run patches the ROM's BenchConfig (found by its magic), runs it in the fork's headless runner
n64-run, and parses the IS-Viewer output into a CSV. Results go to bench-results/<timestamp>-<label>/.
See tools/bench/README.md.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import platform
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ROM = ROOT / "build/n64-us-bench/mm-n64-us.z64"
DEFAULT_FORK = Path.home() / "repos/ares"

CONFIG_MAGIC = 0x424E4348  # "BNCH", see include/bench.h
CONFIG_FORMAT = ">5I"  # magic, scene, warmup, frames, seed
MAX_FRAMES = 600  # BENCH_MAX_FRAMES

# Must match sBenchScenes in src/code/bench.c
SCENES = [
    "south_clock_town",
    "termina_field",
    "mountain_village_winter",
    "great_bay_coast",
    "file_select_empty",
    "file_select_named",
]


def patch_rom(rom: bytes, scene: int, warmup: int, frames: int, seed: int) -> bytes:
    magic = struct.pack(">I", CONFIG_MAGIC)
    offset = rom.find(magic)
    if offset < 0 or rom.find(magic, offset + 1) >= 0:
        sys.exit("error: BenchConfig magic not found exactly once; is this a BENCH=1 ROM?")
    # The header checksum only covers the first 0x101000 bytes
    assert offset >= 0x101000, "BenchConfig inside checksummed region"
    config = struct.pack(CONFIG_FORMAT, CONFIG_MAGIC, scene, warmup, frames, seed)
    return rom[:offset] + config + rom[offset + len(config) :]


def parse_output(text: str) -> tuple[str, list[str], list[list[int]], bool]:
    start = ""
    columns: list[str] = []
    rows: list[list[int]] = []
    ended = False
    for line in text.splitlines():
        if line.startswith("BENCH START"):
            start = line
        elif line.startswith("BENCH COLUMNS"):
            columns = line.split()[2:]
        elif line.startswith("BENCH F "):
            rows.append([int(x) for x in line.split()[2:]])
        elif line.startswith("BENCH END"):
            ended = True
    return start, columns, rows, ended


def run_one(rom_bytes: bytes, scene: int, run: int, args, out_dir: Path) -> dict:
    name = f"{SCENES[scene]}.run{run}"
    patched = patch_rom(rom_bytes, scene, args.warmup, args.frames, args.seed)

    with tempfile.TemporaryDirectory(prefix="bench-") as tmp:
        rom_path = Path(tmp) / "bench.z64"
        rom_path.write_bytes(patched)

        cmd = [str(args.runner), str(rom_path), "--wall-seconds", str(args.timeout)]
        t0 = time.monotonic()
        proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        elapsed = time.monotonic() - t0

    output = proc.stdout
    stop = next((l for l in proc.stderr.splitlines() if l.startswith("n64-run: stop=")), "")
    timed_out = "stop=wall-time-limit" in stop

    start, columns, rows, ended = parse_output(output)
    (out_dir / f"{name}.log").write_text("\n".join(l for l in output.splitlines() if l.startswith("BENCH")) + "\n")

    ok = ended and len(rows) == args.frames and "stop=emux-exit" in stop
    if columns and rows:
        with (out_dir / f"{name}.csv").open("w") as f:
            f.write("scene,run," + ",".join(columns) + "\n")
            for r in rows:
                f.write(f"{SCENES[scene]},{run}," + ",".join(map(str, r)) + "\n")

    return {
        "name": name,
        "scene": SCENES[scene],
        "run": run,
        "ok": ok,
        "timed_out": timed_out,
        "frames": len(rows),
        "host_seconds": round(elapsed, 2),
        "start": start,
        "runner_stop": stop,
    }


def git(*a: str, cwd: Path = ROOT) -> str:
    try:
        return subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True).stdout.strip()
    except OSError:
        return ""


def build_fork_runner(fork: Path) -> Path:
    """Builds n64-run with the fork's own build script (a no-op when up to date) and returns its path."""
    script = fork / "tools/n64-timing/build.sh"
    if not script.is_file():
        sys.exit(f"error: {script} not found; set ARES_FORK to the ares timing-model fork checkout, or pass --runner")
    proc = subprocess.run(["bash", str(script)], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"error: {script} failed:\n{proc.stdout}{proc.stderr}")
    return Path(proc.stdout.strip().splitlines()[-1])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    p.add_argument("--scene", default="all", help="scene index, name, comma list, or 'all'")
    p.add_argument("--runs", type=int, default=5)
    p.add_argument("--warmup", type=int, default=120)
    p.add_argument("--frames", type=int, default=MAX_FRAMES)
    p.add_argument("--seed", type=lambda s: int(s, 0), default=0x12345678)
    p.add_argument("--jobs", type=int, default=1, help="parallel runner instances")
    p.add_argument("--timeout", type=int, default=600, help="host seconds per run (n64-run --wall-seconds)")
    p.add_argument("--runner", type=Path, default=os.environ.get("N64_RUN"),
                   help="n64-run binary (default $N64_RUN, else build it from $ARES_FORK)")
    p.add_argument("--fork", type=Path, default=Path(os.environ.get("ARES_FORK", DEFAULT_FORK)),
                   help=f"ares timing-model fork checkout (default $ARES_FORK, else {DEFAULT_FORK})")
    p.add_argument("--label", default="run")
    p.add_argument("--out", type=Path, default=None)
    args = p.parse_args()

    if not 1 <= args.frames <= MAX_FRAMES:
        sys.exit(f"error: --frames must be 1..{MAX_FRAMES}")
    built = args.runner is None
    if built:
        args.runner = build_fork_runner(args.fork)
    if not os.access(args.runner, os.X_OK):
        sys.exit(f"error: runner {args.runner} is not executable")

    if args.scene == "all":
        scenes = list(range(len(SCENES)))
    else:
        scenes = [int(s) if s.isdigit() else SCENES.index(s) for s in args.scene.split(",")]

    rom_bytes = args.rom.read_bytes()
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.out or ROOT / "bench-results" / f"{stamp}-{args.label}"
    out_dir.mkdir(parents=True, exist_ok=True)

    meta = {
        "label": args.label,
        "timestamp": stamp,
        "rom": str(args.rom),
        "rom_md5": hashlib.md5(rom_bytes).hexdigest(),
        "git_rev": git("rev-parse", "HEAD"),
        "git_dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
        "runner": str(args.runner),
        "runner_sha256": hashlib.sha256(args.runner.read_bytes()).hexdigest(),
        # Only known when bench.py built the runner itself; an explicit --runner may come from any tree.
        "ares_fork": git("describe", "--always", "--tags", "--dirty", cwd=args.fork) if built else None,
        "warmup": args.warmup,
        "frames": args.frames,
        "seed": args.seed,
        "runs": args.runs,
        "jobs": args.jobs,
        "host": platform.node(),
        "results": [],
    }

    jobs = [(s, r) for s in scenes for r in range(args.runs)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futures = {ex.submit(run_one, rom_bytes, s, r, args, out_dir): (s, r) for s, r in jobs}
        for fut in concurrent.futures.as_completed(futures):
            res = fut.result()
            meta["results"].append(res)
            status = "ok" if res["ok"] else ("TIMEOUT" if res["timed_out"] else "FAILED")
            print(f"{res['name']:40s} {status:8s} frames={res['frames']:4d} {res['host_seconds']:6.1f}s", flush=True)

    meta["results"].sort(key=lambda r: r["name"])
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(out_dir)
    if not all(r["ok"] for r in meta["results"]):
        sys.exit(1)


if __name__ == "__main__":
    main()

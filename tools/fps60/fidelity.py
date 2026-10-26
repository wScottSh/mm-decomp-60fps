#!/usr/bin/env python3
"""F1: compare the keyframe sequences of two run.py result directories, scene by scene.

A scene passes when, over the logic ticks both windows cover, every tick has a keyframe in both and
the keyframes' fb_hash values are equal. On a failure the scene is run again from both ROMs with a
frame dump at the first differing tick's final field, and the ares framediff.py report is written.
See tools/fps60/README.md.
"""

import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import corpus  # noqa: E402
import run  # noqa: E402

FRAMEDIFF = Path.home() / "repos/ares-wt/mm60-runner/tools/n64-timing/framediff.py"


def keyframes(path):
    lines = Path(path).read_text().splitlines()
    header = lines[0].split("\t")
    rows = [dict(zip(header, l.split("\t"))) for l in lines[1:]]
    return {int(r["image_tick"]): r for r in rows}


def compare(base, cand):
    """Returns (ticks compared, differing ticks in order). A tick shown on one side only differs."""
    if not base or not cand:
        return 0, []
    lo, hi = max(min(base), min(cand)), min(max(base), max(cand))
    ticks = sorted(t for t in set(base) | set(cand) if lo <= t <= hi)
    differing = [t for t in ticks if t not in base or t not in cand or base[t]["fb_hash"] != cand[t]["fb_hash"]]
    return len(ticks), differing


def dump(result, scene_name, frame, out):
    """Runs the scene again from the result's ROM and dumps stats row `frame`."""
    meta = json.loads((result / "meta.json").read_text())
    if run.sha256(meta["rom"]) != meta["rom_sha256"]:
        raise SystemExit(f"{meta['rom']} changed since {result} was measured; cannot dump its frames")
    rom, symbols = run.load_rom(meta["rom"], meta["map"])
    scene = next(s for s in corpus.SCENES if s["name"] == scene_name)
    ppm = out / f"{scene_name}.ppm"
    run.run_scene(rom, symbols, Path(meta["runner"]), scene, out, dump_frames=[(frame, ppm)])
    return ppm


def report_failure(base, cand, scene, tick, kb, kc, out):
    out.mkdir(parents=True, exist_ok=True)
    if tick not in kb or tick not in kc:
        side = "base" if tick not in kb else "candidate"
        (out / f"{scene}.txt").write_text(f"tick {tick} has no keyframe in the {side} window\n")
        return f"no keyframe for tick {tick} in the {side}"
    with ThreadPoolExecutor(max_workers=2) as pool:
        a = pool.submit(dump, base, scene, int(kb[tick]["final_frame"]), out / "base")
        b = pool.submit(dump, cand, scene, int(kc[tick]["final_frame"]), out / "cand")
        ppm_a, ppm_b = a.result(), b.result()
    proc = subprocess.run([sys.executable, str(FRAMEDIFF), str(ppm_a), str(ppm_b), "--out",
                           str(out / f"{scene}-diff.png")], capture_output=True, text=True)
    (out / f"{scene}-framediff.txt").write_text(proc.stdout + proc.stderr)
    return f"frames {ppm_a} {ppm_b}, framediff {out / f'{scene}-framediff.txt'}"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("base", type=Path)
    p.add_argument("cand", type=Path)
    p.add_argument("--out", type=Path, help="frame dumps on failure (default CAND/fidelity)")
    args = p.parse_args()
    out = args.out or args.cand / "fidelity"

    scenes = json.loads((args.base / "meta.json").read_text())["scenes"]
    cand_scenes = json.loads((args.cand / "meta.json").read_text())["scenes"]
    failed = False
    for scene in scenes:
        if scene not in cand_scenes:
            print(f"{scene}: FAIL, not in the candidate run")
            failed = True
            continue
        kb, kc = keyframes(args.base / scene / "keyframes.tsv"), keyframes(args.cand / scene / "keyframes.tsv")
        n, differing = compare(kb, kc)
        if n == 0:
            print(f"{scene}: FAIL, no ticks in common")
            failed = True
        elif differing:
            where = report_failure(args.base, args.cand, scene, differing[0], kb, kc, out / scene)
            print(f"{scene}: FAIL, first differing tick {differing[0]}, {len(differing)} of {n} ticks differ; {where}")
            failed = True
        else:
            print(f"{scene}: PASS, {n} ticks identical (ticks {min(kb.keys() & kc.keys())} to {max(kb.keys() & kc.keys())})")
    print("F1 FAIL" if failed else "F1 PASS")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

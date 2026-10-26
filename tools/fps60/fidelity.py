#!/usr/bin/env python3
"""F1: compare the keyframe sequences of two run.py result directories, scene by scene.

A scene passes when every tick that has a keyframe in both windows has equal fb_hash values, and with the
noise pin on equal noise_hash values (the noise commands as the game wrote them), and the candidate has a
keyframe for every reference tick except the first and last EDGE_TICKS of the reference window. Ticks only
the candidate shows are reported, not failed: the reference skipped them. Both runs must have the same noise
pin and load pin settings. On a failure the scene is run again from both ROMs with a frame dump at the first
differing tick's final field, and the ares framediff.py report is written. See tools/fps60/README.md.
"""

import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run  # noqa: E402

FRAMEDIFF = Path.home() / "repos/ares-wt/mm60-runner/tools/n64-timing/framediff.py"

# A window's first and last ticks can fall on either side of its edge in a run with other timing.
EDGE_TICKS = 3


def read_tsv(path):
    lines = Path(path).read_text().splitlines()
    header = lines[0].split("\t")
    return [dict(zip(header, l.split("\t"))) for l in lines[1:]]


def keyframes(path):
    return {int(r["image_tick"]): r for r in read_tsv(path)}


def compare(base, cand, keys, tick=lambda k: k):
    """Compares two {key: row} maps. `tick` maps a key to its logic tick. Returns (keys compared,
    {key: the fields that differ} in key order, reference keys the candidate lacks inside the window, keys
    only the candidate has)."""
    ticks = sorted({tick(k) for k in base})
    inner = set(ticks[EDGE_TICKS:-EDGE_TICKS])
    common = sorted(base.keys() & cand.keys())
    differing = {}
    for k in common:
        fields = [f for f in keys if base[k][f] != cand[k][f]]
        if fields:
            differing[k] = fields
    missing = sorted(k for k in base.keys() - cand.keys() if tick(k) in inner)
    extra = sorted(cand.keys() - base.keys())
    return common, differing, missing, extra


def settings(meta):
    """The run settings two fidelity runs must share. `check` walks twice but pins as `on` does. Result
    directories from before the noise pin have no noise_pin key, and from before the load pin no load_pin."""
    mode = meta.get("noise_pin_mode", "on" if meta.get("noise_pin") else "off")
    return {"noise pin": "on" if mode == "check" else mode, "load pin": meta.get("load_pin", False)}


def dump(result, scene, frame, out):
    """Runs the scene again from the result's ROM and dumps stats row `frame`."""
    ppm = out / f"{scene}.ppm"
    run.rerun(result, scene, [(frame, ppm)], out)
    return ppm


def dump_pair(base, cand, scene, base_frame, cand_frame, out):
    """Dumps both frames and writes the framediff report. Returns where they are."""
    out.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        a = pool.submit(dump, base, scene, base_frame, out / "base")
        b = pool.submit(dump, cand, scene, cand_frame, out / "cand")
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

    base_meta = json.loads((args.base / "meta.json").read_text())
    cand_meta = json.loads((args.cand / "meta.json").read_text())
    if settings(base_meta) != settings(cand_meta):
        print(f"F1 FAIL: base runs with {settings(base_meta)}, candidate with {settings(cand_meta)}; compare runs "
              "with the same settings")
        sys.exit(1)
    pin = settings(base_meta)["noise pin"]
    if pin == "off":
        print("noise pin off in both runs: RDP noise follows RDP timing, so any timing change fails F1")
    elif pin == "noise-only":
        print("noise-only runs: stale span reads follow RDP timing; stale_reads.py measures them")
    keys = ["fb_hash"] if pin == "off" else ["fb_hash", "noise_hash"]
    failed = False
    for scene in base_meta["scenes"]:
        if scene not in cand_meta["scenes"]:
            print(f"{scene}: FAIL, not in the candidate run")
            failed = True
            continue
        kb, kc = keyframes(args.base / scene / "keyframes.tsv"), keyframes(args.cand / scene / "keyframes.tsv")
        common, differing, missing, extra = compare(kb, kc, keys)
        notes = f"; {len(extra)} ticks only in the candidate: {extra}" if extra else ""
        if missing:
            print(f"{scene}: FAIL, the candidate has no keyframe for {len(missing)} reference ticks inside the "
                  f"window: {missing}{notes}")
            failed = True
        if not common:
            print(f"{scene}: FAIL, no ticks in common")
            failed = True
        elif differing:
            first = next(iter(differing))
            where = dump_pair(args.base, args.cand, scene, int(kb[first]["final_frame"]),
                              int(kc[first]["final_frame"]), out / scene)
            by_key = ", ".join(f"{k} in {sum(1 for d in differing.values() if k in d)}" for k in keys)
            print(f"{scene}: FAIL, first differing tick {first} ({' and '.join(differing[first])}), "
                  f"{len(differing)} of {len(common)} ticks differ ({by_key}); {where}{notes}")
            failed = True
        elif not missing:
            print(f"{scene}: PASS, {len(common)} ticks identical (ticks {common[0]} to {common[-1]}){notes}")
    print("F1 FAIL" if failed else "F1 PASS")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

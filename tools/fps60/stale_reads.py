#!/usr/bin/env python3
"""Stale-read exposure (preferences line 26): differing pixels of CAND and of CONTROL against BASE.

  stale_reads.py BASE CAND CONTROL [--out DIR]

All three are run.py result directories measured with --noise-pin noise-only: noise is neutralized but the
pipeline mode stays as the game emitted it, so where primitives overlap the RDP can read stale span memory,
and which pixels it does depends on timing. CONTROL is a build with CAND's code layout that behaves like BASE
(README, Stale reads). Per scene, every image both runs show, keyed by (image_tick, image index), is
compared; each image whose fb_hash differs is dumped from both ROMs and its differing pixels counted. The
scene fails when CAND's count exceeds CONTROL's. Images whose noise_hash differs are reported too: the game
emitted different noise or pipeline-mode commands there, which F1 rejects on its own.

Exit 0 when no scene's CAND count exceeds CONTROL's, else 1. Counts are pixels of the runner's 640x480 frame
dumps, where the VI shows each 320x240 framebuffer pixel as a 2x2 block.
"""

import argparse
import json
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fidelity  # noqa: E402
import run  # noqa: E402


def read_ppm(path):
    data = Path(path).read_bytes()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[at:end])
        at = end
    if fields[0] != b"P6" or fields[3] != b"255":
        raise SystemExit(f"{path}: not an 8-bit binary PPM")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[at + 1:at + 1 + 3 * w * h]


def differing_pixels(a, b):
    wa, ha, pa = read_ppm(a)
    wb, hb, pb = read_ppm(b)
    if (wa, ha) != (wb, hb):
        raise SystemExit(f"{a} is {wa}x{ha}, {b} is {wb}x{hb}")
    return sum(1 for i in range(0, len(pa), 3) if pa[i:i + 3] != pb[i:i + 3])


def images(result, scene):
    return {(int(r["image_tick"]), int(r["image"])): r
            for r in fidelity.read_tsv(Path(result) / scene / "images.tsv")}


def dump_all(result, scene, frames, out):
    """Reruns `scene` of `result` once, dumping every stats row in `frames`. Returns {row: ppm}."""
    paths = {f: out / f"{f}.ppm" for f in sorted(frames)}
    if paths:
        run.rerun(result, scene, list(paths.items()), out)
    return paths


def scene_exposure(base, others, scene, out):
    """{name: (images compared, images whose fb_hash differs, differing pixels, images whose noise_hash
    differs)} for each (name, result) in `others` against `base`."""
    ib = images(base, scene)
    pairs = {}
    for name, result in others:
        io = images(result, scene)
        common = sorted(ib.keys() & io.keys())
        pairs[name] = (result, io, common, [k for k in common if ib[k]["fb_hash"] != io[k]["fb_hash"]],
                       [k for k in common if ib[k]["noise_hash"] != io[k]["noise_hash"]])
    base_frames = {int(ib[k]["final_frame"]) for _, _, _, diff, _ in pairs.values() for k in diff}
    with ThreadPoolExecutor(max_workers=1 + len(pairs)) as pool:
        jobs = {"base": pool.submit(dump_all, base, scene, base_frames, out / "base")}
        for name, (result, io, _, diff, _) in pairs.items():
            jobs[name] = pool.submit(dump_all, result, scene, {int(io[k]["final_frame"]) for k in diff}, out / name)
        dumps = {n: j.result() for n, j in jobs.items()}
    exposure = {}
    for name, (result, io, common, diff, noise) in pairs.items():
        pixels = sum(differing_pixels(dumps["base"][int(ib[k]["final_frame"])],
                                      dumps[name][int(io[k]["final_frame"])]) for k in diff)
        exposure[name] = (len(common), len(diff), pixels, len(noise))
    return exposure


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("base", type=Path)
    p.add_argument("cand", type=Path)
    p.add_argument("control", type=Path)
    p.add_argument("--out", type=Path, help="frame dumps (default CAND/stale_reads)")
    args = p.parse_args()
    out = args.out or args.cand / "stale_reads"
    metas = {n: json.loads((r / "meta.json").read_text())
             for n, r in (("base", args.base), ("cand", args.cand), ("control", args.control))}
    for name, meta in metas.items():
        if fidelity.settings(meta)["noise pin"] != "noise-only":
            raise SystemExit(f"{name} ran with {fidelity.settings(meta)}; stale_reads.py needs --noise-pin noise-only")
    if len({fidelity.settings(m)["load pin"] for m in metas.values()}) != 1:
        raise SystemExit("the three runs must share the load pin setting")
    failed = False
    for scene in metas["base"]["scenes"]:
        if any(scene not in metas[n]["scenes"] for n in ("cand", "control")):
            print(f"{scene}: FAIL, not in every run")
            failed = True
            continue
        e = scene_exposure(args.base, [("cand", args.cand), ("control", args.control)], scene, out / scene)
        verdict = "FAIL" if e["cand"][2] > e["control"][2] else "PASS"
        failed |= verdict == "FAIL"
        print(f"{scene}: {verdict}, " + "; ".join(
            f"{n} {px} px in {d} of {c} images" + (f", noise_hash differs in {nz}" if nz else "")
            for n, (c, d, px, nz) in e.items()))
    print("stale reads: FAIL, the candidate exposes more than the control" if failed else "stale reads: PASS")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

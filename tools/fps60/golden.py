#!/usr/bin/env python3
"""F2: the golden set of every presented image, keyed by (image_tick, image index), scene by scene.

  golden.py record RESULTS [--golden PATH]   write the golden set from an accepted run (P2-ref's)
  golden.py check RESULTS [--golden PATH]    compare a run against it

A key passes when its fb_hash and noise_hash equal the golden set's. A scene fails when a key differs, or
when the run lacks a golden key whose tick is inside the golden window (all but its first and last
fidelity.EDGE_TICKS ticks). Keys only the run has are reported. On a failure the first differing key is
dumped from both ROMs (the golden ROM only while it still exists with its recorded sha256).

F2 applies only to builds that present interpolated images (gBenchCfbKeyframe and gBenchCfbImage in the
map). A retail-architecture build presents keyframes only, which F1 covers: both commands exit 2 for it.
Runs must use the noise pin (on or check) and the load pin. See tools/fps60/README.md.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fidelity  # noqa: E402

GOLDEN = Path(__file__).resolve().parent / "golden.tsv"
COLUMNS = ["scene", "image_tick", "image", "keyframe", "final_frame", "fb_hash", "noise_hash"]
KEYS = ["fb_hash", "noise_hash"]
NOT_APPLICABLE = 2


def applicable(meta, what):
    """Exits unless `meta` is an F2-comparable run."""
    if not meta.get("keyframe_symbol"):
        print(f"F2 does not apply to {what}: a retail-architecture build presents only keyframes; F1 covers it")
        sys.exit(NOT_APPLICABLE)
    s = fidelity.settings(meta)
    if s != {"noise pin": "on", "load pin": True}:
        print(f"F2 needs the noise pin on and the load pin on; {what} ran with {s}")
        sys.exit(1)


def images(result, scene):
    return {(int(r["image_tick"]), int(r["image"])): r
            for r in fidelity.read_tsv(Path(result) / scene / "images.tsv")}


def record(result, golden):
    meta = json.loads((result / "meta.json").read_text())
    applicable(meta, result)
    rows = []
    for scene in meta["scenes"]:
        imgs = images(result, scene)
        if not any(r["keyframe"] == "0" for r in imgs.values()):
            print(f"{scene}: no interpolated image in the window; a golden set needs a run that presents them")
            sys.exit(1)
        rows += [{"scene": scene, **{c: imgs[k][c] for c in COLUMNS[1:]}} for k in sorted(imgs)]
        print(f"{scene}: {len(imgs)} images, {sum(1 for r in imgs.values() if r['keyframe'] == '1')} keyframes")
    source = {k: meta[k] for k in ("rom", "rom_sha256", "map", "runner", "runner_sha256")}
    golden.write_text("# " + json.dumps({"source": str(result.resolve()), **source}) + "\n"
                      + "\t".join(COLUMNS) + "\n" + "".join("\t".join(r[c] for c in COLUMNS) + "\n" for r in rows))
    print(f"golden set: {len(rows)} images over {len(meta['scenes'])} scenes in {golden}")


def read_golden(golden):
    first, *rest = golden.read_text().splitlines()
    source = json.loads(first[2:])
    header = rest[0].split("\t")
    by_scene = {}
    for line in rest[1:]:
        r = dict(zip(header, line.split("\t")))
        by_scene.setdefault(r["scene"], {})[(int(r["image_tick"]), int(r["image"]))] = r
    return source, by_scene


def check(result, golden):
    meta = json.loads((result / "meta.json").read_text())
    applicable(meta, result)
    source, by_scene = read_golden(golden)
    failed = False
    for scene, gold in by_scene.items():
        if scene not in meta["scenes"]:
            print(f"{scene}: FAIL, not in the run")
            failed = True
            continue
        imgs = images(result, scene)
        common, differing, missing, extra = fidelity.compare(gold, imgs, KEYS, tick=lambda k: k[0])
        notes = f"; {len(extra)} keys only in the run: {extra}" if extra else ""
        if missing:
            print(f"{scene}: FAIL, the run lacks {len(missing)} golden images inside the window: {missing}{notes}")
            failed = True
        if not common:
            print(f"{scene}: FAIL, no images in common")
            failed = True
        elif differing:
            first = next(iter(differing))
            where = dump(source, result, scene, first, int(gold[first]["final_frame"]),
                         int(imgs[first]["final_frame"]), result / "golden" / scene)
            by_key = ", ".join(f"{k} in {sum(1 for d in differing.values() if k in d)}" for k in KEYS)
            print(f"{scene}: FAIL, first differing image (tick {first[0]}, image {first[1]}) "
                  f"({' and '.join(differing[first])}), {len(differing)} of {len(common)} images differ ({by_key}); "
                  f"{where}{notes}")
            failed = True
        elif not missing:
            print(f"{scene}: PASS, {len(common)} images identical (ticks {common[0][0]} to {common[-1][0]}){notes}")
    print("F2 FAIL" if failed else "F2 PASS")
    return failed


def dump(source, result, scene, key, gold_frame, frame, out):
    golden_run = Path(source["source"])
    if (golden_run / "meta.json").is_file() and Path(source["rom"]).is_file() and \
            fidelity.run.sha256(source["rom"]) == source["rom_sha256"]:
        return fidelity.dump_pair(golden_run, result, scene, gold_frame, frame, out)
    out.mkdir(parents=True, exist_ok=True)
    ppm = fidelity.dump(result, scene, frame, out / "cand")
    return f"frame {ppm}; the golden run {golden_run} or its ROM is gone, so only the run's frame is dumped"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=["record", "check"])
    p.add_argument("result", type=Path)
    p.add_argument("--golden", type=Path, default=GOLDEN)
    args = p.parse_args()
    if args.command == "record":
        record(args.result, args.golden)
    else:
        sys.exit(1 if check(args.result, args.golden) else 0)


if __name__ == "__main__":
    main()

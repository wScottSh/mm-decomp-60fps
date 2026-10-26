#!/usr/bin/env python3
"""Builds the F1 reference ROM (preferences line 30): this checkout's BENCH code over REF's game code.

  reference.py OUT [--ref 2ff723f02]

Every file under src/ and include/ that differs between REF and HEAD is classified by its two views:
the game view (the file as `#if BENCH` blocks compile with BENCH=0) and the BENCH view (only what those
blocks hold, both branches). A file whose game view is unchanged keeps HEAD's version: its change is
BENCH code. A file whose BENCH view is unchanged takes REF's version: its change is game code. A file that
changed in both views cannot be composed and stops the build. bench.c and bench.h are all BENCH code.

The composed tree is built twice in this checkout, which must be clean: BENCH=0 must match the retail ROM
(the composed game code is retail), then BENCH=1 gives OUT/mm-n64-us.z64 and its map. The checkout is
restored afterwards.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REF = "2ff723f02"
BENCH_ONLY = {"src/code/bench.c", "include/bench.h"}
IF = re.compile(r"\s*#\s*if(def|ndef)?\b")
IF_BENCH = re.compile(r"\s*#\s*if\s+BENCH\s*$")
ELSE = re.compile(r"\s*#\s*(else|elif)\b")
ENDIF = re.compile(r"\s*#\s*endif\b")


def views(text):
    """(game view, BENCH view) of a source file: the lines BENCH=0 compiles, and the `#if BENCH` blocks."""
    game, bench, stack = [], [], []  # per open #if: None, or the BENCH branch being read (1, then 0 after #else)
    for line in text.splitlines():
        bench_open = stack and stack[-1] is not None
        if IF_BENCH.match(line) or (bench_open and (ELSE.match(line) or ENDIF.match(line))):
            if IF_BENCH.match(line):
                stack.append(1)
            elif ENDIF.match(line):
                stack.pop()
            else:
                stack[-1] = 0
            bench.append(line)
            continue
        if IF.match(line):
            stack.append(None)
        elif ENDIF.match(line) and stack:
            stack.pop()
        if any(s is not None for s in stack):
            bench.append(line)
        if 1 not in stack:
            game.append(line)
    return [g for g in game if g.strip()], bench  # a hook's blank separator line is not a game change


def show(rev, path):
    r = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=ROOT, capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def compose(ref):
    """{path: 'head' | 'ref'} for every src/ and include/ file that differs between `ref` and HEAD."""
    changed = subprocess.run(["git", "diff", "--name-only", ref, "HEAD", "--", "src", "include"], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.split()
    plan, mixed = {}, []
    for path in changed:
        if path in BENCH_ONLY:
            plan[path] = "head"
            continue
        old, new = show(ref, path), show("HEAD", path)
        if old is None or new is None:
            mixed.append(f"{path} (added or removed since {ref})")
            continue
        (og, ob), (ng, nb) = views(old), views(new)
        if og == ng:
            plan[path] = "head"
        elif ob == nb:
            plan[path] = "ref"
        else:
            mixed.append(f"{path} (game code and BENCH code both changed)")
    if mixed:
        raise SystemExit("cannot compose the reference:\n  " + "\n  ".join(mixed))
    return plan


def make(*args):
    return subprocess.run(["make", "-j32", *args], cwd=ROOT, capture_output=True, text=True)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out", type=Path)
    p.add_argument("--ref", default=REF)
    p.add_argument("--dry-run", action="store_true", help="print the composition and stop")
    args = p.parse_args()
    plan = compose(args.ref)
    for path, side in sorted(plan.items()):
        print(f"{side:4} {path}")
    if args.dry_run:
        return
    if subprocess.run(["git", "diff", "--quiet", "HEAD"], cwd=ROOT).returncode:
        raise SystemExit("the checkout has uncommitted changes")
    game = [path for path, side in plan.items() if side == "ref"]
    try:
        if game:
            subprocess.run(["git", "checkout", args.ref, "--", *game], cwd=ROOT, check=True)
        retail = make()
        if "mm-n64-us.z64: OK" not in retail.stdout:
            sys.stdout.write(retail.stdout[-2000:] + retail.stderr[-2000:])
            raise SystemExit("the composed tree's BENCH=0 build does not match retail")
        print("BENCH=0 build: mm-n64-us.z64: OK (retail)")
        bench = make("BENCH=1")
        if bench.returncode:
            sys.stdout.write(bench.stdout[-2000:] + bench.stderr[-2000:])
            raise SystemExit("the composed tree's BENCH=1 build failed")
        args.out.mkdir(parents=True, exist_ok=True)
        for suffix in ("z64", "map"):
            shutil.copy(ROOT / "build/n64-us-bench" / f"mm-n64-us.{suffix}", args.out)
    finally:
        if game:
            subprocess.run(["git", "checkout", "HEAD", "--", *game], cwd=ROOT, check=True)
    sha = subprocess.run(["sha256sum", args.out / "mm-n64-us.z64"], capture_output=True, text=True).stdout[:16]
    print(f"reference {args.out / 'mm-n64-us.z64'} sha256 {sha}: game code of {args.ref}, BENCH code of "
          f"{subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True, text=True).stdout.strip()}")


if __name__ == "__main__":
    main()

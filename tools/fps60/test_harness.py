#!/usr/bin/env python3
"""Host tests for the F1 tick-set rule, the F2 golden set and the stale-read verdict, on synthetic result
directories. No runner: frame dumps are replaced by a stub. Run: tools/fps60/test_harness.py"""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fidelity  # noqa: E402
import golden  # noqa: E402
import stale_reads  # noqa: E402

TICKS = range(10, 30)


def write_run(root, ticks=TICKS, d=1, interp=False, changes=(), drop=(), noise_pin="on", load_pin=True):
    """A one-scene result directory. Each tick has `d` images (index 1..d, keyframe d). `changes` and `drop`
    hold (tick, index) keys whose fb_hash is altered or that are left out."""
    root.mkdir(parents=True, exist_ok=True)
    (root / "meta.json").write_text(json.dumps({
        "rom": "none", "rom_sha256": "", "map": "none", "runner": "none", "runner_sha256": "",
        "keyframe_symbol": interp, "noise_pin": noise_pin != "off", "noise_pin_mode": noise_pin,
        "load_pin": load_pin, "scenes": ["sct"]}))
    scene = root / "sct"
    scene.mkdir(exist_ok=True)
    cols = ["image_tick", "image", "keyframe", "final_frame", "fb_hash", "noise_hash"]
    rows = []
    for t in ticks:
        for i in range(1, d + 1):
            if (t, i) in drop:
                continue
            h = f"{t:04X}{i:04X}" + ("FFFF" if (t, i) in changes else "0000")
            rows.append([t, i, int(i == d), 100 + 3 * t + i, h, "ABCD0000"])
    body = "".join("\t".join(map(str, r)) + "\n" for r in rows)
    (scene / "images.tsv").write_text("\t".join(cols) + "\n" + body)
    kf = [r for r in rows if r[2] == 1]
    (scene / "keyframes.tsv").write_text("\t".join(cols) + "\n" + "".join("\t".join(map(str, r)) + "\n" for r in kf))
    return root


def call(main, argv):
    out = io.StringIO()
    old = sys.argv
    sys.argv = ["x"] + [str(a) for a in argv]
    try:
        with contextlib.redirect_stdout(out):
            main()
        code = 0
    except SystemExit as e:
        code = e.code if isinstance(e.code, int) else 1
    finally:
        sys.argv = old
    return code, out.getvalue()


class Harness(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        fidelity.dump_pair = lambda *a, **k: "dump stubbed"
        golden.dump = lambda *a, **k: "dump stubbed"

    def f1(self, **cand):
        base = write_run(self.tmp / "base")
        return call(fidelity.main, [base, write_run(self.tmp / "cand", **cand)])

    def test_f1_identical_passes(self):
        self.assertEqual(self.f1()[0], 0)

    def test_f1_missing_interior_tick_fails_and_names_it(self):
        code, out = self.f1(drop={(20, 1)})
        self.assertEqual(code, 1)
        self.assertIn("[20]", out)

    def test_f1_missing_edge_ticks_pass(self):
        code, out = self.f1(drop={(10, 1), (12, 1), (29, 1), (27, 1)})
        self.assertEqual(code, 0, out)

    def test_f1_missing_fourth_tick_fails(self):
        self.assertEqual(self.f1(drop={(13, 1)})[0], 1)

    def test_f1_extra_candidate_ticks_pass_and_are_reported(self):
        code, out = self.f1(ticks=range(5, 35))
        self.assertEqual(code, 0, out)
        self.assertIn("only in the candidate", out)

    def test_f1_changed_hash_fails_at_its_tick(self):
        code, out = self.f1(changes={(17, 1)})
        self.assertEqual(code, 1)
        self.assertIn("first differing tick 17", out)

    def test_f1_settings_must_match(self):
        self.assertEqual(self.f1(load_pin=False)[0], 1)
        self.assertEqual(call(fidelity.main, [write_run(self.tmp / "b2"),
                                              write_run(self.tmp / "c2", noise_pin="noise-only")])[0], 1)

    def golden(self, cand, **base):
        g = self.tmp / "golden.tsv"
        code, out = call(golden.main, ["record", write_run(self.tmp / "gbase", **base), "--golden", g])
        if code:
            return code, out
        return call(golden.main, ["check", write_run(self.tmp / "gcand", **cand), "--golden", g])

    def test_f2_identical_passes(self):
        code, out = self.golden(dict(d=3, interp=True), d=3, interp=True)
        self.assertEqual(code, 0, out)

    def test_f2_changed_interpolated_image_fails_at_its_key(self):
        code, out = self.golden(dict(d=3, interp=True, changes={(15, 2)}), d=3, interp=True)
        self.assertEqual(code, 1)
        self.assertIn("tick 15, image 2", out)

    def test_f2_missing_interpolated_image_fails(self):
        code, out = self.golden(dict(d=3, interp=True, drop={(15, 1)}), d=3, interp=True)
        self.assertEqual(code, 1)
        self.assertIn("(15, 1)", out)

    def test_f2_refuses_retail_architecture(self):
        self.assertEqual(self.golden(dict(d=3, interp=True))[0], golden.NOT_APPLICABLE)
        self.assertEqual(self.golden(dict(), d=3, interp=True)[0], golden.NOT_APPLICABLE)

    def test_f2_refuses_a_run_without_interpolated_images(self):
        self.assertEqual(self.golden(dict(interp=True), interp=True)[0], 1)

    def test_f2_needs_the_pins(self):
        self.assertEqual(self.golden(dict(d=3, interp=True, load_pin=False), d=3, interp=True)[0], 1)

    def stale(self, cand_px, control_px, mode="noise-only"):
        runs = [write_run(self.tmp / n, noise_pin=mode) for n in ("sb", "sc", "sk")]
        stale_reads.scene_exposure = lambda base, others, scene, out: {"cand": (20, 2, cand_px, 0),
                                                                       "control": (20, 2, control_px, 0)}
        return call(stale_reads.main, runs)

    def test_stale_reads_fails_when_candidate_exceeds_control(self):
        self.assertEqual(self.stale(9, 8)[0], 1)

    def test_stale_reads_passes_at_or_below_control(self):
        self.assertEqual(self.stale(8, 8)[0], 0)
        self.assertEqual(self.stale(0, 8)[0], 0)

    def test_stale_reads_needs_noise_only_runs(self):
        self.assertEqual(self.stale(0, 0, mode="on")[0], 1)

    def test_differing_pixels_counts_pixels(self):
        a, b = self.tmp / "a.ppm", self.tmp / "b.ppm"
        a.write_bytes(b"P6\n2 2\n255\n" + bytes(12))
        b.write_bytes(b"P6\n2 2\n255\n" + bytes([0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 1]))
        self.assertEqual(stale_reads.differing_pixels(a, b), 2)


if __name__ == "__main__":
    unittest.main()

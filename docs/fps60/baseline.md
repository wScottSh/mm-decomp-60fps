# fps60 baseline: the reference build

This is the first run of the fps60 harness (`tools/fps60/`) on the reference build: a `BENCH=1` build of this tree, retail game code with the bench hooks. Later units compare their candidates against a run of the same harness, not against these numbers. Every number here was measured on 2026-10-25 on unicron (AMD Ryzen AI MAX+ 395, 32 threads) with the pinned runner.

- Reference build: `make -j32 BENCH=1` at commit `119fa3c81` (branch `harness/H1a`). Later commits on that branch change only `tools/` and `docs/`, so `src/` and `include/` are the same at its head. ROM sha256 `3453508b29662a0d`. The plain build at the same commit still matches (`build/n64-us/mm-n64-us.z64: OK`).
- Runner: `~/n64-timing/build/mm60-runner/rundir/bin/n64-run`, sha256 `1f9d59f66929c5b5` (ares `54f35f9c7`).
- Command: `tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --check-determinism` (4 jobs). The tables are `tools/fps60/report.py RESULTS/run1`.
- Determinism: `determinism: PASS, 50 files byte-identical (host.txt, wall.tsv excluded)`. `fidelity.py RESULTS/run1 RESULTS/run2` passes every scene.

Presentation (fields per displayed image, complete images in the window):

| scene | fields | images | img_n1 | img_n2 | img_n3 | img_n4 | img_n5 | img_n6plus | image_share_at_1 | image_fields_max |
|---|---|---|---|---|---|---|---|---|---|---|
| title | 600 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0.0 | 3 |
| filesel | 600 | 472 | 347 | 125 | 0 | 0 | 0 | 0 | 0.735169 | 2 |
| sct | 600 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0.0 | 3 |
| field | 600 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0.0 | 3 |
| mountain | 600 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0.0 | 3 |
| greatbay | 600 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0.0 | 3 |

Tick cadence (fields per logic tick):

| scene | divisors | ticks | tick_n1 | tick_n2 | tick_n3 | tick_n4 | tick_n5 | tick_n6plus | lag_ticks | skipped_ticks | tick_fields_max | settle_tick | window_start_frame |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| title | 3 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0 | 0 | 3 | 127 | 218 |
| filesel | 1 | 472 | 347 | 125 | 0 | 0 | 0 | 0 | 125 | 0 | 2 | 14 | 257 |
| sct | 3 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0 | 0 | 3 | 22 | 130 |
| field | 3 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0 | 0 | 3 | 211 | 712 |
| mountain | 3 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0 | 0 | 3 | 22 | 128 |
| greatbay | 3 | 198 | 0 | 0 | 198 | 0 | 0 | 0 | 0 | 0 | 3 | 201 | 676 |

Wall time (host, not deterministic):

| run | wall_s | jobs | load_before | load_after |
|---|---|---|---|---|
| title | 40.9 | 4 | 1.65 | 3.20 |
| filesel | 51.4 | 4 | 1.65 | 3.47 |
| sct | 61.9 | 4 | 1.65 | 3.64 |
| field | 101.9 | 4 | 1.65 | 3.75 |
| mountain | 49.8 | 4 | 3.20 | 3.70 |
| greatbay | 96.8 | 4 | 3.47 | 2.40 |
| total | 148.6 | 4 | 1.65 | 2.40 |

## Reading the numbers

- Every Play scene presents one image per tick at 3 fields per image, with no lag tick in the 600-field window. The 60 fps target needs `image_share_at_1` = 1, so every Play scene is at 0.
- `filesel` (two named files) runs at divisor 1 and lags on 125 of 472 ticks (2 fields instead of 1). This is the named-file slowdown that mmbench measures on retail as about 1.69 fields per game frame (ares fork, `tools/n64-timing/mmbench/README.md`). Its count moves with code layout: an earlier build of this branch, with different `bench.c` code, measured 122 lag ticks of 475.
- `title` is the title Play scene from the retail boot (the mask intro), from the end of its fade-in.
- `field` and `greatbay` start after their entrance cutscenes, about 200 ticks after boot. Each run takes about 100 s for that reason.
- Wall time comes from one run with 4 runner processes in parallel on a shared host. It is the run time of the harness, not a game measurement.

## Known limit: RDP noise makes F1 timing-sensitive

The RDP's noise generator steps once per RDP clock, so dithered pixels depend on when the RDP draws them. This holds on a console (Thar0/RDP-Noise console dumps, cited in the fork's `ares/n64/rdp/timed.hpp`), and the runner models it. Any change that moves RDP timing changes dithered pixels by one 5-bit step, even when the game is identical. For example, Tatl's glow in South Clock Town and the mask in the title change. The H1a report has the measurements: a build that only adds dead code in `graph.c` fails F1 in 5 of 6 scenes, and a same-layout control fails only in the scene where it draws. Until that is resolved, F1 as defined (pixel-identical keyframes) rejects every change that alters timing.

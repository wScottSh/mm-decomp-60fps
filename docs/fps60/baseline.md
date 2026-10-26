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

## Load (unit H1b)

The load metrics of `tools/fps60/README.md` (Load metrics), measured on 2026-10-25 on unicron with the pinned runner.

- Reference build: `make -j32 BENCH=1` at commit `13a2afd28` (branch `harness/H1b`). Later commits on that branch change only `tools/`, `docs/` and `README.md`. ROM sha256 `231160338bb5642e`. The plain build at the same commit still matches (`build/n64-us/mm-n64-us.z64: OK`).
- Runner: sha256 `1f9d59f66929c5b5`.
- Command: `tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --check-determinism`, then `tools/fps60/report.py RESULTS/run1`.
- Determinism: `determinism: PASS, 75 files byte-identical (host.txt, wall.tsv excluded)`.
- One field is 16.715 ms (median runner field, 1,567,040 PClocks in `sct`). Shares are of that.
- The load hooks moved code, so this build's presentation differs from H1a's in one scene: `filesel` shows 510 images with 87 lag ticks (H1a's build: 472 and 125). The five Play scenes are unchanged (198 ticks, no lag tick).

Load per field: max / p99 / p50 in ms (share of one field):

| scene | cpu | rsp | rsp_gfx | rsp_audio | rdp_pipe | rdp_buf |
|---|---|---|---|---|---|---|
| title | 9.77 / 9.51 / 2.94 (0.584 / 0.569 / 0.176) | 6.27 / 6.09 / 1.41 (0.375 / 0.365 / 0.084) | 4.78 / 4.63 / 0.00 (0.286 / 0.277 / 0.000) | 1.54 / 1.51 / 0.28 (0.092 / 0.090 / 0.017) | 16.55 / 16.55 / 3.46 (0.990 / 0.990 / 0.207) | 16.44 / 16.43 / 3.46 (0.983 / 0.983 / 0.207) |
| filesel | 16.83 / 7.91 / 6.96 (1.007 / 0.473 / 0.417) | 4.48 / 4.42 / 3.93 (0.268 / 0.264 / 0.235) | 2.35 / 2.35 / 2.28 (0.141 / 0.140 / 0.136) | 2.20 / 2.14 / 1.72 (0.132 / 0.128 / 0.103) | 16.59 / 16.48 / 15.96 (0.993 / 0.986 / 0.955) | 16.38 / 15.77 / 15.59 (0.980 / 0.944 / 0.933) |
| sct | 16.80 / 16.79 / 10.62 (1.005 / 1.005 / 0.635) | 16.44 / 16.43 / 8.93 (0.984 / 0.983 / 0.534) | 15.88 / 15.50 / 7.06 (0.950 / 0.927 / 0.422) | 4.50 / 4.18 / 2.79 (0.269 / 0.250 / 0.167) | 16.69 / 16.66 / 11.43 (0.999 / 0.997 / 0.684) | 16.40 / 16.39 / 10.98 (0.981 / 0.981 / 0.657) |
| field | 16.79 / 16.76 / 11.65 (1.004 / 1.003 / 0.697) | 16.38 / 16.37 / 3.74 (0.980 / 0.980 / 0.224) | 14.43 / 14.38 / 1.14 (0.863 / 0.860 / 0.068) | 3.67 / 3.46 / 2.54 (0.220 / 0.207 / 0.152) | 16.53 / 16.52 / 5.36 (0.989 / 0.988 / 0.321) | 15.84 / 15.68 / 4.54 (0.948 / 0.938 / 0.271) |
| mountain | 10.81 / 10.66 / 9.13 (0.647 / 0.638 / 0.546) | 15.97 / 15.05 / 1.13 (0.956 / 0.900 / 0.067) | 14.45 / 14.34 / 0.00 (0.864 / 0.858 / 0.000) | 1.53 / 1.52 / 0.58 (0.092 / 0.091 / 0.035) | 16.48 / 16.48 / 8.46 (0.986 / 0.986 / 0.506) | 16.34 / 16.34 / 8.46 (0.978 / 0.978 / 0.506) |
| greatbay | 16.73 / 16.73 / 10.41 (1.001 / 1.001 / 0.623) | 16.41 / 16.40 / 9.36 (0.982 / 0.981 / 0.560) | 15.00 / 14.74 / 7.26 (0.897 / 0.882 / 0.434) | 3.05 / 2.95 / 2.03 (0.182 / 0.177 / 0.121) | 16.55 / 16.54 / 10.99 (0.990 / 0.989 / 0.657) | 16.42 / 16.41 / 9.23 (0.982 / 0.982 / 0.552) |

Load per logic tick: max / p99 / p50 in ms (share of one field):

| scene | cpu | rsp | rsp_gfx | rsp_audio | rdp_pipe | rdp_buf |
|---|---|---|---|---|---|---|
| title | 15.95 / 15.88 / 12.75 (0.954 / 0.950 / 0.763) | 9.13 / 9.07 / 5.32 (0.546 / 0.542 / 0.318) | 4.78 / 4.77 / 4.35 (0.286 / 0.285 / 0.260) | 4.44 / 4.42 / 0.79 (0.266 / 0.265 / 0.048) | 26.88 / 26.70 / 20.00 (1.608 / 1.597 / 1.196) | 26.77 / 26.59 / 19.89 (1.601 / 1.591 / 1.190) |
| filesel | 18.72 / 10.19 / 7.10 (1.120 / 0.610 / 0.425) | 7.26 / 6.77 / 3.95 (0.434 / 0.405 / 0.236) | 3.44 / 3.06 / 2.28 (0.206 / 0.183 / 0.137) | 4.37 / 4.21 / 1.69 (0.261 / 0.252 / 0.101) | 19.36 / 18.96 / 15.96 (1.158 / 1.134 / 0.955) | 18.56 / 17.70 / 15.62 (1.110 / 1.059 / 0.935) |
| sct | 42.65 / 40.15 / 31.03 (2.552 / 2.402 / 1.856) | 39.39 / 37.96 / 27.55 (2.357 / 2.271 / 1.648) | 26.60 / 26.21 / 20.43 (1.591 / 1.568 / 1.222) | 15.08 / 13.01 / 8.42 (0.902 / 0.778 / 0.504) | 39.90 / 39.67 / 27.81 (2.387 / 2.373 / 1.664) | 31.65 / 31.47 / 26.23 (1.893 / 1.883 / 1.569) |
| field | 36.57 / 36.36 / 31.03 (2.188 / 2.175 / 1.856) | 25.95 / 25.47 / 22.55 (1.553 / 1.524 / 1.349) | 16.37 / 16.33 / 15.01 (0.979 / 0.977 / 0.898) | 10.44 / 10.41 / 7.67 (0.625 / 0.623 / 0.459) | 25.18 / 24.82 / 21.85 (1.506 / 1.485 / 1.307) | 23.93 / 23.90 / 19.59 (1.432 / 1.430 / 1.172) |
| mountain | 23.80 / 23.78 / 21.74 (1.424 / 1.423 / 1.300) | 18.87 / 18.09 / 15.40 (1.129 / 1.082 / 0.921) | 14.45 / 14.40 / 12.96 (0.864 / 0.862 / 0.775) | 4.45 / 4.44 / 1.67 (0.266 / 0.266 / 0.100) | 25.54 / 25.52 / 24.94 (1.528 / 1.527 / 1.492) | 25.40 / 25.38 / 24.80 (1.519 / 1.518 / 1.484) |
| greatbay | 38.15 / 37.70 / 36.33 (2.283 / 2.255 / 2.173) | 30.50 / 30.33 / 27.71 (1.825 / 1.815 / 1.658) | 22.03 / 21.96 / 21.42 (1.318 / 1.313 / 1.281) | 8.81 / 8.79 / 6.02 (0.527 / 0.526 / 0.360) | 29.50 / 29.48 / 27.50 (1.765 / 1.764 / 1.645) | 25.56 / 25.56 / 25.19 (1.529 / 1.529 / 1.507) |

Graph thread CPU per tick by system, p50 / max ms:

| scene | other | actor_update | actor_draw | collision | room_draw | skybox | environment | camera | message | effect | audio_update | bench |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| title | 1.10 / 1.23 | 1.25 / 1.37 | 2.10 / 2.11 | 0.05 / 0.11 | 0.18 / 0.18 | 0.05 / 0.11 | 0.23 / 0.28 | 0.40 / 0.50 | 0.13 / 0.22 | 0.36 / 0.45 | 0.19 / 0.31 | 0.06 / 0.12 |
| filesel | 3.83 / 3.99 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.15 / 0.25 | 0.06 / 0.16 |
| sct | 1.09 / 1.31 | 6.16 / 7.42 | 5.77 / 7.47 | 0.19 / 0.29 | 0.24 / 0.38 | 0.06 / 0.11 | 0.33 / 0.48 | 1.66 / 2.58 | 1.51 / 2.08 | 0.40 / 0.90 | 0.35 / 0.47 | 0.06 / 0.08 |
| field | 1.07 / 1.26 | 8.01 / 12.43 | 4.57 / 5.41 | 0.36 / 1.23 | 0.45 / 0.64 | 0.07 / 0.17 | 0.34 / 0.49 | 1.77 / 2.57 | 1.40 / 1.73 | 0.45 / 1.00 | 0.37 / 0.49 | 0.06 / 0.11 |
| mountain | 1.03 / 1.14 | 5.77 / 5.94 | 3.08 / 3.27 | 0.26 / 0.26 | 0.40 / 0.51 | 0.05 / 0.14 | 0.28 / 0.38 | 1.98 / 2.26 | 1.40 / 1.64 | 0.37 / 0.37 | 0.15 / 0.26 | 0.06 / 0.07 |
| greatbay | 1.07 / 1.15 | 8.65 / 9.00 | 11.19 / 11.65 | 0.29 / 0.29 | 0.51 / 0.51 | 0.06 / 0.07 | 0.35 / 0.36 | 1.46 / 1.63 | 1.60 / 1.69 | 0.45 / 0.95 | 0.25 / 0.32 | 0.06 / 0.06 |

Graph_Update phases per tick, p50 wall / p50 CPU ms:

| scene | pre | update | draw | game | post | rcp_wait | tail |
|---|---|---|---|---|---|---|---|
| title | 0.38 / 0.33 | 2.71 / 2.49 | 3.09 / 3.09 | 0.11 / 0.11 | 0.04 / 0.04 | 43.62 / 0.03 | 0.13 / 0.10 |
| filesel | 0.31 / 0.30 | 0.00 / 0.00 | 0.00 / 0.00 | 3.73 / 3.58 | 0.06 / 0.05 | 9.81 / 0.03 | 0.10 / 0.07 |
| sct | 0.51 / 0.51 | 12.58 / 8.84 | 11.50 / 8.33 | 0.20 / 0.20 | 0.05 / 0.05 | 26.64 / 0.03 | 0.15 / 0.12 |
| field | 0.50 / 0.50 | 14.79 / 11.14 | 7.31 / 7.13 | 0.19 / 0.19 | 0.04 / 0.04 | 27.19 / 0.03 | 0.15 / 0.12 |
| mountain | 0.27 / 0.27 | 10.80 / 8.71 | 5.65 / 5.55 | 0.19 / 0.19 | 0.04 / 0.04 | 33.06 / 0.03 | 0.15 / 0.12 |
| greatbay | 0.38 / 0.38 | 14.21 / 11.06 | 17.61 / 14.08 | 0.22 / 0.22 | 0.05 / 0.05 | 17.57 / 0.04 | 0.15 / 0.12 |

CPU per tick by thread, p50 / max ms, and hook cost:

| scene | graph | audio | sched | irqmgr | padmgr | dmamgr | hook_ms_p50 | clock_reads_p50 |
|---|---|---|---|---|---|---|---|---|
| title | 6.18 / 6.40 | 4.16 / 7.13 | 0.76 / 0.85 | 0.14 / 0.14 | 1.14 / 1.42 | 0.09 / 0.12 | 0.051 | 67 |
| filesel | 4.07 / 4.27 | 2.17 / 14.02 | 0.47 / 0.84 | 0.05 / 0.11 | 0.30 / 0.58 | 0.00 / 0.00 | 0.007 | 9 |
| sct | 18.31 / 21.21 | 11.91 / 19.00 | 0.94 / 1.12 | 0.16 / 0.20 | 0.85 / 1.11 | 0.22 / 0.33 | 0.110 | 143 |
| field | 19.06 / 25.42 | 9.62 / 12.29 | 0.90 / 0.95 | 0.16 / 0.17 | 0.81 / 0.99 | 0.14 / 0.33 | 0.167 | 217 |
| mountain | 14.93 / 15.27 | 4.71 / 6.48 | 0.83 / 0.87 | 0.15 / 0.15 | 0.78 / 0.89 | 0.09 / 0.14 | 0.070 | 91 |
| greatbay | 25.92 / 26.99 | 7.98 / 9.40 | 0.93 / 0.98 | 0.16 / 0.17 | 0.84 / 0.95 | 0.10 / 0.14 | 0.200 | 261 |

Top actors by update + draw, ms per tick (update / draw):

| scene | #1 | #2 | #3 | #4 | #5 |
|---|---|---|---|---|---|
| title | PLAYER 0.68 / 1.57 | EN_MAG 0.01 / 0.24 | DM_CHAR05 0.07 / 0.16 | EN_OSN 0.13 / 0.00 |  |
| filesel |  |  |  |  |  |
| sct | PLAYER 1.48 / 2.19 | EN_TEST3 1.13 / 0.00 | EN_DAIKU 0.45 / 0.67 | EN_DG 0.69 / 0.36 | EN_STH 0.17 / 0.28 |
| field | PLAYER 1.44 / 2.08 | EN_RUPPECROW 0.68 / 0.30 | EN_INVISIBLE_RUPPE 0.57 / 0.00 | OBJ_GRASS 0.33 / 0.12 | EN_ELF 0.14 / 0.24 |
| mountain | PLAYER 1.32 / 2.15 | EN_GEG 0.80 / 0.00 | EN_GO 0.70 / 0.00 | OBJECT_KANKYO 0.29 / 0.31 | EN_ELF 0.14 / 0.26 |
| greatbay | EN_TANRON4 1.68 / 6.55 | PLAYER 1.22 / 1.94 | EN_ZOW 0.65 / 0.00 | EN_ELF 0.15 / 0.25 | EN_KAKASI 0.31 / 0.00 |

### Reading the load numbers

- Per tick is the retail 20 fps tick (3 fields in Play). At 60 fps each field needs a new image, so a resource's per-field budget is one field. Per-tick values above 1.0 show how much one tick's work exceeds a field. How the logic and the per-field renders split across fields is A1's question, and B1 turns these numbers into the gap.
- CPU per tick in Play is 12.8 to 36.3 ms at p50 (title to greatbay), against 16.7 ms per field. The graph thread's update plus draw is the largest part, and the AudioMgr thread (audio synthesis) takes 4.2 to 11.9 ms per tick.
- `rdp_pipe` is near a full field in the busiest field of every scene, because the fork's `PIPE_BUSY` stays set until `SYNC_FULL`, while the RDP waits for the RSP too. `rdp_buf` is lower where the RDP starves (sct per tick p50 26.2 against 27.8 ms).
- Hook cost: `hook_ms_p50` is the per-tick cost of the per-system brackets (0.007 ms in filesel to 0.200 ms in greatbay). It is inside every number above and is the same in any build that plays the same game. It is the boot-time cost of one clock read times the reads per tick, and a planted extra read per call measured the in-game cost per read at 1.0 to 1.9 times the boot-time cost (greatbay highest), so the true hook cost is up to about 0.4 ms per tick (inferred).

## P2-ref corpus scenes (unit C1)

Four scenes for P2-ref part a's entry criteria (`docs/fps60/architecture.md`, Q9): `pause`, `transition`, `blur` and `moving` (`tools/fps60/README.md`, Scenes). Measured on 2026-10-25 on unicron with the pinned runner.

- Reference build: `make -j32 BENCH=1` at commit `98010a7d6` (branch `harness/C1`). ROM sha256 `807323e89e9a0fe9`. Runner sha256 `1f9d59f66929c5b5`.
- Commands: `tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --check-determinism` (noise pin on, all 10 scenes), `tools/fps60/fidelity.py RESULTS/run1 RESULTS/run2`, and `tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out PERF --noise-pin off`.
- Determinism: `determinism: PASS, 133 files byte-identical (host.txt, wall.tsv excluded)`. F1 of the run against itself: PASS in all 10 scenes. The perf run (pin off) completes all 10 scenes.
- An earlier determinism run of the same build failed on one file only, `title/tmpqx6yh6h0/rom.z64` in `run2`: `run.py`'s temporary ROM directory was left behind after that scene had finished. Every measured output was identical. The cause is not known.

Presentation, cadence and holds (complete images and ticks in the 600-field window; wall time in seconds, host, 4 jobs):

| scene | noise pin | divisors | ticks | images | image_fields_max | lag_ticks | skipped_ticks | hold_fields | retaken_cfb_fields | settle_tick | window_start_frame | wall_s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| pause | on | 2,3 | 232 | 232 | 25 | 5 | 2 | 29 | 1 | 22 | 132 | 54.1 |
| transition | on | 3 | 187 | 187 | 37 | 1 | 0 | 0 | 0 | 211 | 716 | 110.2 |
| blur | on | 3 | 194 | 194 | 4 | 13 | 0 | 0 | 0 | 22 | 132 | 69.8 |
| moving | on | 3 | 199 | 199 | 3 | 0 | 0 | 0 | 0 | 211 | 716 | 103.0 |
| pause | off | 2,3 | 233 | 233 | 24 | 3 | 2 | 28 | 1 | 22 | 131 | 57.5 |
| transition | off | 3 | 187 | 187 | 37 | 1 | 0 | 0 | 0 | 211 | 713 | 111.5 |
| blur | off | 3 | 198 | 198 | 3 | 0 | 0 | 0 | 0 | 22 | 130 | 68.3 |
| moving | off | 3 | 198 | 198 | 3 | 0 | 0 | 0 | 0 | 211 | 713 | 105.7 |

Ticks with each `BENCH_STATE_*` flag in the window (noise pin on; the pin-off run differs by at most one tick per column):

| scene | capture | pause_filter | pause | page_turn | motion_blur | transition | tile | play |
|---|---|---|---|---|---|---|---|---|
| pause | 2 | 10 | 146 | 8 | 0 | 1 | 0 | 236 |
| transition | 0 | 0 | 0 | 0 | 0 | 44 | 0 | 189 |
| blur | 0 | 0 | 0 | 0 | 42 | 1 | 0 | 196 |
| moving | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 201 |

Why each scene exercises its feature:

- `pause`: Start opens the menu on tick 10 after settling. The capture tick and the draw-skipped tick after it never swap (`capture` 2), the pause background filters for 10 ticks (`pause_filter`), the R press turns to the map page over 8 ticks (`page_turn`), and Start closes it. Kaleido runs at divisor 2. `hold_fields` 29 = 3 fields of the two capture ticks after tick 32, plus 19 fields of the first filter tick's image and 1 field each for the 7 filter ticks after it.
- `transition`: Link runs back through the gate tunnel into West Clock Town. `transition` is set on 44 ticks: fade out, scene load, fade in.
- `blur`: C-down takes off the Deku Mask. `Player_Action_86` enables priority motion blur (`motion_blur` 42 ticks), and `sMotionBlurStatus` (`z_play.c`) reads `MOTION_BLUR_PROCESS`, the state in which `Play_DrawMotionBlur` blends the previous image, on 139 of the 600 window fields (a per-field peek of that byte).
- `moving`: the camera's view direction (`PlayState.view`, eye to at, peeked each field) turns 381 degrees net over the window, from -90 to -111 degrees after one full turn. Link crosses the gate bridge, runs along the town wall, and onto the grass (frame dumps).

### Reading the new scenes

- No tile transition is reachable. MM never sets `gTransitionTileState` to `TRANS_TILE_SETUP` (no writer in `src/`), so `tile` is 0 everywhere and the divisor-1 Play tick of the architecture's table does not occur in retail. `transition` is a scene change at divisor 3.
- Two retail freezes are not holds under preferences line 24, so `hold_fields` does not count them. Closing the pause menu holds tick 178's image for 25 fields (24 with the pin off), because the first unpaused tick (179) lasts 432 ms from its start to the next tick's, with the CPU busy for 179 ms of it (pin off: 413 ms and 161 ms; `load_ticks.tsv` `wall` and `cpu_busy`). The scene change holds one image for 37 fields while the new scene loads: tick 243 lasts 532 ms with the CPU busy for 207 ms (pin off). The rest of those ticks is CPU idle, so the graph thread waits on something else, probably DMA (inferred, not traced). Under the Replay architecture no new image exists for those fields, so they fail the presentation rule unless they become a listed exception.
- `pause` shows 1 `retaken_cfb_fields`: after the capture tick, tick 35 took the `CfbInfo` the VI still showed (see `tools/fps60/README.md`, Retail holds and tick states).
- `blur` lags 13 ticks with the pin on and none with it off. The pin's display-list walk and `G_PM_1PRIMITIVE` add CPU and RDP time, and motion blur's full-screen blend is the heaviest RDP load in the corpus (per tick `rdp_pipe` max 45.3 ms, 2.71 fields, pin off).

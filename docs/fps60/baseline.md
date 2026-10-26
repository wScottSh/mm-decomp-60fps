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

- Reference build: `make -j32 BENCH=1` at commit `c05eaddb2` (branch `harness/H1b`). Later commits on that branch change only `tools/`, `docs/` and `README.md`. ROM sha256 `e8fcc2284813a6db`. The plain build at the same commit still matches (`build/n64-us/mm-n64-us.z64: OK`).
- Runner: sha256 `1f9d59f66929c5b5`.
- Command: `tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --check-determinism`, then `tools/fps60/report.py RESULTS/run1`.
- Determinism: `determinism: PASS, 75 files byte-identical (host.txt, wall.tsv excluded)`.
- One field is 16.715 ms (median runner field, 1,567,040 PClocks in `sct`). Shares are of that.
- The load hooks moved code, so this build's presentation differs from H1a's in one scene: `filesel` shows 514 images with 83 lag ticks (H1a's build: 472 and 125). The five Play scenes are unchanged (198 ticks, no lag tick).

Load per field: max / p99 / p50 in ms (share of one field):

| scene | cpu | rsp | rsp_gfx | rsp_audio | rdp_pipe | rdp_buf |
|---|---|---|---|---|---|---|
| title | 9.73 / 9.46 / 2.90 (0.582 / 0.566 / 0.174) | 6.28 / 6.10 / 1.41 (0.376 / 0.365 / 0.085) | 4.79 / 4.62 / 0.00 (0.286 / 0.277 / 0.000) | 1.53 / 1.50 / 0.28 (0.091 / 0.090 / 0.017) | 16.55 / 16.55 / 3.47 (0.990 / 0.990 / 0.207) | 16.44 / 16.43 / 3.47 (0.983 / 0.983 / 0.207) |
| filesel | 16.83 / 7.80 / 6.93 (1.007 / 0.467 / 0.414) | 4.47 / 4.41 / 3.93 (0.268 / 0.264 / 0.235) | 2.36 / 2.35 / 2.28 (0.141 / 0.141 / 0.136) | 2.18 / 2.13 / 1.71 (0.130 / 0.128 / 0.102) | 16.55 / 16.49 / 15.96 (0.990 / 0.986 / 0.955) | 16.36 / 15.76 / 15.59 (0.979 / 0.943 / 0.933) |
| sct | 16.80 / 16.80 / 10.26 (1.005 / 1.005 / 0.614) | 16.47 / 16.42 / 9.12 (0.985 / 0.982 / 0.546) | 16.06 / 15.64 / 7.08 (0.961 / 0.936 / 0.423) | 4.32 / 4.16 / 2.77 (0.258 / 0.249 / 0.166) | 16.68 / 16.66 / 11.31 (0.998 / 0.997 / 0.677) | 16.40 / 16.39 / 10.99 (0.981 / 0.981 / 0.658) |
| field | 16.79 / 16.77 / 11.51 (1.004 / 1.003 / 0.688) | 16.39 / 16.37 / 3.74 (0.981 / 0.979 / 0.224) | 14.47 / 14.38 / 1.14 (0.866 / 0.861 / 0.068) | 3.67 / 3.46 / 2.56 (0.220 / 0.207 / 0.153) | 16.54 / 16.52 / 5.42 (0.990 / 0.988 / 0.324) | 15.88 / 15.71 / 4.56 (0.950 / 0.940 / 0.273) |
| mountain | 10.77 / 10.63 / 9.07 (0.645 / 0.636 / 0.543) | 15.97 / 15.03 / 1.13 (0.956 / 0.899 / 0.067) | 14.52 / 14.34 / 0.00 (0.869 / 0.858 / 0.000) | 1.54 / 1.50 / 0.58 (0.092 / 0.090 / 0.035) | 16.48 / 16.48 / 8.46 (0.986 / 0.986 / 0.506) | 16.34 / 16.34 / 8.46 (0.978 / 0.978 / 0.506) |
| greatbay | 16.74 / 16.73 / 10.17 (1.001 / 1.001 / 0.608) | 16.40 / 16.40 / 9.20 (0.981 / 0.981 / 0.550) | 14.91 / 14.73 / 7.21 (0.892 / 0.881 / 0.431) | 3.02 / 2.94 / 1.99 (0.180 / 0.176 / 0.119) | 16.54 / 16.54 / 10.85 (0.990 / 0.990 / 0.649) | 16.42 / 16.41 / 9.13 (0.982 / 0.982 / 0.546) |

Load per logic tick: max / p99 / p50 in ms (share of one field):

| scene | cpu | rsp | rsp_gfx | rsp_audio | rdp_pipe | rdp_buf |
|---|---|---|---|---|---|---|
| title | 15.80 / 15.71 / 12.76 (0.945 / 0.940 / 0.763) | 9.14 / 9.08 / 5.31 (0.547 / 0.543 / 0.318) | 4.79 / 4.78 / 4.35 (0.286 / 0.286 / 0.260) | 4.44 / 4.43 / 0.79 (0.265 / 0.265 / 0.047) | 26.87 / 26.68 / 20.00 (1.608 / 1.596 / 1.197) | 26.76 / 26.57 / 19.89 (1.601 / 1.590 / 1.190) |
| filesel | 18.70 / 10.11 / 7.06 (1.119 / 0.605 / 0.422) | 6.87 / 6.74 / 3.95 (0.411 / 0.404 / 0.236) | 3.13 / 3.00 / 2.28 (0.187 / 0.179 / 0.137) | 4.32 / 4.21 / 1.69 (0.258 / 0.252 / 0.101) | 19.06 / 18.90 / 15.97 (1.140 / 1.131 / 0.956) | 17.77 / 17.66 / 15.62 (1.063 / 1.056 / 0.934) |
| sct | 40.97 / 39.17 / 30.36 (2.451 / 2.343 / 1.816) | 38.24 / 37.65 / 27.38 (2.288 / 2.252 / 1.638) | 26.59 / 26.28 / 20.52 (1.591 / 1.572 / 1.228) | 13.82 / 12.65 / 8.31 (0.827 / 0.757 / 0.497) | 39.65 / 39.27 / 27.77 (2.372 / 2.349 / 1.661) | 31.43 / 31.41 / 26.03 (1.881 / 1.879 / 1.557) |
| field | 36.45 / 36.17 / 30.63 (2.181 / 2.164 / 1.832) | 25.95 / 25.43 / 22.63 (1.553 / 1.521 / 1.354) | 16.32 / 16.31 / 14.99 (0.977 / 0.976 / 0.897) | 10.45 / 10.40 / 7.70 (0.625 / 0.622 / 0.461) | 25.16 / 24.84 / 21.92 (1.505 / 1.486 / 1.311) | 23.92 / 23.90 / 19.58 (1.431 / 1.430 / 1.172) |
| mountain | 23.66 / 23.62 / 21.63 (1.415 / 1.413 / 1.294) | 18.79 / 18.02 / 15.41 (1.124 / 1.078 / 0.922) | 14.52 / 14.40 / 12.96 (0.869 / 0.862 / 0.775) | 4.45 / 4.44 / 1.66 (0.266 / 0.265 / 0.099) | 25.55 / 25.52 / 24.93 (1.529 / 1.527 / 1.492) | 25.41 / 25.38 / 24.79 (1.520 / 1.519 / 1.483) |
| greatbay | 37.82 / 37.66 / 36.06 (2.263 / 2.253 / 2.158) | 30.52 / 30.22 / 27.58 (1.826 / 1.808 / 1.650) | 22.10 / 22.01 / 21.40 (1.322 / 1.317 / 1.280) | 8.83 / 8.76 / 5.96 (0.528 / 0.524 / 0.357) | 29.46 / 29.32 / 27.36 (1.762 / 1.754 / 1.637) | 25.56 / 25.50 / 25.15 (1.529 / 1.526 / 1.505) |

Graph thread CPU per tick by system, p50 / max ms:

| scene | other | actor_update | actor_draw | collision | room_draw | skybox | environment | camera | message | effect | audio_update | bench |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| title | 1.10 / 1.26 | 1.27 / 1.39 | 2.10 / 2.12 | 0.05 / 0.12 | 0.18 / 0.18 | 0.05 / 0.06 | 0.23 / 0.28 | 0.40 / 0.51 | 0.13 / 0.22 | 0.35 / 0.44 | 0.19 / 0.33 | 0.06 / 0.11 |
| filesel | 3.82 / 3.97 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.16 / 0.27 | 0.06 / 0.15 |
| sct | 1.09 / 1.27 | 6.20 / 7.44 | 5.75 / 7.51 | 0.19 / 0.27 | 0.24 / 0.40 | 0.06 / 0.19 | 0.33 / 0.45 | 1.69 / 2.57 | 1.51 / 2.08 | 0.38 / 0.88 | 0.35 / 0.54 | 0.06 / 0.11 |
| field | 1.07 / 1.23 | 8.02 / 12.52 | 4.57 / 5.47 | 0.37 / 1.23 | 0.43 / 0.63 | 0.07 / 0.18 | 0.34 / 0.52 | 1.79 / 2.59 | 1.40 / 1.75 | 0.44 / 1.04 | 0.37 / 0.49 | 0.06 / 0.07 |
| mountain | 1.03 / 1.13 | 5.83 / 6.01 | 3.06 / 3.23 | 0.26 / 0.26 | 0.38 / 0.49 | 0.05 / 0.15 | 0.28 / 0.38 | 1.96 / 2.25 | 1.41 / 1.64 | 0.35 / 0.35 | 0.15 / 0.26 | 0.06 / 0.07 |
| greatbay | 1.07 / 1.17 | 8.43 / 8.75 | 11.45 / 11.94 | 0.28 / 0.29 | 0.47 / 0.47 | 0.06 / 0.07 | 0.35 / 0.37 | 1.51 / 1.66 | 1.62 / 1.76 | 0.43 / 0.94 | 0.26 / 0.31 | 0.06 / 0.07 |

Graph_Update phases per tick, p50 wall / p50 CPU ms:

| scene | pre | update | draw | game | post | rcp_wait | tail |
|---|---|---|---|---|---|---|---|
| title | 0.37 / 0.33 | 2.72 / 2.51 | 3.08 / 3.08 | 0.11 / 0.11 | 0.04 / 0.04 | 43.62 / 0.03 | 0.13 / 0.10 |
| filesel | 0.31 / 0.31 | 0.00 / 0.00 | 0.00 / 0.00 | 3.73 / 3.58 | 0.06 / 0.05 | 9.85 / 0.03 | 0.10 / 0.07 |
| sct | 0.51 / 0.51 | 12.48 / 8.80 | 11.55 / 8.32 | 0.20 / 0.19 | 0.05 / 0.05 | 25.39 / 0.03 | 0.15 / 0.12 |
| field | 0.50 / 0.50 | 14.66 / 11.10 | 7.34 / 7.09 | 0.19 / 0.19 | 0.04 / 0.04 | 27.35 / 0.03 | 0.15 / 0.12 |
| mountain | 0.27 / 0.27 | 10.81 / 8.75 | 5.61 / 5.51 | 0.19 / 0.19 | 0.04 / 0.04 | 33.09 / 0.03 | 0.15 / 0.12 |
| greatbay | 0.37 / 0.37 | 13.93 / 10.89 | 17.77 / 14.33 | 0.22 / 0.22 | 0.05 / 0.05 | 17.71 / 0.03 | 0.15 / 0.12 |

CPU per tick by thread, p50 / max ms, and hook cost:

| scene | graph | audio | sched | irqmgr | padmgr | dmamgr | hook_ms_p50 | clock_reads_p50 |
|---|---|---|---|---|---|---|---|---|
| title | 6.18 / 6.39 | 4.15 / 6.98 | 0.77 / 0.85 | 0.14 / 0.15 | 1.14 / 1.41 | 0.09 / 0.12 | 0.054 | 67 |
| filesel | 4.07 / 4.24 | 2.11 / 14.01 | 0.47 / 0.86 | 0.05 / 0.11 | 0.29 / 0.59 | 0.00 / 0.00 | 0.007 | 9 |
| sct | 18.18 / 21.12 | 11.51 / 17.52 | 0.94 / 1.13 | 0.16 / 0.21 | 0.83 / 1.12 | 0.22 / 0.33 | 0.114 | 143 |
| field | 19.00 / 25.38 | 9.35 / 12.12 | 0.91 / 0.97 | 0.16 / 0.17 | 0.79 / 0.98 | 0.14 / 0.33 | 0.174 | 217 |
| mountain | 14.93 / 15.31 | 4.61 / 6.26 | 0.84 / 0.87 | 0.15 / 0.16 | 0.77 / 0.89 | 0.10 / 0.14 | 0.073 | 91 |
| greatbay | 26.00 / 27.05 | 7.66 / 9.14 | 0.94 / 0.98 | 0.17 / 0.18 | 0.83 / 0.93 | 0.10 / 0.14 | 0.209 | 261 |

Top actors by update + draw, ms per tick (update / draw):

| scene | #1 | #2 | #3 | #4 | #5 |
|---|---|---|---|---|---|
| title | PLAYER 0.69 / 1.57 | EN_MAG 0.01 / 0.24 | DM_CHAR05 0.06 / 0.17 | EN_OSN 0.13 / 0.00 |  |
| filesel |  |  |  |  |  |
| sct | PLAYER 1.47 / 2.17 | EN_TEST3 1.13 / 0.00 | EN_DAIKU 0.45 / 0.63 | EN_DG 0.70 / 0.37 | OBJ_TOKEIDAI 0.06 / 0.40 |
| field | PLAYER 1.43 / 2.07 | EN_RUPPECROW 0.68 / 0.31 | EN_INVISIBLE_RUPPE 0.56 / 0.00 | OBJ_GRASS 0.32 / 0.13 | EN_ELF 0.15 / 0.23 |
| mountain | PLAYER 1.33 / 2.14 | EN_GEG 0.80 / 0.00 | EN_GO 0.70 / 0.00 | OBJECT_KANKYO 0.33 / 0.32 | EN_ELF 0.14 / 0.26 |
| greatbay | EN_TANRON4 1.43 / 6.78 | PLAYER 1.24 / 1.99 | EN_ZOW 0.65 / 0.00 | EN_ELF 0.16 / 0.24 | EN_KAKASI 0.32 / 0.00 |

### Reading the load numbers

- Per tick is the retail 20 fps tick (3 fields in Play). At 60 fps each field needs a new image, so a resource's per-field budget is one field. Per-tick values above 1.0 show how much one tick's work exceeds a field. How the logic and the per-field renders split across fields is A1's question, and B1 turns these numbers into the gap.
- CPU per tick in Play is 12.8 to 36.1 ms at p50 (title to greatbay), against 16.7 ms per field. The graph thread's update plus draw is the largest part, and the AudioMgr thread (audio synthesis) takes 4.2 to 11.5 ms per tick.
- `rdp_pipe` is near a full field in the busiest field of every scene, because the fork's `PIPE_BUSY` stays set until `SYNC_FULL`, while the RDP waits for the RSP too. `rdp_buf` is lower where the RDP starves (sct per tick p50 26.0 against 27.8 ms).
- Hook cost: `hook_ms_p50` is the per-tick cost of the per-system brackets (0.007 ms in filesel to 0.209 ms in greatbay). It is inside every number above and is the same in any build that plays the same game. The H1b report measures it.

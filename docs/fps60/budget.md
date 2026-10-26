# fps60 budget: how far retail is from 60 fps under Replay

This is the output of unit B1. It measures, per corpus scene and per resource, how much time retail needs for the work that the Replay architecture ([architecture.md](architecture.md)) would run per tick and per image, compares it with the 60 fps budget, shows where that time goes, and gives a lower bound for the RDP. The ranked hypothesis backlog that P3 units execute is `backlog.tsv` in the program store (`~/.claude/orchestrate/mm-60fps/backlog.tsv`).

## How to read this document

- **Labels.** Every number is **measured** (run on the pinned runner, sha256 `1f9d59f66929c5b5`), **cited** (read from a document or code), or **inferred** (derived from measured or cited numbers, not run). Tables say which once, in their caption.
- **Two ROMs.** The headline CPU numbers and the per-tick cross-checks come from the unmodified `BENCH=1` build of main `11e09bb80` (ROM sha256 `231160338bb5642e`, the same ROM as the H1b baseline, determinism PASS on 75 files). The per-image RCP numbers, the segment split, the RDRAM traffic and the Z coverage come from the same tree plus temporary B1 instrumentation (ROM sha256 `2c4b17e9c65f5884`, not committed; patch and scripts in the program store under `reports/B1-tools/`). The instrumentation adds code, so it moves code layout. In the five Play scenes presentation and tick cadence are unchanged (198 ticks, no lag tick), per-tick RSP and RDP differ by at most 1.9% from the main ROM, and per-tick CPU by -4.3% to +3.2% (greatbay max 38.15 to 36.49 ms, sct p99 40.15 to 41.43 ms). That CPU shift is itself a finding, see [CPU layout sensitivity](#cpu-layout-sensitivity).
- **Budgets.** `T_f` is the median runner field, 16.715 ms (measured). Per preferences lines 3 and 23: CPU per tick at most `0.90 x d x T_f` (45.13 ms at d = 3, 15.04 ms at d = 1); RSP, RDP and the first-image bound `W_t` at most `0.90 x T_f` = 15.04 ms per field. RDP busy is `rdp_buf` (`DPC_BUFBUSY`), not `rdp_pipe`, which includes time the RDP waits on the RSP (H1b note, `tools/fps60/README.md`).
- **Per image.** Replay renders the tick's retail display list once per field, so the RSP and RDP need, per field, what one retail gfx task needs. The instrumentation records each gfx task: RSP start to RDP full sync (`W_t`, the quantity of retail's `gRDPTimeAcc`), non-halted RSP time outside audio tasks (`RSP gfx`), and `DPC_BUFBUSY` (`RDP busy`). One task per tick in Play, so one record is one Replay image. Statistics are over the 198 tasks of each 600-field window (512 in filesel), max, p99 and p50 by nearest rank.

## Commands

```sh
tools/fps60/worktree-setup.sh B1 analyze/B1 && make -j24 BENCH=1
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out b1-out/base --check-determinism   # main ROM
# instrumented ROM (patch applied), every Play scene, then one run per skipped segment:
python3 b1-tools/b1run.py ROM_B1 --out b1-out/abl/ref
python3 b1-tools/b1run.py --zscan ROM_B1 --out b1-out/abl/zscan --scenes title,sct,field,mountain,greatbay
python3 b1-tools/b1run.py --skip ROOM=1 ROM_B1 --out b1-out/abl/ROOM_1 --scenes ...      # b1-tools/ablate.sh runs all
python3 b1-tools/b1budget.py b1-out/base/run1 b1-out/abl/ref                               # the budget tables
python3 b1-tools/b1seg.py b1-out/abl; python3 b1-tools/b1floor.py b1-out/abl; python3 b1-tools/b1where.py b1-out/base/run1
b1-tools/dplog.sh                                                                        # RSP back-pressure, ARES_DPLOG
```

The `b1-tools` scripts and the patch are in the program store under `reports/B1-tools/`. Apply the patch to a worktree of `11e09bb80`, build, copy the scripts to `b1-tools/` in it, and run the commands above from the worktree root. `b1run.py` wraps `tools/fps60/run.py` without changing it: it patches the B1 config block in the ROM, adds a `bus` step after every field (the runner's RDRAM interface counters per requester) and peeks the task ring after the window. The run outputs stay in `~/repos/mm-wt/B1/b1-out/` until the worktree is removed.

## Budget per scene

**CPU per tick** (measured retail CPU busy over a tick, main ROM; Replay's extra is inferred from A1's constants and A1's measured matrix maximum per tick, Q7 below; filesel runs retail's divisor-1 loop, so Replay adds nothing there). Gap = projected / budget, and projected minus budget in ms.

| scene | d | CPU per tick, retail max / p99 / p50 | + Replay | projected max / p99 | budget | gap at max (ratio, ms) | gap at p99 |
|---|---|---|---|---|---|---|---|
| title | 3 | 15.95 / 15.88 / 12.75 | 0.43 | 16.38 / 16.31 | 45.13 | 0.36x, -28.75 | 0.36x, -28.82 |
| filesel | 1 | 18.72 / 10.19 / 7.10 | 0.00 | 18.72 / 10.19 | 15.04 | 1.24x, +3.68 | 0.68x, -4.85 |
| sct | 3 | 42.65 / 40.15 / 31.03 | 1.76 | 44.42 / 41.91 | 45.13 | 0.98x, -0.71 | 0.93x, -3.22 |
| field | 3 | 36.57 / 36.36 / 31.03 | 1.04 | 37.60 / 37.39 | 45.13 | 0.83x, -7.53 | 0.83x, -7.74 |
| mountain | 3 | 23.80 / 23.78 / 21.74 | 0.70 | 24.50 / 24.48 | 45.13 | 0.54x, -20.63 | 0.54x, -20.65 |
| greatbay | 3 | 38.15 / 37.70 / 36.33 | 2.41 | 40.57 / 40.11 | 45.13 | 0.90x, -4.56 | 0.89x, -5.02 |

**RSP per field** (RSP gfx per image: measured, instrumented ROM; RSP audio per field: measured, main ROM; their sum is the Replay field, one render plus one audio task; summing the two maxima bounds the worst field from above).

| scene | RSP gfx per image max / p99 / p50 | RSP audio per field max / p99 / p50 | RSP per field max / p99 | budget | gap at max | gap at p99 |
|---|---|---|---|---|---|---|
| title | 4.77 / 4.77 / 4.33 | 1.54 / 1.51 / 0.28 | 6.31 / 6.27 | 15.04 | 0.42x, -8.73 | 0.42x, -8.77 |
| filesel | 2.34 / 2.33 / 2.27 | 2.20 / 2.14 / 1.72 | 4.54 / 4.47 | 15.04 | 0.30x, -10.51 | 0.30x, -10.57 |
| sct | 26.22 / 25.69 / 20.10 | 4.50 / 4.18 / 2.79 | 30.73 / 29.87 | 15.04 | 2.04x, +15.68 | 1.99x, +14.82 |
| field | 16.06 / 16.05 / 14.81 | 3.67 / 3.46 / 2.54 | 19.73 / 19.51 | 15.04 | 1.31x, +4.68 | 1.30x, +4.46 |
| mountain | 14.46 / 14.33 / 12.91 | 1.53 / 1.52 / 0.58 | 15.99 / 15.85 | 15.04 | 1.06x, +0.94 | 1.05x, +0.80 |
| greatbay | 21.60 / 21.60 / 20.85 | 3.05 / 2.95 / 2.03 | 24.65 / 24.55 | 15.04 | 1.64x, +9.61 | 1.63x, +9.51 |

**RDP busy per image** (measured, instrumented ROM; the per-tick column is the main ROM's `t_rdp_buf`, which in filesel can span two tasks).

| scene | RDP busy (rdp_buf) per image max / p99 / p50 | cross-check: per tick, main ROM, max / p99 | budget | gap at max | gap at p99 |
|---|---|---|---|---|---|
| title | 26.75 / 26.56 / 19.82 | 26.77 / 26.59 | 15.04 | 1.78x, +11.71 | 1.77x, +11.51 |
| filesel | 16.44 / 15.81 / 15.63 | 18.56 / 17.70 | 15.04 | 1.09x, +1.39 | 1.05x, +0.77 |
| sct | 31.22 / 31.04 / 25.74 | 31.65 / 31.47 | 15.04 | 2.08x, +16.17 | 2.06x, +16.00 |
| field | 23.82 / 23.77 / 19.42 | 23.93 / 23.90 | 15.04 | 1.58x, +8.78 | 1.58x, +8.72 |
| mountain | 25.38 / 25.37 / 24.76 | 25.40 / 25.38 | 15.04 | 1.69x, +10.33 | 1.69x, +10.33 |
| greatbay | 25.15 / 25.14 / 24.78 | 25.56 / 25.56 | 15.04 | 1.67x, +10.10 | 1.67x, +10.09 |

**W_t per image** (RSP start to RDP full sync, measured, instrumented ROM; the first-image overhead `N_m*c_apply + c_start + c_gap` is inferred from A1's constants).

| scene | W_t per image max / p99 / p50 | + first-image overhead | projected max / p99 | budget | gap at max | gap at p99 |
|---|---|---|---|---|---|---|
| title | 26.93 / 26.74 / 20.02 | 0.17 | 27.10 / 26.90 | 15.04 | 1.80x, +12.06 | 1.79x, +11.86 |
| filesel | 16.67 / 16.64 / 16.09 | 0.00 | 16.67 / 16.64 | 15.04 | 1.11x, +1.62 | 1.11x, +1.59 |
| sct | 39.13 / 35.44 / 27.67 | 0.35 | 39.48 / 35.79 | 15.04 | 2.62x, +24.44 | 2.38x, +20.75 |
| field | 24.87 / 24.65 / 21.70 | 0.25 | 25.12 / 24.90 | 15.04 | 1.67x, +10.08 | 1.65x, +9.85 |
| mountain | 25.59 / 25.58 / 24.97 | 0.20 | 25.80 / 25.79 | 15.04 | 1.71x, +10.75 | 1.71x, +10.75 |
| greatbay | 28.98 / 28.76 / 26.75 | 0.43 | 29.41 / 29.19 | 15.04 | 1.96x, +14.37 | 1.94x, +14.15 |

### Reading the budget

- **The RDP is the binding resource in every Play scene, and `W_t` binds the schedule.** RDP busy per image is 1.58x (field) to 2.08x (sct) over budget at max, 8.8 to 16.2 ms. `W_t` is 1.67x to 2.62x over, 10.1 to 24.4 ms. Every P3 unit that does not cut RDP time or `W_t` leaves every Play scene failing.
- **The CPU per tick already fits, barely in sct.** Projected max is 44.42 ms against 45.13 in sct (0.98x, 0.71 ms margin), 40.57 in greatbay (0.90x). The margin is inside the BENCH hook cost (up to about 0.4 ms per tick, H1b, inferred) and the layout sensitivity below (up to 1.66 ms). Tripling RCP work per tick also adds RDRAM contention for the CPU, not measured here. So sct's CPU needs a cut too, even though it passes on paper.
- **The per-field CPU form** (preferences line 23, information only): logic and draw run as one block of 24.5 to 44.4 ms per tick, so the first field of every Play tick is 100% busy (inferred from the per-tick numbers, which all exceed one field).
- **RSP is over in sct (2.04x), greatbay (1.64x) and field (1.31x), and 1.06x in mountain.** Part of RSP gfx is back-pressure: the RSP spins on `DPC_STATUS` or `DPC_CURRENT` when the RDP falls behind. Measured with `ARES_DPLOG` on the main ROM (sum of poll-loop read gaps under 4 us per task): sct 5.40 p50 / 8.22 max ms per task, mountain 5.87 / 7.26, field 3.89 / 4.72, greatbay 4.12 / 4.59, title 0. Without it (inferred, max minus max), RSP per field would be about 22.5 ms in sct, 20.1 in greatbay, 15.0 in field and 8.7 in mountain. So an RDP speedup also lowers RSP busy, and sct and greatbay still need RSP work of their own.
- **filesel** (divisor 1, retail's loop) is 1.11x over on `W_t` and 1.24x on its worst tick of CPU (18.72 ms, an AudioMgr spike of 14.02 ms on one tick), and lags on 87 of 510 ticks today (H1b). It needs its own unit; it is not on the Play path.

## Where the time goes

### CPU (main ROM, measured)

CPU per tick by thread, p99 / max ms:

| scene | graph | audio | sched | irqmgr | padmgr | dmamgr |
|---|---|---|---|---|---|---|
| title | 6.35 / 6.40 | 7.06 / 7.13 | 0.84 / 0.85 | 0.14 / 0.14 | 1.29 / 1.42 | 0.12 / 0.12 |
| filesel | 4.23 / 4.27 | 4.56 / 14.02 | 0.83 / 0.84 | 0.10 / 0.11 | 0.56 / 0.58 | 0.00 / 0.00 |
| sct | 21.02 / 21.21 | 16.67 / 19.00 | 1.11 / 1.12 | 0.20 / 0.20 | 1.11 / 1.11 | 0.32 / 0.33 |
| field | 24.71 / 25.42 | 11.89 / 12.29 | 0.95 / 0.95 | 0.17 / 0.17 | 0.91 / 0.99 | 0.18 / 0.33 |
| mountain | 15.27 / 15.27 | 6.28 / 6.48 | 0.86 / 0.87 | 0.15 / 0.15 | 0.89 / 0.89 | 0.10 / 0.14 |
| greatbay | 26.83 / 26.99 | 9.36 / 9.40 | 0.98 / 0.98 | 0.17 / 0.17 | 0.92 / 0.95 | 0.10 / 0.14 |

Graph thread CPU per tick by `Graph_Update` phase, p99 / max ms (`update` is `Play_Update`, `draw` is `Play_Draw`; `C_up` = pre + update + game, `C_dr` = draw + post in Q7):

| scene | pre | update | draw | game | post | rcp_wait | tail |
|---|---|---|---|---|---|---|---|
| title | 0.44 / 0.44 | 2.58 / 2.61 | 3.15 / 3.20 | 0.16 / 0.16 | 0.04 / 0.04 | 0.06 / 0.06 | 0.14 / 0.15 |
| filesel | 0.39 / 0.41 | 0.00 / 0.00 | 0.00 / 0.00 | 3.71 / 3.75 | 0.06 / 0.12 | 0.07 / 0.10 | 0.17 / 0.17 |
| sct | 0.64 / 0.65 | 10.52 / 10.55 | 10.00 / 10.12 | 0.26 / 0.31 | 0.06 / 0.15 | 0.07 / 0.08 | 0.17 / 0.17 |
| field | 0.62 / 0.62 | 15.38 / 16.40 | 8.17 / 8.46 | 0.22 / 0.24 | 0.05 / 0.06 | 0.06 / 0.06 | 0.16 / 0.17 |
| mountain | 0.36 / 0.37 | 9.03 / 9.04 | 5.66 / 5.69 | 0.19 / 0.26 | 0.05 / 0.05 | 0.04 / 0.04 | 0.12 / 0.13 |
| greatbay | 0.43 / 0.44 | 11.50 / 11.56 | 14.63 / 14.63 | 0.24 / 0.24 | 0.06 / 0.06 | 0.04 / 0.04 | 0.12 / 0.12 |

Graph thread CPU per tick by engine system, p99 / max ms (exclusive time, the innermost bracket gets it):

| scene | actor_draw | actor_update | camera | message | other | effect | environment | audio_update | room_draw | collision | bench | skybox |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| title | 2.11 / 2.11 | 1.36 / 1.37 | 0.49 / 0.50 | 0.21 / 0.22 | 1.23 / 1.23 | 0.45 / 0.45 | 0.28 / 0.28 | 0.31 / 0.31 | 0.18 / 0.18 | 0.11 / 0.11 | 0.11 / 0.12 | 0.10 / 0.11 |
| filesel | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 3.97 / 3.99 | 0.00 / 0.00 | 0.00 / 0.00 | 0.25 / 0.25 | 0.00 / 0.00 | 0.00 / 0.00 | 0.14 / 0.16 | 0.00 / 0.00 |
| sct | 7.34 / 7.47 | 7.16 / 7.42 | 2.50 / 2.58 | 1.99 / 2.08 | 1.28 / 1.31 | 0.89 / 0.90 | 0.44 / 0.48 | 0.44 / 0.47 | 0.38 / 0.38 | 0.29 / 0.29 | 0.08 / 0.08 | 0.08 / 0.11 |
| field | 5.15 / 5.41 | 12.12 / 12.43 | 2.55 / 2.57 | 1.66 / 1.73 | 1.25 / 1.26 | 0.99 / 1.00 | 0.48 / 0.49 | 0.48 / 0.49 | 0.64 / 0.64 | 1.23 / 1.23 | 0.08 / 0.11 | 0.17 / 0.17 |
| mountain | 3.26 / 3.27 | 5.93 / 5.94 | 2.25 / 2.26 | 1.63 / 1.64 | 1.12 / 1.14 | 0.37 / 0.37 | 0.37 / 0.38 | 0.24 / 0.26 | 0.51 / 0.51 | 0.26 / 0.26 | 0.06 / 0.07 | 0.14 / 0.14 |
| greatbay | 11.64 / 11.65 | 8.96 / 9.00 | 1.49 / 1.63 | 1.66 / 1.69 | 1.12 / 1.15 | 0.95 / 0.95 | 0.36 / 0.36 | 0.31 / 0.32 | 0.51 / 0.51 | 0.29 / 0.29 | 0.06 / 0.06 | 0.07 / 0.07 |

Top 10 actors by update plus draw, ms per tick averaged over the window (update / draw):

- **title**: PLAYER 0.68 / 1.57; EN_MAG 0.01 / 0.24; DM_CHAR05 0.07 / 0.16; EN_OSN 0.13 / 0.00
- **sct**: PLAYER 1.48 / 2.19; EN_TEST3 1.13 / 0.00; EN_DAIKU 0.45 / 0.67; EN_DG 0.69 / 0.36; EN_STH 0.17 / 0.28; OBJ_TOKEIDAI 0.06 / 0.38; EN_ELF 0.14 / 0.26; EN_SELLNUTS 0.19 / 0.00; OBJ_ENTOTU 0.01 / 0.15; EN_PST 0.03 / 0.12
- **field**: PLAYER 1.44 / 2.08; EN_RUPPECROW 0.68 / 0.30; EN_INVISIBLE_RUPPE 0.57 / 0.00; OBJ_GRASS 0.33 / 0.12; EN_ELF 0.14 / 0.24; EN_ANI 0.27 / 0.00; OBJ_TOKEIDAI 0.06 / 0.18; EN_FALL 0.06 / 0.19; EN_NEO_REEBA 0.08 / 0.02; OBJ_ICE_POLY 0.10 / 0.00
- **mountain**: PLAYER 1.32 / 2.15; EN_GEG 0.80 / 0.00; EN_GO 0.70 / 0.00; OBJECT_KANKYO 0.29 / 0.31; EN_ELF 0.14 / 0.26; EN_KAKASI 0.31 / 0.00; EN_KANBAN 0.11 / 0.00; OBJ_DRIFTICE 0.05 / 0.00; EN_DOOR 0.03 / 0.00; EN_TEST4 0.03 / 0.00
- **greatbay**: EN_TANRON4 1.68 / 6.55; PLAYER 1.22 / 1.94; EN_ZOW 0.65 / 0.00; EN_ELF 0.15 / 0.25; EN_KAKASI 0.31 / 0.00; OBJ_TSUBO 0.04 / 0.19; EN_KANBAN 0.17 / 0.00; OBJ_YASI 0.01 / 0.15; EN_FALL 0.02 / 0.11; EN_HORSE 0.05 / 0.07

Reading (inferred from the tables): in sct the AudioMgr thread (audio synthesis and sequence processing) is the second-largest CPU consumer at 16.67 ms p99 per tick, 40% of the tick, more than `Play_Update`. Actor update and actor draw are the largest graph-thread systems everywhere, and greatbay's `EN_TANRON4` (the seagull flock) alone draws 6.55 ms per tick. `Camera_Update` costs 2.25 to 2.55 ms per tick in the walking scenes.

### RSP and RDP by display-list segment (instrumented ROM, measured)

**Method.** `Play_DrawMain` and `Actor_DrawAll` mark the write pointers of `POLY_OPA`, `POLY_XLU` and `OVERLAY` at segment boundaries: `SKY` (the skybox and `Environment_Draw` before the room), `ROOM` (lights, `Scene_Draw`, `Room_Draw`), `SKY2_RAIN`, `ACTORS` (`Actor_DrawAll` without effects), `EFFECTS` (`Effect_DrawAll`, `EffectSs_DrawAll`, `EffFootmark_Draw`), `LENS_FILL` (sun and custom lens flares, screen fills, sandstorm, motion blur), `HUD` (`Play_PostWorldDraw`: interface and messages), `STARS` (`Environment_DrawSkyboxStars`) and `CLEAR` (the Z and color clears of `func_8012CF0C`). A variant run (the same instrumented ROM with a skip mask patched into its B1 config block) skips one segment on the RCP only: after the tick's draw, the first command of the segment's range in the chosen buffers is overwritten with `gSPBranchList` to the range's end, so the CPU does all of retail's work and the RSP and RDP skip that part. The drop in per-image time is that segment's cost. Buffer masks: 1 `POLY_OPA`, 2 `POLY_XLU`, 4 `OVERLAY`, 7 all.

**Error.** Skipping a segment can change later work: an opaque segment that is not drawn changes which later pixels pass the Z test, and state a segment leaves behind is not set. The closure check bounds it: the sum of the eight single-segment drops against the drop when all eight are skipped together differs by 0.02 (title), 0.85 (sct), 0.20 (field), 0.06 (mountain) and 1.57 ms (greatbay) at p50, at most 6.3% of the drop. The residual with everything skipped (view setup, actor list scaffolding, task overhead) is 1.23 to 1.41 ms. `CLEAR` is not a clean measure: without the Z clear the world compares against the last image's Z and most of its writes fail, so its 3.7 to 5.7 ms drop is mostly that interaction. The clears themselves are 2.02 full-screen fills per Play image in the Play scenes (2.29 in title) from `dp_fill` bytes, 0.62 ms at FILL's 4 px/clk (SDK 12.1.4, inferred).

Drop in RDP busy per image when the segment is skipped, p50 / max ms (first row: the reference):

| variant | title | sct | field | mountain | greatbay |
|---|---|---|---|---|---|
| ref | 19.82 / 26.75 | 25.74 / 31.22 | 19.42 / 23.82 | 24.76 / 25.38 | 24.78 / 25.15 |
| SKY_7 | 4.88 / 4.97 | 4.20 / 3.84 | 3.90 / 4.23 | 7.00 / 6.85 | 4.78 / 4.64 |
| ROOM_1 | 0.00 / 0.03 | 11.52 / 12.08 | 8.68 / 12.48 | 10.83 / 10.87 | 11.20 / 11.42 |
| ROOM_2 | -0.00 / 0.01 | 0.28 / 0.53 | 0.16 / 0.30 | 1.19 / 1.13 | 0.56 / 0.52 |
| ACTORS_1 | 2.20 / 4.61 | 6.53 / 6.59 | 3.28 / 3.08 | 2.20 / 2.17 | 6.30 / 6.47 |
| ACTORS_2 | -0.00 / 0.03 | 0.59 / 0.57 | 0.32 / 0.40 | 0.41 / 0.30 | 0.31 / 0.31 |
| ACTORS_4 | 2.45 / 2.47 | -0.11 / 0.09 | 0.00 / 0.00 | 0.00 / 0.00 | -0.02 / -0.05 |
| EFFECTS_7 | 0.02 / 0.06 | -0.05 / 0.14 | 0.03 / 0.06 | 0.02 / -0.00 | 0.09 / 0.02 |
| LENS_FILL_7 | 8.84 / 8.81 | 0.08 / -0.01 | 0.01 / -0.01 | 0.00 / 0.00 | 0.01 / -0.12 |
| HUD_7 | -0.00 / 0.03 | 1.97 / 1.98 | 1.75 / 1.68 | 1.76 / 1.91 | 2.02 / 1.90 |
| SKY2_RAIN_7 | 0.00 / 0.00 | -0.06 / -0.04 | 0.00 / 0.00 | 0.00 / 0.00 | -0.00 / -0.04 |
| STARS_7 | 0.00 / 0.00 | 0.18 / 0.21 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / -0.02 |
| CLEAR_7 | 1.17 / 1.34 | 4.70 / 5.73 | 3.71 / 4.70 | 4.68 / 4.67 | 4.51 / 4.56 |
| ROOM_7 | -0.00 / 0.03 | 11.93 / 12.44 | 9.04 / 12.54 | 12.02 / 12.03 | 11.65 / 11.88 |
| ACTORS_7 | 4.69 / 7.08 | 7.02 / 7.14 | 3.65 / 3.49 | 2.60 / 2.46 | 6.57 / 6.73 |
| ALL | 18.41 / 25.27 | 24.43 / 29.83 | 18.19 / 22.46 | 23.46 / 24.01 | 23.54 / 23.78 |

Drop in RSP gfx per image, p50 / max ms:

| variant | title | sct | field | mountain | greatbay |
|---|---|---|---|---|---|
| ref | 4.33 / 4.77 | 20.10 / 26.22 | 14.81 / 16.06 | 12.91 / 14.46 | 20.85 / 21.60 |
| SKY_7 | 0.38 / 0.40 | 2.56 / 1.83 | 3.12 / 2.75 | 6.40 / 6.48 | 4.30 / 4.40 |
| ROOM_1 | 0.01 / 0.01 | 7.69 / 9.60 | 8.25 / 8.90 | 7.89 / 9.31 | 9.52 / 9.80 |
| ROOM_2 | 0.01 / 0.01 | 0.43 / 0.99 | 0.24 / 0.25 | 0.12 / 0.23 | 0.87 / 1.06 |
| ACTORS_1 | 2.96 / 3.35 | 14.40 / 18.55 | 8.75 / 8.29 | 8.93 / 10.36 | 12.63 / 12.98 |
| ACTORS_2 | 0.02 / 0.02 | 0.38 / 0.67 | 0.26 / 0.23 | 0.39 / 1.48 | 0.38 / 0.37 |
| ACTORS_4 | 0.15 / 0.15 | -0.02 / 0.15 | 0.00 / 0.00 | 0.00 / 0.00 | -0.00 / 0.00 |
| EFFECTS_7 | 0.02 / 0.01 | 0.04 / 0.31 | 0.02 / 0.12 | 0.03 / 0.12 | 0.07 / 0.16 |
| LENS_FILL_7 | 0.29 / 0.30 | 0.00 / 0.30 | 0.01 / -0.02 | 0.00 / 0.00 | -0.01 / -0.03 |
| HUD_7 | 0.00 / 0.00 | 0.49 / 0.81 | 0.44 / 0.38 | 0.75 / 1.91 | 0.64 / 0.90 |
| SKY2_RAIN_7 | 0.00 / 0.00 | 0.03 / 0.07 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 |
| STARS_7 | 0.00 / 0.00 | 0.01 / 0.13 | 0.00 / 0.00 | 0.00 / 0.00 | -0.00 / 0.00 |
| CLEAR_7 | 0.12 / 0.07 | 2.80 / 2.08 | 2.48 / 2.21 | 2.47 / 3.12 | 3.14 / 3.39 |
| ROOM_7 | 0.02 / 0.02 | 8.14 / 9.99 | 8.46 / 9.06 | 8.06 / 9.48 | 10.26 / 10.56 |
| ACTORS_7 | 3.11 / 3.51 | 14.78 / 19.05 | 8.99 / 8.51 | 9.25 / 10.58 | 12.97 / 13.36 |
| ALL | 4.01 / 4.44 | 19.76 / 25.87 | 14.49 / 15.71 | 12.57 / 14.11 | 20.53 / 21.26 |

Drop in `W_t` per image, p50 / max ms:

| variant | title | sct | field | mountain | greatbay |
|---|---|---|---|---|---|
| ref | 20.02 / 26.93 | 27.67 / 39.13 | 21.70 / 24.87 | 24.97 / 25.59 | 26.75 / 28.98 |
| SKY_7 | 3.73 / 2.74 | 3.33 / 4.61 | 3.89 / 3.36 | 6.90 / 6.80 | 4.15 / 4.61 |
| ROOM_1 | 0.00 / 0.03 | 10.94 / 12.94 | 9.73 / 11.88 | 10.84 / 10.89 | 11.67 / 12.97 |
| ROOM_2 | 0.00 / 0.01 | 0.70 / 0.08 | 0.44 / 0.32 | 1.19 / 1.13 | 0.71 / 0.89 |
| ACTORS_1 | 2.19 / 4.61 | 7.21 / 12.06 | 4.08 / 2.98 | 2.20 / 2.17 | 8.02 / 9.28 |
| ACTORS_2 | 0.00 / 0.03 | 0.68 / 4.03 | 0.49 / 0.41 | 0.41 / 0.31 | 0.36 / 0.45 |
| ACTORS_4 | 2.45 / 2.47 | 0.03 / -0.17 | 0.00 / 0.00 | 0.00 / 0.00 | -0.16 / 0.01 |
| EFFECTS_7 | 0.03 / 0.06 | 0.04 / 3.79 | 0.11 / 0.05 | 0.02 / -0.00 | -0.01 / 0.07 |
| LENS_FILL_7 | 8.87 / 8.76 | 0.00 / -0.61 | 0.04 / -0.01 | 0.00 / 0.00 | -0.07 / 0.16 |
| HUD_7 | 0.00 / 0.03 | 2.10 / 5.16 | 1.80 / 1.69 | 1.76 / 1.93 | 1.90 / 2.05 |
| SKY2_RAIN_7 | 0.00 / 0.00 | -0.01 / -0.07 | 0.00 / 0.00 | 0.00 / 0.00 | -0.09 / 0.03 |
| STARS_7 | 0.00 / 0.00 | -0.01 / -0.76 | 0.00 / 0.00 | 0.00 / 0.00 | -0.02 / 0.03 |
| CLEAR_7 | 1.20 / 1.37 | 3.55 / 5.61 | 3.58 / 3.58 | 4.71 / 4.70 | 3.08 / 3.25 |
| ROOM_7 | 0.00 / 0.03 | 11.41 / 14.03 | 10.04 / 11.91 | 12.03 / 12.05 | 12.42 / 13.71 |
| ACTORS_7 | 4.71 / 7.08 | 7.84 / 12.45 | 4.49 / 3.38 | 2.60 / 2.46 | 8.28 / 9.55 |
| ALL | 18.29 / 24.87 | 24.35 / 34.36 | 18.57 / 20.67 | 23.31 / 23.53 | 24.20 / 25.39 |

RDP RDRAM traffic removed by the skip, KiB per task (window mean of the runner's `bus` counters: color read / write, depth read / write, texture read):

| variant | title | sct | field | mountain | greatbay |
|---|---|---|---|---|---|
| SKY_7 | 113/224, 0/0, 50 | 0/151, 0/0, 88 | 2/137, 1/1, 87 | 151/302, 0/0, 86 | 0/150, 0/0, 86 |
| ROOM_1 | 0/0, 0/0, 0 | 232/172, 232/175, 90 | 215/155, 215/150, 78 | 260/193, 260/193, 21 | 219/151, 219/157, 127 |
| ROOM_2 | 0/0, 0/0, 0 | 8/2, 8/1, 5 | 4/2, 4/0, 5 | 42/40, 42/0, 3 | 9/0, 9/0, 8 |
| ACTORS_1 | 47/18, 47/18, 60 | 40/12, 40/12, 214 | 22/7, 21/6, 90 | 20/5, 20/5, 35 | 27/6, 27/5, 221 |
| ACTORS_2 | 0/0, 0/0, 0 | 3/1, 2/0, 26 | 0/0, 0/0, 25 | 1/1, 1/0, 23 | 0/0, 0/0, 18 |
| ACTORS_4 | 108/56, 0/0, 40 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 |
| EFFECTS_7 | 0/0, 0/0, 2 | 0/0, 0/0, 2 | 0/0, 0/0, 2 | 0/0, 0/0, 2 | 1/1, 1/0, 2 |
| LENS_FILL_7 | 162/313, 0/0, 366 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 |
| HUD_7 | 0/0, 0/0, 0 | 74/30, 0/0, 27 | 69/26, 0/0, 27 | 74/29, 0/0, 27 | 69/25, 0/0, 27 |
| SKY2_RAIN_7 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 |
| STARS_7 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 | 0/0, 0/0, 0 |
| CLEAR_7 | 0/11, 0/10, 0 | 2/192, 0/188, -0 | 0/162, 0/156, 0 | 0/239, 0/198, 0 | 0/165, 0/164, 0 |
| ROOM_7 | 0/0, 0/0, 0 | 240/178, 240/175, 95 | 218/157, 218/150, 83 | 302/233, 302/193, 23 | 228/158, 228/157, 135 |
| ACTORS_7 | 155/74, 47/18, 100 | 43/14, 42/12, 240 | 22/7, 22/6, 115 | 22/6, 22/5, 58 | 27/6, 27/5, 239 |
| ALL | 426/606, 47/18, 514 | 358/373, 282/188, 452 | 308/324, 239/156, 312 | 549/571, 324/198, 196 | 325/340, 256/164, 489 |

Reading (measured numbers, inferred conclusions):

- **Opaque room geometry is the largest RDP cost in every outdoor scene**: 11.5 ms of sct's 25.7 ms p50 (45%), 8.7 of 19.4 in field, 10.8 of 24.8 in mountain, 11.2 of 24.8 in greatbay. It Z-tests about 116,000 pixels per image in sct (232 KiB of depth reads) and writes about 88,000 (172 KiB of color writes). That is 6.1 RDP clocks per Z-tested pixel, against 3.95 clocks for the same render mode on hardware in large rectangles (Thar0, cited in R1 appendix D). The extra is per-primitive and per-load overhead, not fill.
- **Opaque actors** cost 2.2 to 6.5 ms of RDP but only write 2,500 to 9,000 pixels (5 to 18 KiB), while reading 35 to 221 KiB of texture per image. Actor RDP time is texture loads and primitive overhead, not pixels. Actors are also the largest RSP cost: 14.4 of sct's 20.1 ms RSP gfx p50, 12.6 of greatbay's 20.9.
- **The skybox** costs 3.9 to 7.0 ms of RDP and up to 6.4 ms of RSP. It draws one full screen of 2-cycle, two-texture bilinear CI8 pixels (151 KiB of color writes; two layers, 302 KiB, in mountain and title) and loads 86 KiB of texture per image through 32x32 `LoadTile` tiles, two per quad (`z_vr_box.c:160-210`, cited). Its 2-cycle fill floor is 2.46 ms per layer (SDK 12.1.3, inferred).
- **The HUD** (`Interface_Draw`, `Message_Draw`) costs 1.75 to 2.0 ms of RDP for about 15,000 written pixels; R1 lists `G_PM_1PRIMITIVE` in `z_parameter.c` at 30 to 40 dead clocks per primitive (SDK 12.2.3, cited).
- **title** is different: its `LENS_FILL` segment costs 8.8 ms (313 KiB of color writes and 366 KiB of texture reads per image, about 2.4 framebuffers, so probably the motion-blur pass; inferred, not checked by name), and the `EN_MAG` logo in `OVERLAY` costs 2.5 ms.
- **Translucent geometry, effects, rain and stars are small** in this corpus: at most 1.2 ms (mountain's room translucency).

### RDRAM traffic per image (instrumented ROM, measured)

Per task, the RDP's own RDRAM requesters hold the RDRAM interface for 8.7 ms in sct (command 0.75, color 3.48, depth 2.45, texture 1.31, fill 0.69) and wait for it 5.4 ms, while the CPU holds it 10.1 ms and the VI 3.8 ms per tick (the runner's `bus` counters, window mean). In mountain the RDP holds 8.8 ms and waits 3.7 ms. So about half of RDP busy is RDRAM time, a third of which is waiting on other requesters. The wait split depends on the runner's RDRAM arbitration model, whose rows the fork marks partly as model choices; a hypothesis that targets it needs a second, cited mechanism (preferences line 5).

## RDP lower bounds under pixel-identical rules

The floor counts the work that no pixel-identical change can remove while retail's render modes and draw order stay: the clears, every sky pixel retail writes (it lies under antialiased edges, so no identical change can skip it without proof), every final opaque world pixel once (the Z coverage: pixels whose Z differs from the clear value `0xFFFC` after the task, scanned by the instrumented ROM on every task), every translucent pixel once (its result depends on what is under it), and the HUD and overlay pixels. It assumes all overdraw that fails or is later covered could be removed, and it charges each pixel the best hardware rate for its mode, measured on large rectangles: FILL 4 px/clk and 2-cycle 2 clk/px (SDK 12.1.3, 12.1.4), the MM opaque world mode with Z pass, VI on and separate banks 3.95 clk/px (Thar0 RDP-Timing-Tests `sample_results.txt` L307), a Z read with `IM_RD` and no write 2.45 clk/px (L245), 1-cycle 1 clk/px (SDK 12.1.2). So it is a lower bound, optimistic by the small-triangle factor that R1 could not find; the measured room cost above (6.1 against 3.95 clk/px) suggests about 1.5x on the world term.

| scene | clears | sky, 2-cycle | opaque world, Z coverage max x 3.95 clk | translucent | overlay and HUD | lens, fill, motion blur | floor | budget | floor / budget | retail RDP max |
|---|---|---|---|---|---|---|---|---|---|---|
| title | 0.70 | 3.67 | 3.56 (56,320 px) | 0.00 | 0.46 | 2.56 | 10.95 | 15.04 | 0.73 | 26.75 |
| sct | 0.62 | 2.48 | 4.85 (76,800 px) | 0.07 | 0.25 | 0.00 | 8.27 | 15.04 | 0.55 | 31.22 |
| field | 0.62 | 2.24 | 4.85 (76,800 px) | 0.05 | 0.21 | 0.00 | 7.97 | 15.04 | 0.53 | 23.82 |
| mountain | 0.62 | 4.95 | 4.73 (74,893 px) | 0.82 | 0.24 | 0.00 | 11.36 | 15.04 | 0.76 | 25.38 |
| greatbay | 0.62 | 2.46 | 4.72 (74,635 px) | 0.02 | 0.21 | 0.00 | 8.02 | 15.04 | 0.53 | 25.15 |
| filesel | 0.31 | | | | 3.91 (all 476 KiB of color writes at 1 clk/px) | | 4.22 | 15.04 | 0.28 | 16.44 |

(inferred from measured pixel counts and cited per-pixel costs; filesel is not split, so all its color writes are charged at 1-cycle).

- **No scene's floor exceeds the budget, so B1 raises no RDP gate.** Retail spends 2.2x (mountain) to 3.8x (sct) its floor.
- **Mountain is the scene at risk.** Its floor is 0.76 of the budget, and with the 1.5x small-triangle factor on its world term it is about 14.0 ms, 0.93 of the budget (inferred). Its two sky layers (4.95 ms at the 2-cycle floor) are the reason. Reaching budget in mountain means removing nearly all per-primitive and load overhead.
- The Z coverage scan itself perturbed timing: with it on, sct's RDP busy rose from 25.74 to 27.08 ms p50 (measured). The scan runs on the CPU right after each task, so it moves CPU RDRAM traffic against the next task. Floors use only its pixel counts, which do not depend on timing.

## Effect budget: can a shipping build estimate retail's RCP time?

**No, not well enough to keep counts console-like.** The question (gates-notes.md, gate G4-effect-budget) is whether a shipping build can compute, for each tick, an estimate of the RCP time retail would have taken, so `func_80173B48` (`src/code/game.c:300-303`, cited) keeps retail's rain and dust counts while the build itself renders faster.

- The counts are sensitive to fractions of a millisecond. Rain scales with `budget / 30 ms` truncated to an integer count (`src/code/z_kankyo.c:2080-2085`), and Link's dust steps when the budget crosses multiples of 12 or 20 ms (`ovl_player_actor/z_player.c:10320`, `10336`, `10346`), with budget = `3 x T_f - gRDPTimeTotal` (cited code, inferred thresholds).
- Retail's `W_t` is measurable per tick in a BENCH build (this unit's ring), so an estimator can be fitted and checked. The cheapest per-tick signal a shipping build has is its own display list. A least-squares fit of `W_t` on the per-segment command counts of `POLY_OPA`, `POLY_XLU` and `OVERLAY` (15 non-constant features) over the 990 Play tasks leaves a residual of 1.47 ms RMS, 6.52 ms worst, on the data it was fitted to. Fitted on four scenes and tested on the fifth, its RMS error is 7.2 to 453 ms (measured, `b1-tools/b1fit.py`). The time depends on pixels, texture loads and RDRAM contention, which the command count does not see, and a better estimator would need the renderer's own work.
- Retail's own count is not a fixed value either: on a console the RCP time moves with CPU and RDRAM contention and the noise generator's phase (preferences line 20), so "console-like" means within a console's run-to-run spread. That spread is not measured.
- So the measurable statement is narrower: a shipping estimator can be checked against the BENCH ring, and the simplest one misses by more than a rain step. Gate default stands as the gate defines it.

## CPU layout sensitivity

Adding the B1 instrumentation (about 220 lines of BENCH code in `bench.c`, `sched.c`, `graph.c`, `z_play.c`, `z_actor.c`, none on the per-actor path) changed CPU per tick in the Play scenes by -4.3% to +3.2% with identical game logic (greatbay max 38.15 to 36.49 ms and p50 36.33 to 34.84 ms, sct p99 40.15 to 41.43 ms, measured, same ticks and inputs). RSP and RDP per tick moved by up to 1.9% with it. The code that moved is the code after the edits in link order, which shifts every later function's I-cache index. The VR4300 I-cache is 16 KiB, direct-mapped (VR4300 manual 11.2.1, cited in R1). So code placement alone is worth at least about 1.5 ms per tick in greatbay (1.66 at max, 1.49 at p50), in either direction (inferred), and every CPU hypothesis has to be judged against a same-layout control or against several layouts.

## Q7 corrections

[architecture.md Q7](architecture.md#q7-load-projection) now carries the measured inputs, the corrected worked numbers and the sources for each symbol. The main corrections: the projected CPU extra at 187 matrices is 1.30 ms for the graph thread and 2.23 ms with the scheduler's apply (verify-7), not 1.7 ms; RSP gfx alone is 1.07x to 1.74x over budget in the outdoor scenes measured, not 1.2x to 1.9x; RDP busy is measured and binds every Play scene.

## Backlog

`~/.claude/orchestrate/mm-60fps/backlog.tsv` holds one row per single-mechanism hypothesis, ordered by expected gain on the binding resource per scene (RDP and `W_t` first). Each row names its hardware reference, target scene and metric, and labels its gain measured, cited or inferred.

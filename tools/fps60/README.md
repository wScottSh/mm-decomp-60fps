# fps60 harness

The fps60 harness measures how a `BENCH=1` build of this tree presents images and runs its logic ticks. It runs each scene of a fixed corpus on the pinned ares runner and writes per-field data, the image each logic tick ends on, and summaries. `fidelity.py` compares two runs tick by tick (check F1). The program's rules are in `~/.claude/orchestrate/mm-60fps/preferences.md`. After unit H1 lands, `tools/fps60/` is frozen (preferences line 7).

## Run

```sh
tools/fps60/worktree-setup.sh UNIT BRANCH [BASE_REF]   # prints ~/repos/mm-wt/UNIT, ready to build
make -j32 BENCH=1                                       # build/n64-us-bench/mm-n64-us.z64 and .map
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --check-determinism
tools/fps60/fidelity.py BASE_RESULTS CAND_RESULTS
```

- `worktree-setup.sh` creates the worktree (or reuses it), copies the untracked build inputs from `~/repos/mm-decomp-60fps` (baseroms, `extracted/`, the IDO recompilation, `.venv`, `.make_options.mk`), and builds `tools/`. It only reads the main checkout. It copies rather than hard-links, so `make setup` in a worktree cannot write into the main checkout. A worktree takes 723 MB before its first build and 1.19 GB after a `BENCH=1` build. Setup took 7 s on a new worktree and 1 s on a rerun (measured on unicron).
- `run.py` reads symbols from the map beside the ROM (`--map` overrides). It checks that the runner's sha256 starts with `1f9d59f6` (set `FPS60_ANY_RUNNER=1` only for experiments). It runs at most 4 runner processes (`--jobs`, preferences line 14), and `--scenes title,sct` selects scenes.
- A full corpus run took 149 s of wall time with 4 jobs, and each scene took 41 to 102 s (measured on unicron, see `docs/fps60/baseline.md`).
- `run.py --check-determinism` runs the corpus into `RESULTS/run1` and `RESULTS/run2` and fails unless every file except `wall.tsv` and `host.txt` is byte-identical.
- `fidelity.py` exits 0 only when every scene passes. On a failure it runs that scene again from both ROMs, dumps both frames, and writes the ares `framediff.py` report under `CAND_RESULTS/fidelity/<scene>/`. The ROM that a result directory names in `meta.json` must still have the recorded sha256.

`run.py` fails loudly when the runner exits nonzero, the stop reason is not `script-stop`, the ROM boots a scene other than the one asked for, the window mark is missing, a peek is missing or unreadable, the stats file is short, or the VI origin does not belong to the framebuffer that the harness attributes to it. It prints the fields, images, and ticks it measured per scene and in total.

## Scenes

`corpus.py` defines the corpus as data. Each scene boots one `BENCH` scene (`sBenchScenes` in `src/code/bench.c`), waits until it settles, and measures a window of 600 fields.

| Scene | Boot | Settles when | Input in the window |
|---|---|---|---|
| `title` | `retail_boot`: the retail route, console logo then the title | the title Play scene has faded in | none |
| `filesel` | `file_select_named`: File 1 and File 2 named "A", cursor on File 1 | the main screen is idle (`CM_MAIN_MENU`) | none |
| `sct` | South Clock Town, day 1 12:00, human Link | the fade-in is done | stick up for 20 ticks, then a circle every 60 ticks |
| `field` | Termina Field | the fade-in and the entrance cutscene are done | stick up |
| `mountain` | Mountain Village (winter) | the fade-in is done | none |
| `greatbay` | Great Bay Coast | the fade-in and the entrance cutscene are done | none |

The routes are mmbench's (`tools/n64-timing/mmbench` in the ares fork) moved to ticks. `field` keeps the stick up because mmbench's turn runs Link into a wall after the entrance cutscene. Neither moving route opens a textbox (checked on frame dumps at window fields 5, 300 and 599).

## How the harness reads the game

A displayed image is a change of `VI_ORIGIN`. To key images by logic tick, the `BENCH` build publishes `gBenchProbe` (`include/bench.h`). `run.py` reads it between fields with the runner's `peek`. A peek logged at `frame=N` reads the state after N fields, so it belongs to stats row N-1.

- `tick` counts logic ticks since boot. `Bench_BeginFrame` increments it at the start of each `Graph_Update`.
- `Bench_TagCfb` (`graph.c`) stores the current tick in `cfbTick[i]` when a task takes `sGraphCfbInfos[i]`. `gScheduler.curBuf` names the `CfbInfo` the VI shows, so `cfbTick[curBuf index]` is the tick of the displayed image. `run.py` checks that `VI_ORIGIN` minus `curBuf->framebuffer` is the same in every window field.
- `settled` and `settleTick` mark the end of the scene's entry. The window starts with the first field after the field in which `settled` became nonzero.
- Input does not come from the runner. `run.py` patches the scene's inputs into `gBenchConfig.inputs`, and `Bench_ApplyInput` (`game.c`, `GameState_GetInput`) writes them into controller 1 from the first tick after `settleTick`. It derives `prev`, `press` and `rel` as `PadMgr_GetInputNoLock` does. An input therefore reaches the same tick in every build, however long ticks take. A runner script cannot guarantee that, because the next tick's `GameState_GetInput` can run within the field in which the previous tick ended.

### Keyframes in a 60 fps build

A retail-architecture build presents one image per logic tick, so every image is a keyframe. A build that presents interpolated images defines `u32 gBenchCfbKeyframe[3]`, parallel to `cfbTick`. It holds nonzero for a `CfbInfo` whose image is its tick's final image and zero for an interpolated image. When the map has that symbol, `run.py` peeks it every field and marks only those images as keyframes. Ticks and tick lengths still come from `cfbTick`.

## Output

| File | Contents |
|---|---|
| `meta.json` | ROM, map and runner paths and sha256s, the `gBenchProbe` address, whether the keyframe symbol exists, the scenes. |
| `summary.tsv` | One row per scene: the columns of `summary.json` below, flattened. |
| `wall.tsv` | Host wall time per scene and in total, the job count, and the 1-minute load average before and after each scene. Not deterministic. |
| `<scene>/fields.tsv` | One row per window field. Columns are listed below. |
| `<scene>/keyframes.tsv` | The per-tick image sequence: one row per complete keyframe image, with `image_tick`, `first_field`, `fields` shown, `final_frame` (stats row of its last field), `fb_hash` at its last field, and `hash_changes` (times `fb_hash` changed while it was shown, 0 when the framebuffer was not written while displayed). |
| `<scene>/ticks.tsv` | One row per complete tick: `tick`, `first_field` its first image appears, `fields` until the next tick's first image, `divisor`, and `skipped_after` (ticks after it that never appeared). |
| `<scene>/summary.json` | `fields`, `images`, `fields_per_image` (counts at 1 to 5 and 6 or more), `image_share_at_1`, `image_fields_max`, `ticks`, `fields_per_tick`, `lag_ticks`, `skipped_ticks`, `tick_fields_max`, `divisors`, `keyframes`, `first_tick`, `last_tick`, `window_start_frame`, `settle_tick`, `retrace_ticks`. |
| `<scene>/stats.tsv` | The runner's per-field stats for the whole run (the runner's README, Per-field stats). |
| `<scene>/script.txt`, `events.txt` | The generated runner script, and its mark and peek lines. |
| `<scene>/guest.txt` | The ROM's IS-Viewer output (the `BENCH START` line). |
| `<scene>/host.txt` | The runner's stop line and host-time lines. Not deterministic. |

`fields.tsv` columns:

| Column | Meaning |
|---|---|
| `field` | Window field, 0 to 599. |
| `frame` | Stats row (runner field index). |
| `origin`, `fb_hash` | From `stats.tsv`: `VI_ORIGIN` and the hash of the displayed pixels. |
| `tick` | `gBenchProbe.tick` after the field: the latest logic tick that started. |
| `image_tick` | The logic tick whose image the VI shows. |
| `keyframe` | 1 when the shown image is its tick's final image (always 1 without `gBenchCfbKeyframe`). |
| `divisor` | `framerateDivisor` after the latest tick. |

### Metric definitions

- **Fields per displayed image.** A complete image is a run of fields between two `VI_ORIGIN` changes inside the window. The partial images at the window edges are left out. The pass rule is that every image lasts 1 field (`image_share_at_1` = 1).
- **Fields per logic tick.** A tick's length is the number of fields from the first field showing any of its images to the first field showing the next tick's image. This is VI-locked, unlike the start time of `Graph_Update`, which moves with CPU and RDP time inside a field. The first and last tick in the window are left out. A **lag tick** lasts longer than the divisor at its first field. A **skipped tick** never appears. Its time counts toward the tick before it.
- **F1.** `fidelity.py` takes each scene's keyframes by `image_tick`, over the tick range both windows cover. A tick passes when both sides have a keyframe for it and the two `fb_hash` values are equal. The output is pass or fail, the first differing tick, and the number of differing ticks.

## Pins

A pin replaces a timing value that feeds game logic or visuals with a fixed value, so that a faster candidate plays the same game as the reference. Reference and candidates are both `BENCH=1` builds and share every pin. A pin that changes retail behavior is a fidelity confound: F1 compares against the pinned reference, not against a console.

| Site | Retail | Under `BENCH` | Changes retail behavior |
|---|---|---|---|
| `src/code/z_play.c:2265` (`Play_Init`) | `Rand_Seed(osGetTime())` | `Rand_Seed(gBenchConfig.seed)` | Yes. Retail's seed depends on boot time. |
| `src/code/bench.c:253` (`Bench_Boot`) | none | `Rand_Seed(gBenchConfig.seed)` before the first game state | Yes. Retail has the default seed there. |
| `src/code/game.c:311` (`func_80173B48`, the effect budget for snow, rain and Link's dust: `z_kankyo.c:2080`, `z_object_kankyo.c:521`, `z_player.c:10320`, `10336`, `10346`) | `divisor * gIrqMgrRetraceTime - gRDPTimeTotal`, in ns | `divisor * gBenchConfig.retraceTicks`, in ns, while `BENCH_FLAG_PIN_EFFECT_BUDGET` is set (default) | Yes. It drops the RDP-time term. Otherwise a faster RDP draws more particles. |
| `src/code/game.c:312` (`gIrqMgrRetraceTime` in the effect budget) | measured at boot by `IrqMgr` (`src/boot/irqmgr.c:199-203`) | `gBenchConfig.retraceTicks`, default 782715, which is the value the pinned runner measures (`BENCH START` line) | No on the runner. The effect budget is its only game-logic consumer. |
| `src/code/game.c:156` (`GameState_GetInput`) | the pad | `gBenchConfig.inputs` from the tick after `settleTick` | No change to game logic. Input timing is exact rather than human. |

Paths that feed timing into game logic and are not pinned. Each one is outside this unit's scope (`src/` beyond the `BENCH` hook files), or pinning it would change the game. F1 catches any divergence they cause in a corpus scene.

| Site | Feedback | Corpus exposure |
|---|---|---|
| `src/code/z_scene.c:93` (object loads), `src/code/z_room.c:608` (room loads), `src/code/z_player_lib.c:208` (mask object), `src/code/z_player_lib.c:2946` (get-item object), `src/code/z_parameter.c:6791` (`Interface_LoadStory`), `src/code/z_play_hireso.c:1167` (Bombers' Notebook) | The tick that polls (`OS_MESG_NOBLOCK`) sees a DMA as done or not done, depending on DMA time against the tick. | Object and room loads happen at scene entry before the window settles. Within the window, a candidate that changes when a DMA completes can change the tick an actor appears. |
| `AudioSeq_GetActiveSeqId` (12 call sites) and `Audio_IsSequencePlaying` (3) in `src/code` and `src/overlays` | Game logic reads sequence state that the audio thread sets on its own AI-driven schedule. | Sites run in specific actors and cutscenes. None was identified in the corpus scenes. |
| `src/code/z_parameter.c:829, 890, 5692-6187`, `src/overlays/actors/ovl_En_Mm3/z_en_mm3.c:416` | Minigame, postman and bottle timers count `osGetTime()`, not ticks. | No timer runs in the corpus. |
| `src/code/z_sram_NES.c:1925-1978` | Flash save writes wait for 2 s of `osGetTime()`. | No save in the windows. |
| `src/audio/lib/thread.c:156`, `967` | `osGetCount()` seeds audio randomness. | Audio only. Not visible. |
| `src/code/speed_meter.c:122-128` | Draws `gRSPGfxTimeTotal`, `gRDPTimeTotal` and `gGraphUpdatePeriod`. | Only when `R_ENABLE_ARENA_DBG` is nonzero (`game.c:115`). 0 in the corpus. |
| RDP noise (hardware, no source line) | The noise LFSRs step once per RDP clock (the fork's `ares/n64/rdp/timed.hpp`, from Thar0/RDP-Noise console dumps). Dithered pixels (alpha dither, color dither, the combiner's NOISE input) depend on when the RDP draws them. | Every scene except `filesel`. A change that moves RDP timing changes those pixels by one 5-bit step while the game stays identical, so F1 fails (see `docs/fps60/baseline.md`). |
| `src/code/sched.c:245, 427`, `src/code/graph.c:159` | Frame pacing itself: swaps, retraces, and the 3 s RCP hang timeout. | This is what the harness measures, not a confound. |
| `src/code/z_nmi_buff.c:32`, `src/boot/libc64/__osMalloc.c:37`, `src/boot/fault.c:553` | Reset time, allocation debug stamps, the crash screen. | Not read by game logic. |

`BENCH` also changes the boot (`Bench_BootPlay` and `Bench_BootFileSelect` in `bench.c` take shortcuts that the console logo, title and file select would otherwise take) and adds RAM (the probe, the 4 KiB input table, `tools/bench`'s record buffer). Base and candidate share these.

## Gotchas

- The harness reads `gBenchProbe` by offset (`run.py`, `PROBE_*`). A change to `BenchProbe` or `BenchConfig` in `include/bench.h` is a harness change. `run.py` checks the probe's magic and the config's magic, not the layout.
- `run.py` never clears `BENCH_FLAG_PIN_EFFECT_BUDGET`. `tools/bench/bench.py` leaves the defaults, so it also runs pinned and also records (`BENCH_FLAG_RECORD`). `run.py` turns recording off, so the ROM never exits on its own.
- `title` settles 127 ticks after boot. The Play scenes with an entrance cutscene (`field`, `greatbay`) settle about 200 ticks after boot. Their runs take longer for that reason.
- The first image of a window often belongs to a tick before `settleTick`, because the image the VI shows lags the latest tick by about two ticks.
- F1 is sensitive to RDP timing through the noise generator (see the table above). A planted change that draws one pixel on tick 100 of `sct` fails F1 in `sct` only when it is compared with a build of the same layout. Its first differing tick is 99, because the extra CPU work during tick 100 moves the RDP's render of tick 99 and so its noise. Compared with the reference, the same build fails in 5 scenes, each within its first 12 compared ticks. The H1a report has the patches and numbers.

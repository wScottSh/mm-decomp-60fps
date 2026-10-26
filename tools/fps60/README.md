# fps60 harness

The fps60 harness measures how a `BENCH=1` build of this tree presents images and runs its logic ticks. It runs each scene of a fixed corpus on the pinned ares runner and writes per-field data, the image each logic tick ends on, and summaries. `fidelity.py` compares two runs tick by tick (check F1). The program's rules are in `~/.claude/orchestrate/mm-60fps/preferences.md`. After unit H1 lands, `tools/fps60/` is frozen (preferences line 7).

## Run

```sh
tools/fps60/worktree-setup.sh UNIT BRANCH [BASE_REF]   # prints ~/repos/mm-wt/UNIT, ready to build
make -j32 BENCH=1                                       # build/n64-us-bench/mm-n64-us.z64 and .map
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --check-determinism
tools/fps60/run.py build/n64-us-bench/mm-n64-us.z64 --out RESULTS --noise-pin off   # perf runs: retail RDP noise
tools/fps60/fidelity.py BASE_RESULTS CAND_RESULTS
```

- `worktree-setup.sh` creates the worktree (or reuses it), copies the untracked build inputs from `~/repos/mm-decomp-60fps` (baseroms, `extracted/`, the IDO recompilation, `.venv`, `.make_options.mk`), and builds `tools/`. It only reads the main checkout. It copies rather than hard-links, so `make setup` in a worktree cannot write into the main checkout. A worktree takes 723 MB before its first build and 1.19 GB after a `BENCH=1` build. Setup took 7 s on a new worktree and 1 s on a rerun (measured on unicron).
- `run.py` reads symbols from the map beside the ROM (`--map` overrides). It checks that the runner's sha256 starts with `1f9d59f6` (set `FPS60_ANY_RUNNER=1` only for experiments). It runs at most 4 runner processes (`--jobs`, preferences line 14), and `--scenes title,sct` selects scenes.
- A full corpus run took 149 s of wall time with 4 jobs, and each scene took 41 to 102 s (measured on unicron, see `docs/fps60/baseline.md`).
- `run.py --check-determinism` runs the corpus into `RESULTS/run1` and `RESULTS/run2` and fails unless every file except `wall.tsv` and `host.txt` is byte-identical.
- `run.py --noise-pin on|off|check` sets the noise pin (see Noise pin below). The default is `on`, for fidelity runs. Perf runs use `off`. `check` is `on` plus a second walk of every gfx task, which must give the same record and patch nothing. With the pin on, `run.py` fails a scene whose walk met an unresolved target, an unknown opcode, an overflow or a check mismatch, and prints the totals.
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
| `<scene>/keyframes.tsv` | The per-tick image sequence: one row per complete keyframe image, with `image_tick`, `first_field`, `fields` shown, `final_frame` (stats row of its last field), `fb_hash` at its last field, and `hash_changes` (times `fb_hash` changed while it was shown, 0 when the framebuffer was not written while displayed). With the noise pin on, also that tick's walk: `noise_hash`, `noise_cmds`, `noise_patched` and `dl_cmds` (`BenchNoiseRecord`). |
| `<scene>/ticks.tsv` | One row per complete tick: `tick`, `first_field` its first image appears, `fields` until the next tick's first image, `divisor`, and `skipped_after` (ticks after it that never appeared). |
| `<scene>/summary.json` | `noise_pin`, and with the pin on `noise_totals` (`BenchNoiseTotals` after the window), `noise_cmds_per_keyframe_max` and `noise_patched_in_window`. `fields`, `images`, `fields_per_image` (counts at 1 to 5 and 6 or more), `image_share_at_1`, `image_fields_max`, `ticks`, `fields_per_tick`, `lag_ticks`, `skipped_ticks`, `tick_fields_max`, `divisors`, `keyframes`, `first_tick`, `last_tick`, `window_start_frame`, `settle_tick`, `retrace_ticks`. |
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
- **F1.** `fidelity.py` takes each scene's keyframes by `image_tick`, over the tick range both windows cover. A tick passes when both sides have a keyframe for it, the two `fb_hash` values are equal, and, with the noise pin on, the two `noise_hash` values are equal. Both runs must have the same noise pin setting. The output is pass or fail, the first differing tick and which hash differs there, and the number of differing ticks for each hash.

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
| RDP noise (hardware, no source line) | The noise LFSRs step once per RDP clock (the fork's `ares/n64/rdp/timed.hpp`, from Thar0/RDP-Noise console dumps). Dithered pixels (alpha dither, color dither, the combiner's NOISE input) depend on when the RDP draws them. | Every scene except `filesel`. With the noise pin off, a change that moves RDP timing changes those pixels by one 5-bit step while the game stays identical, so F1 fails (see `docs/fps60/baseline.md`). The noise pin removes this path for fidelity runs. |
| `src/code/sched.c:245, 427`, `src/code/graph.c:159` | Frame pacing itself: swaps, retraces, and the 3 s RCP hang timeout. | This is what the harness measures, not a confound. |
| `src/code/z_nmi_buff.c:32`, `src/boot/libc64/__osMalloc.c:37`, `src/boot/fault.c:553` | Reset time, allocation debug stamps, the crash screen. | Not read by game logic. |

`BENCH` also changes the boot (`Bench_BootPlay` and `Bench_BootFileSelect` in `bench.c` take shortcuts that the console logo, title and file select would otherwise take) and adds RAM (the probe, the 4 KiB input table, `tools/bench`'s record buffer). Base and candidate share these.

## Noise pin

Preferences line 20: retail never shows the same noise-dithered pixels twice, so noise patterns are not part of retail's image. Fidelity runs neutralize noise identically in reference and candidate, and F1 also compares how the game used noise.

The pin also removes a second path from RDP timing to pixels: span buffer coherency. Without `G_PM_1PRIMITIVE`, the RDP prefetches the next span's color and Z before the previous primitive's write-back lands (SDK 12.2.3; the fork models it in `RDP::prefetch`, `ares/n64/rdp/timed.cpp`). Where primitives overlap and read memory (`IM_RD` or `Z_CMP`), the read sees old or new memory depending on RDRAM contention from the CPU, RSP, VI and AI. With noise pinned and the game state identical at every tick (actor positions, rotations, scales and camera, checked by a throwaway per-tick hash), a dead-code build still differed from the reference by single edge pixels in `sct` (25 ticks) and `field` (1 tick). Forcing `G_PM_1PRIMITIVE` removed every one (H1c report). The fork's spec lists `rdp.read-gate` as fail and `hw:stale-read` as pending, so this path is modeled but not yet checked against a console.

`BenchConfig.noisePin` (`include/bench.h`, `BENCH_NOISE_PIN_*`) selects it. The ROM's default is off, so `tools/bench` keeps retail rendering. `run.py` patches it per run.

What it does. `Bench_PinNoise` (`src/code/bench.c`, called from `Graph_TaskSet00` in `graph.c` after the previous gfx task finished and before the task is sent) walks every display-list command the task reaches, in RSP order:

- `G_DL` calls and branches, `G_ENDDL`, the segment table (`G_MOVEWORD G_MW_SEGMENT`), and `G_LOAD_UCODE` switches between F3DZEX2 and S2DEX2. `G_BRANCH_Z` and `G_CULLDL` depend on vertex depth, so the walk takes both paths of a `G_BRANCH_Z` and never culls. A target outside RDRAM or in a segment that was never set counts as `unresolved`. An opcode the active microcode does not define, and S2DEX2's `G_SELECT_DL`, count as `unknown`.
- A noise command (the name covers both paths) is a `G_SETOTHERMODE_H`, `G_SETOTHERMODE_L`, `G_RDPSETOTHERMODE` or `G_SETCOMBINE` that selects one of the noise reads of the fork's RDP engine (`ares/n64/rdp/engine/rdp_core.c`, `rdp_blend.c`), or that writes the pipeline mode. Each one is rewritten in RDRAM to a fixed substitute:

| Noise read | Substitute | Why this one |
|---|---|---|
| `G_CD_NOISE` (RGB dither) | `G_CD_MAGICSQ` | Alpha dither `PATTERN` and `NOTPATTERN` read the magic square under both, so alpha is unchanged. |
| `G_AD_NOISE` (alpha dither) | `G_AD_PATTERN` | Ordered, and the same mean dither. |
| `G_AC_DITHER` (alpha compare against a noise threshold) | `G_AC_THRESHOLD` | Keeps the compare, against the blend color's alpha. |
| combiner RGB input A = `NOISE`, either cycle | `G_CCMUX_1` | `NOISE` is abc100000 in 1/256 units; its mean is 1. |
| pipeline mode (`G_PM_NPRIMITIVE` or `G_PM_1PRIMITIVE`) | `G_PM_1PRIMITIVE` | Each primitive waits for the previous one's write-back, so no read is stale. Primitives drawn before a task's first pipeline-mode write keep the RSP's starting mode. |

- The per-tick record (`BenchProbe.cfbNoise[i]`, parallel to `cfbTick[i]`) holds `noise_hash`, FNV-1a over both words of every noise command as the game wrote it, in walk order. It also holds the noise command count, how many of them this walk rewrote, and the commands visited. `run.py` reads the record of the displayed `CfbInfo` each field, like `cfbTick`.
- Idempotent. Display lists in asset, object and static memory are rewritten in place, and the rewrite stays. A log (`sBenchNoiseStatic`) keeps each rewritten command's address, original and substitute. A later walk that finds the substitute at that address hashes the original. If the game has rewritten the command since, the walk treats it as new. The gfx pools are rebuilt every frame, so their log (`sBenchNoisePool`) is cleared at each walk. `--noise-pin check` proves the property on a run: every task is walked twice, and the second walk must hash the same and patch nothing (`mismatch` 0).

What it detects. With the pin on, a tick fails F1 when its image differs anywhere (pixels outside noise, and the substitutes' deterministic dither) or when the game emitted different noise commands: a noise mode added, removed or changed (for example `G_CD_MAGICSQ` written where retail writes `G_CD_NOISE`), a changed combiner that reads or stops reading `NOISE`, a changed pipeline mode, or the same commands in a different order or count. Every othermode write that covers the pipeline bit is hashed, so a change that adds, drops or reorders those writes also fails, even when the image could not show it.

What it cannot detect.

- The noise patterns themselves, and which overlapping pixels read stale memory. A change that only moves RDP timing passes, which is the point (line 20).
- Edge pixels where retail would read stale memory: the pin always draws them as 1PRIMITIVE does.
- Pixels in noise-dithered draws that differ only by the dither the substitute replaced. The substitute draws them with a fixed dither, so a change in the colors, geometry or textures of a noise draw still shows. But a difference that retail's noise would have hidden or shown at random shows here as the substitute renders it.
- A command at an address that the walk patched earlier, which the game later overwrites with exactly the substitute's words. The walk then hashes the old original. Reference and candidate do the same, so F1 only misses a candidate that changes this alone. Gfx-pool addresses are immune, because their log lives for one walk.
- A rewritten command that the game reads back as data and copies elsewhere. The copy then holds the substitute and is not a noise command. No such read is known in the corpus.
- Noise commands in display lists that no gfx task reaches, and noise in a task other than `Graph_TaskSet00`'s (MM sends no other gfx task).
- Partial othermode writes that cover half of a 2-bit dither or compare field count as `unknown`, and fail the run, because their result depends on state before.

Cost. The walk visits about 11,500 commands per tick in `sct` (measured, see the H1c report) on the CPU before the task is sent, and 1PRIMITIVE adds RDP time per primitive. Both change timing, but with the pin on timing no longer reaches the image. File select, which already lags in retail, shows fewer ticks in its window with the pin on. Perf runs use `--noise-pin off`, where `Bench_PinNoise` returns at once.

Pin off is retail rendering, not H1a's bytes. Any code or data layout change moves CPU timing and so the RDP clock at each pixel, so a pin-off run of this tree does not reproduce H1a's `fb_hash` values even though the pin-off path only returns (H1c report).

## Gotchas

- The harness reads `gBenchProbe` by offset (`run.py`, `PROBE_*`, `CFG_NOISE_PIN`). A change to `BenchProbe` or `BenchConfig` in `include/bench.h` is a harness change. `run.py` checks the probe's magic and the config's magic, not the layout.
- `run.py` never clears `BENCH_FLAG_PIN_EFFECT_BUDGET`. `tools/bench/bench.py` leaves the defaults, so it also runs pinned and also records (`BENCH_FLAG_RECORD`). `run.py` turns recording off, so the ROM never exits on its own.
- `title` settles 127 ticks after boot. The Play scenes with an entrance cutscene (`field`, `greatbay`) settle about 200 ticks after boot. Their runs take longer for that reason.
- The first image of a window often belongs to a tick before `settleTick`, because the image the VI shows lags the latest tick by about two ticks.
- With the noise pin off, F1 is sensitive to RDP timing through the noise generator (see the table above). A planted change that draws one pixel on tick 100 of `sct` fails F1 in `sct` only when it is compared with a build of the same layout. Its first differing tick is 99, because the extra CPU work during tick 100 moves the RDP's render of tick 99 and so its noise. Compared with the reference, the same build fails in 5 scenes, each within its first 12 compared ticks. The H1a report has the patches and numbers.

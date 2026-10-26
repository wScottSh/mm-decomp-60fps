# fps60 harness

The fps60 harness measures how a `BENCH=1` build of this tree presents images, runs its logic ticks, and loads the CPU, RSP and RDP. It runs each scene of a fixed corpus on the pinned ares runner and writes per-field data, the image each logic tick ends on, load per field and per tick, and summaries. `fidelity.py` compares two runs tick by tick (check F1). The program's rules are in `~/.claude/orchestrate/mm-60fps/preferences.md`. After unit H1 lands, `tools/fps60/` is frozen (preferences line 7).

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
| `load.tsv` | One row per scene: `field_ms` (the median runner field), and for each load resource the max, p99 and p50 per field (`f_`) and per tick (`t_`) as a share of one field, and `hook_ms_p50`. |
| `<scene>/load_summary.json` | Per resource, per field and per tick: `max`, `p99`, `p50` in ms and as a share of one field. Per tick also: CPU time per thread (`tick_cpu`), per graph phase in wall and CPU time (`tick_phase_wall`, `tick_phase_cpu`), per system (`tick_system`), thread switches, the top 10 actors by update plus draw time, and the hook cost. |
| `<scene>/load_fields.tsv` | One row per retrace interval in the window (see Load metrics). |
| `<scene>/load_ticks.tsv` | One row per logic tick that starts and ends inside the window: the `BenchTick` record (`include/bench.h`) plus `cpu_busy`, `rsp_gfx` and `other_threads`. |
| `<scene>/load_actors.tsv` | Graph thread time per actor id over the window, in update and draw, sorted by the sum. |

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
- **Load.** See Load metrics below.
- **F1.** `fidelity.py` takes each scene's keyframes by `image_tick`, over the tick range both windows cover. A tick passes when both sides have a keyframe for it and the two `fb_hash` values are equal. The output is pass or fail, the first differing tick, and the number of differing ticks.

## Load metrics

The load counters are always on in a `BENCH` build and cost nothing to read: the runner peeks RAM between fields without touching emulated timing. Perf runs use them. Units in the TSVs: CPU times are PClocks (93.75 MHz), RSP times are the runner's 750 MHz units (`XPROFREAD 0x0200`), RDP counters are RCP clocks (62.5 MHz). The summaries convert to ms. A share is ms divided by the median runner field (`stats.tsv` `cpu_cycles` per field, 16.715 ms measured).

### CPU time per thread

`Bench_EnqueueAndYield` (`src/boot/idle.c`) replaces `__osEnqueueAndYield` at its 7 libultra call sites under `#if BENCH` (`osRecvMesg`, `osSendMesg`, `osJamMesg`, `osStartThread`, `osSetThreadPri`, `osStopThread`, `osYieldThread`). Every thread switch in MM goes through one of them, or through an interrupt whose woken thread resumes inside one of them. At each switch, `Bench_CpuSwitch` charges `2 * (COUNT - last)` PClocks to the thread that has been running and records which thread runs next: the run queue's head, or the yielding thread when it goes back on the run queue ahead of that head. When an interrupt preempts a thread, nothing runs the hook until the woken thread resumes in its own yield, and that charge goes to the preempted thread. So each PClock is charged once, to the thread that ran it.

- **CPU busy** = wall time minus the idle thread's time. The idle thread (`Idle_ThreadEntry` after it sets `OS_PRIORITY_IDLE`) runs only when no other thread can, including while the graph thread waits on the RCP in `Graph_TaskSet00`.
- The exception handler's time goes to the thread that was running when the exception was taken, and the dispatch after a switch goes to the thread that runs next. So a thread's time includes the kernel time of the interrupts that hit it. emux has an exception-level cycle counter (`XPROFREAD 0x0001`), but the fork credits pending cycles to it in batches at count flushes (`ares/n64/cpu/cpu.cpp` `stepCount`), so it is not used.
- `switches` counts hook calls (two per switch).
- Graph thread brackets use `Bench_CpuNow`, the running thread's charged time plus the time since its last switch, read with interrupts disabled. A system bracket therefore excludes the time of every thread that preempted the graph thread inside it.

### Per field

`Bench_FieldSnapshot` runs in the scheduler thread at every VI retrace (`Sched_HandleRetrace`) and writes the cumulative counters into a ring of 4 `BenchField` records. It also folds the 24-bit DPC counters into 32-bit totals and clears them, so they never wrap. The runner peeks the whole ring after every field. A runner field can end just before or just after the scheduler takes the snapshot of the retrace at its end, so the harness keys fields by retrace, not by runner field: `load_fields.tsv` has one row per interval between consecutive retrace snapshots, from the latest snapshot at the window's start to the latest at its end (600 intervals in the reference corpus, measured). The interval's own wall time (`wall`) varies with the scheduler's latency by about ±0.7% around the runner field (measured, `snapshot_wall_pclk_min` and `_max` in `load_summary.json`), so a per-field share can exceed 1.0 by that much.

A peek can see a record's cache lines in different states (measured: one 16-byte line stale while the lines before and after it, `seq` included, were new). So the ROM writes both rings only through their uncached KSEG1 aliases, after flushing their cached lines at boot, and stores a record's `seq` (or `tick`) as `0xFFFFFFFF` first and its number last. The harness also uses a retrace record only when two consecutive reads agree. A record stays in the ring for 4 retraces, so each is read 3 or 4 times, and `run.py` runs two fields past the window to read the last ones. The actor totals stay cached, so a line of them read at the window's end can be up to one tick old (inferred from the same behavior).

| Column | Meaning |
|---|---|
| `seq` | The retrace that ends the interval. |
| `wall` | PClocks between the two snapshots. |
| `cpu_busy`, `idle` | Wall minus the idle thread's time, and the idle thread's time. |
| `graph`, `audio` | The graph thread's and the AudioMgr thread's time (`AudioThread_Update` runs there). |
| `switches` | Thread switch hooks. |
| `rsp`, `rsp_gfx`, `rsp_audio` | Non-halted RSP time: all of it (emux global `XPROFREAD 0x0200`), the part outside audio tasks, and the part inside them. |
| `rdp_pipe`, `rdp_buf` | `DPC_PIPEBUSY` and `DPC_BUFBUSY` RCP clocks. |

**RSP split.** `Sched_RunTask` opens emux profile slot 1 just before `osSpTaskStartGo` of an audio task, and `Sched_HandleRSPDone` closes it and adds its non-halted RSP time. The RSP runs one task at a time, and the scheduler yields the gfx task before it starts an audio task, so the slot holds only audio time. A snapshot or tick boundary that falls inside an audio task splits the slot there. Gfx is total minus audio. These come from emux, so on hardware both read 0.

**RDP.** `rdp_pipe` is RDP pipe busy (`DPC_PIPEBUSY`). In the fork, `PIPE_BUSY` holds from a `DPC_END` write until `SYNC_FULL` retires (`ares/n64/rdp/timed.hpp`, `Dpc::busy` and `syncFullRetired`), so it includes time the RDP waits for the RSP to send more commands. `rdp_buf` (`BUFBUSY`: the command FIFO is not empty or a command executes) leaves out most of that wait. Which of the two the budget uses is for B1 to decide.

### Per tick

A tick runs from its `Graph_Update` start (`Bench_BeginFrame`) to the next one. `Bench_BeginFrame` writes the ending tick's `BenchTick` into a ring of 640 records, and `run.py` reads the ring once after the window. `load_ticks.tsv` has the ticks that start and end inside the window.

- `wall`, `idle`, `graph`, `audio`, `sched`, `irqmgr`, `padmgr`, `dmamgr`: as per field. `other_threads` is the rest (PI manager, main, flash).
- `rsp`, `rsp_audio`, `rdp_pipe`, `rdp_buf`: as per field, with the DPC counters read live at the boundary.
- `wall_<phase>`, `cpu_<phase>`: the graph thread's wall and CPU time per phase of `Graph_Update`. `pre` is input and `Audio_Update`, `update` is `Play_Update`, `draw` is `Play_Draw`, `game` is the rest of `GameState_Update` (all of it outside Play), `post` is the display list close and task setup, `rcp_wait` is `Graph_TaskSet00` blocked on the previous gfx task, and `tail` is the rest until the next tick.
- `sys_<system>`: graph thread CPU time per engine system. Time is exclusive: the innermost entered system gets it, and `other` gets the rest, so the systems sum to the tick's `graph`. The brackets sit at the call sites in `z_play.c`, `z_actor.c` and `graph.c`: `actor_update` (`Actor_UpdateAll`), `actor_draw` (`Actor_DrawAll` without its effect draws), `collision` (`CollisionCheck_AT`, `_OC`, `_Damage`, `_ClearContext`), `room_draw` (`Scene_Draw`, `Room_Draw`), `skybox`, `environment` (`Environment_Update` and the `Environment_Draw*` calls), `camera` (`Camera_Update`), `message` (`Message_*` and `Interface_*` update and draw), `effect` (`Effect_*`, `EffectSs_*`, `EffFootmark_*`), `audio_update` (`Audio_Update`), and `bench` (the tick bookkeeping). Audio synthesis is the AudioMgr thread, not a graph system.
- Actor time: `Actor_UpdateActor` brackets each `actor->update` call and `Actor_Draw` brackets itself, keyed by actor id.

### Hook cost

Every bracket calls `Bench_CpuNow` twice. The ROM times 64 calls at boot (`clock_read_pclk` in the `BENCH START` line and `hook_clock_read_pclk` in `load_summary.json`), and each tick counts its calls (`clock_reads`). `hook_ms_per_tick` is their product. It leaves out the switch hooks (`switches` calls of `Bench_CpuSwitch`). The hooks are the same code in every `BENCH` build and run once per call site, so two builds that play the same game pay the same count. A candidate must keep every bracket where it is: moving one moves time between systems. The H1b report has the measured cost.

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

`BENCH` also changes the boot (`Bench_BootPlay` and `Bench_BootFileSelect` in `bench.c` take shortcuts that the console logo, title and file select would otherwise take), adds RAM (the probe, the 4 KiB input table, the load rings: 111 KiB of bss, against 73 KiB for the per-frame record buffer they replace), and adds the load hooks' CPU time (see Load metrics). Base and candidate share these. `tools/fps60/memmap.py ELF...` prints the static RAM layout of builds side by side.

## Gotchas

- The harness reads `gBenchProbe` by offset (`run.py`, `PROBE_*`). A change to `BenchProbe` or `BenchConfig` in `include/bench.h` is a harness change. `run.py` checks the probe's magic and the config's magic, not the layout.
- `run.py` never clears `BENCH_FLAG_PIN_EFFECT_BUDGET`. The ROM never exits on its own: the runner script stops it.
- `title` settles 127 ticks after boot. The Play scenes with an entrance cutscene (`field`, `greatbay`) settle about 200 ticks after boot. Their runs take longer for that reason.
- The first image of a window often belongs to a tick before `settleTick`, because the image the VI shows lags the latest tick by about two ticks.
- F1 is sensitive to RDP timing through the noise generator (see the table above). A planted change that draws one pixel on tick 100 of `sct` fails F1 in `sct` only when it is compared with a build of the same layout. Its first differing tick is 99, because the extra CPU work during tick 100 moves the RDP's render of tick 99 and so its noise. Compared with the reference, the same build fails in 5 scenes, each within its first 12 compared ticks. The H1a report has the patches and numbers.
